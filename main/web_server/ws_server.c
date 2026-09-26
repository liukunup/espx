/**
 * @file ws_server.c
 * @brief WebSocket endpoint implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <cJSON.h>

#include "task_util.h"
#include "app_info.h"
#include "ws_server.h"
#include "web_server.h"
#include "node_config.h"
#include "device_manager.h"
#include "device_type.h"
#include "config_apply.h"
#include "event_bus.h"
#include "net_services/time_sync.h"
#include "net_services/mdns_service.h"
#include "sys_stats.h"

static const char *TAG = "ws_server";

#define WS_MAX_CLIENTS      4
#define WS_MAX_FRAME        1024
#define WS_BROADCAST_MS     1000

static httpd_handle_t s_server = NULL;
static TaskHandle_t s_task = NULL;
static volatile bool s_running = false;
static volatile bool s_dirty = true;      /* pending immediate broadcast */
static char *s_last_state = NULL;         /* de-duplicate identical pushes */

/* -------------------------------------------------------------------------- */
/* state snapshot                                                             */
/* -------------------------------------------------------------------------- */

static cJSON *build_state(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "state");

    cJSON *devices = cJSON_AddArrayToObject(root, "devices");
    for (size_t i = 0; i < device_get_count(); i++) {
        const device_t *dev = device_get_by_index(i);
        if (dev == NULL) continue;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", dev->id);
        cJSON_AddStringToObject(item, "type", dev->type->name);
        cJSON_AddStringToObject(item, "description", dev->type->description);
        cJSON_AddBoolToObject(item, "enabled", dev->enabled);
        cJSON_AddBoolToObject(item, "initialized", dev->initialized);
        if (dev->config) {
            cJSON_AddItemToObject(item, "config", cJSON_Duplicate((cJSON *)dev->config, true));
        }
        if (dev->enabled && dev->initialized && dev->type->read) {
            cJSON *value = cJSON_CreateObject();
            if (dev->type->read((device_t *)dev, value) == ESP_OK) {
                cJSON_AddItemToObject(item, "value", value);
            } else {
                cJSON_Delete(value);
            }
        }
        cJSON_AddItemToArray(devices, item);
    }

    cJSON_AddNumberToObject(root, "uptime", (double)(esp_timer_get_time() / 1000000ULL));
    cJSON_AddNumberToObject(root, "epoch", (double)time_sync_epoch());
    char iso[32];
    if (time_sync_iso8601(iso, sizeof(iso)) == ESP_OK) {
        cJSON_AddStringToObject(root, "time", iso);
    }

    /* Identity and environment travel with every snapshot, so a client needs
     * nothing but the state stream to render the page. The one-off "hello"
     * frame is a convenience, not a dependency. */
    cJSON_AddStringToObject(root, "id", node_config_get_device_id());
    cJSON_AddStringToObject(root, "name", node_config_get_name());
    cJSON_AddStringToObject(root, "version", app_version());
    if (mdns_service_is_running()) {
        cJSON_AddStringToObject(root, "mdns", mdns_service_fqdn());
    }
    cJSON_AddNumberToObject(root, "ws_clients", (double)ws_server_client_count());

    sys_stats_t st;
    sys_stats_get(&st);
    cJSON *cpu = cJSON_AddObjectToObject(root, "cpu");
    cJSON_AddNumberToObject(cpu, "freq_mhz", st.cpu_freq_mhz);
    if (st.cpu_valid) cJSON_AddNumberToObject(cpu, "usage", (double)st.cpu_usage);
    cJSON_AddNumberToObject(cpu, "tasks", st.task_count);

    cJSON *ram = cJSON_AddObjectToObject(root, "ram");
    cJSON_AddNumberToObject(ram, "internal_free", (double)st.int_free);
    cJSON_AddNumberToObject(ram, "internal_min_free", (double)st.int_min_free);
    cJSON_AddNumberToObject(ram, "internal_used_pct",
                            (double)sys_stats_internal_used_pct(&st));
    if (st.psram_present) {
        cJSON_AddNumberToObject(ram, "psram_free", (double)st.psram_free);
        cJSON_AddNumberToObject(ram, "psram_used_pct",
                                (double)sys_stats_psram_used_pct(&st));
    }

    return root;
}

/* -------------------------------------------------------------------------- */
/* broadcast                                                                  */
/* -------------------------------------------------------------------------- */

static void broadcast_text(const char *text, int len)
{
    if (s_server == NULL || text == NULL) {
        return;
    }

    size_t count = WS_MAX_CLIENTS;
    int fds[WS_MAX_CLIENTS];

    if (httpd_get_client_list(s_server, &count, fds) != ESP_OK) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        if (httpd_ws_get_fd_info(s_server, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET) {
            continue;
        }

        httpd_ws_frame_t frame = {
            .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)text,
            .len = (size_t)len,
        };

        esp_err_t err = httpd_ws_send_frame_async(s_server, fds[i], &frame);
        if (err != ESP_OK) {
            ESP_LOGD(TAG, "send to fd %d failed: %s", fds[i], esp_err_to_name(err));
        }
    }
}

static void broadcast_state(bool force)
{
    cJSON *state = build_state();
    if (state == NULL) return;

    char *text = cJSON_PrintUnformatted(state);
    cJSON_Delete(state);
    if (text == NULL) return;

    /* Skip identical pushes: the broadcast runs on a timer, but a UI does not
     * need the same snapshot repeatedly. */
    if (!force && s_last_state && strcmp(s_last_state, text) == 0) {
        free(text);
        return;
    }

    free(s_last_state);
    s_last_state = strdup(text);

    int n = ws_server_client_count();
    if (n > 0) {
        ESP_LOGD(TAG, "broadcast state to %d client(s)", n);
    }
    broadcast_text(text, (int)strlen(text));
    free(text);
}

int ws_server_client_count(void)
{
    if (s_server == NULL) return 0;

    size_t count = WS_MAX_CLIENTS;
    int fds[WS_MAX_CLIENTS];
    if (httpd_get_client_list(s_server, &count, fds) != ESP_OK) {
        return 0;
    }

    int n = 0;
    for (size_t i = 0; i < count; i++) {
        if (httpd_ws_get_fd_info(s_server, fds[i]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            n++;
        }
    }
    return n;
}

void ws_server_request_broadcast(void)
{
    s_dirty = true;
}

/* -------------------------------------------------------------------------- */
/* inbound commands                                                           */
/* -------------------------------------------------------------------------- */

static void reply(const char *type, cJSON *extra)
{
    cJSON *out = cJSON_CreateObject();
    cJSON_AddStringToObject(out, "type", type);
    if (extra) {
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, extra) {
            cJSON_AddItemToObject(out, item->string, cJSON_Duplicate(item, true));
        }
    }
    char *text = cJSON_PrintUnformatted(out);
    if (text) {
        broadcast_text(text, (int)strlen(text));
        free(text);
    }
    cJSON_Delete(out);
}

static void reply_error(const char *msg)
{
    cJSON *e = cJSON_CreateObject();
    cJSON_AddStringToObject(e, "error", msg);
    reply("error", e);
    cJSON_Delete(e);
}

static void handle_frame(cJSON *msg)
{
    cJSON *type = cJSON_GetObjectItem(msg, "type");
    if (!cJSON_IsString(type)) {
        reply_error("missing type");
        return;
    }

    const char *t = type->valuestring;

    if (strcmp(t, "ping") == 0) {
        reply("pong", NULL);
        return;
    }

    if (strcmp(t, "refresh") == 0) {
        ws_server_request_broadcast();
        return;
    }

    if (strcmp(t, "read") == 0 || strcmp(t, "write") == 0) {
        cJSON *id = cJSON_GetObjectItem(msg, "id");
        if (!cJSON_IsString(id)) {
            reply_error("missing id");
            return;
        }

        if (strcmp(t, "write") == 0) {
            cJSON *value = cJSON_GetObjectItem(msg, "value");
            if (value == NULL) {
                reply_error("missing value");
                return;
            }
            esp_err_t err = device_write(id->valuestring, value);
            if (err != ESP_OK) {
                reply_error(esp_err_to_name(err));
                return;
            }
        }

        cJSON *res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "id", id->valuestring);
        cJSON *value = cJSON_CreateObject();
        if (device_read(id->valuestring, value) == ESP_OK) {
            cJSON_AddItemToObject(res, "value", value);
        } else {
            cJSON_Delete(value);
        }
        reply("result", res);
        cJSON_Delete(res);

        ws_server_request_broadcast();
        return;
    }

    if (strcmp(t, "config") == 0) {
        cJSON *yaml = cJSON_GetObjectItem(msg, "yaml");
        if (!cJSON_IsString(yaml)) {
            reply_error("missing yaml");
            return;
        }

        config_apply_result_t r;
        char err[128] = {0};
        esp_err_t rc = config_apply_payload(yaml->valuestring, &r, err, sizeof(err));

        cJSON *res = cJSON_CreateObject();
        cJSON_AddBoolToObject(res, "ok", rc == ESP_OK && r.devices_failed == 0);
        cJSON_AddNumberToObject(res, "added", r.devices_added);
        cJSON_AddNumberToObject(res, "updated", r.devices_updated);
        cJSON_AddNumberToObject(res, "removed", r.devices_removed);
        cJSON_AddBoolToObject(res, "reboot_required", r.reboot_recommended);
        const char *m = r.error[0] ? r.error : err;
        if (m[0]) cJSON_AddStringToObject(res, "error", m);
        reply("config_result", res);
        cJSON_Delete(res);

        ws_server_request_broadcast();
        return;
    }

    reply_error("unknown type");
}

/* -------------------------------------------------------------------------- */
/* handshake / frame handler                                                  */
/* -------------------------------------------------------------------------- */

static esp_err_t ws_handler(httpd_req_t *req)
{
    /* A GET with no payload is the handshake: the server has already accepted
     * the upgrade by the time we get here. */
    if (req->method == HTTP_GET) {
        ESP_LOGI(TAG, "client connected (fd=%d)", httpd_req_to_sockfd(req));

        /* Send the identity banner and an immediate snapshot so a freshly
         * opened UI is populated without waiting for the timer. */
        cJSON *hello = cJSON_CreateObject();
        cJSON_AddStringToObject(hello, "type", "hello");
        cJSON_AddStringToObject(hello, "id", node_config_get_device_id());
        cJSON_AddStringToObject(hello, "name", node_config_get_name());
        cJSON_AddStringToObject(hello, "version", app_version());
        char iso[32];
        if (time_sync_iso8601(iso, sizeof(iso)) == ESP_OK) {
            cJSON_AddStringToObject(hello, "time", iso);
        }
        if (mdns_service_is_running()) {
            cJSON_AddStringToObject(hello, "mdns", mdns_service_fqdn());
        }
        char *text = cJSON_PrintUnformatted(hello);
        cJSON_Delete(hello);
        if (text) {
            httpd_ws_frame_t f = { .type = HTTPD_WS_TYPE_TEXT,
                                   .payload = (uint8_t *)text, .len = strlen(text) };
            esp_err_t err = httpd_ws_send_frame(req, &f);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "hello frame send failed: %s", esp_err_to_name(err));
            }
            free(text);
        }

        ws_server_request_broadcast();
        return ESP_OK;
    }

    /* Determine the frame length, then read the payload. */
    httpd_ws_frame_t frame = {0};
    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "recv header failed: %s", esp_err_to_name(err));
        return err;
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        ESP_LOGI(TAG, "client closed (fd=%d)", httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    if (frame.len == 0) {
        return ESP_OK;
    }

    if (frame.len > WS_MAX_FRAME) {
        ESP_LOGW(TAG, "frame too large (%u bytes), ignored", (unsigned)frame.len);
        reply_error("frame too large");
        return ESP_OK;
    }

    uint8_t *buf = malloc(frame.len + 1);
    if (buf == NULL) {
        return ESP_ERR_NO_MEM;
    }

    frame.payload = buf;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "recv payload failed: %s", esp_err_to_name(err));
        free(buf);
        return err;
    }
    buf[frame.len] = '\0';

    if (frame.type == HTTPD_WS_TYPE_TEXT) {
        cJSON *msg = cJSON_Parse((const char *)buf);
        if (msg == NULL) {
            reply_error("invalid JSON");
        } else {
            handle_frame(msg);
            cJSON_Delete(msg);
        }
    }

    free(buf);
    return ESP_OK;
}

/* -------------------------------------------------------------------------- */
/* broadcast task                                                             */
/* -------------------------------------------------------------------------- */

static void ws_task(void *arg)
{
    while (s_running) {
        /* Push on request immediately; otherwise re-check on the timer. */
        if (s_dirty) {
            s_dirty = false;
            broadcast_state(true);
        } else if (ws_server_client_count() > 0) {
            broadcast_state(false);
        }
        vTaskDelay(pdMS_TO_TICKS(WS_BROADCAST_MS));
    }

    s_task = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief Force a push when any device value changes
 */
static void on_bus_event(const event_t *event, void *user_data)
{
    switch (event->type) {
    case EVENT_DEVICE_VALUE_CHANGED:
    case EVENT_DEVICE_ADDED:
    case EVENT_DEVICE_REMOVED:
    case EVENT_DEVICE_CHANGED:
    case EVENT_NODE_READY:
        ws_server_request_broadcast();
        break;
    default:
        break;
    }
}

esp_err_t ws_server_start(httpd_handle_t server)
{
    if (server == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_server = server;

    static const httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .user_ctx = NULL,
        .is_websocket = true,
    };

    esp_err_t err = httpd_register_uri_handler(server, &ws_uri);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to register /ws: %s", esp_err_to_name(err));
        return err;
    }

    /* Push on any state change instead of waiting for the next tick. */
    event_bus_subscribe(EVENT_DEVICE_VALUE_CHANGED, on_bus_event, NULL);
    event_bus_subscribe(EVENT_DEVICE_ADDED, on_bus_event, NULL);
    event_bus_subscribe(EVENT_DEVICE_REMOVED, on_bus_event, NULL);
    event_bus_subscribe(EVENT_DEVICE_CHANGED, on_bus_event, NULL);
    event_bus_subscribe(EVENT_NODE_READY, on_bus_event, NULL);

    if (s_task == NULL) {
        s_running = true;
        s_dirty = true;
        if (/* Internal-RAM stack: a config push over the socket writes NVS. See task_util.h. */
        xTaskCreate(ws_task, "ws_push", 6144, NULL, 4, &s_task) != pdPASS) {
            s_running = false;
            ESP_LOGE(TAG, "failed to start the broadcast task");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "WebSocket endpoint ready at /ws");
    return ESP_OK;
}

esp_err_t ws_server_stop(void)
{
    s_running = false;
    if (s_task) {
        vTaskDelay(pdMS_TO_TICKS(WS_BROADCAST_MS + 100));
    }
    free(s_last_state);
    s_last_state = NULL;
    s_server = NULL;
    return ESP_OK;
}
