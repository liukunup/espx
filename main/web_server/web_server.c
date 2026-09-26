/**
 * @file web_server.c
 * @brief ESPX HTTPS Web Server implementation
 *
 * Uses mbedtls self-signed certificate.
 */

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_https_server.h>
#include <esp_http_server.h>
#include <cJSON.h>

#include "web_server.h"
#include "node_config.h"
#include "device_manager.h"
#include "device_type.h"
#include "config_apply.h"
#include "ota_service/ota_service.h"
#include "cert_manager/cert_manager.h"
#include "test_mode/test_mode.h"

static const char *TAG = "web_server";

static httpd_handle_t g_server = NULL;

// Embedded web UI (see main/web_server/web_files/index.html)
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

/**
 * @brief Root handler - serve embedded UI
 */
static esp_err_t root_handler(httpd_req_t *req)
{
    size_t len = index_html_end - index_html_start;
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)index_html_start, len);
}

/**
 * @brief Helper: send JSON response
 */
static esp_err_t send_json(httpd_req_t *req, cJSON *json, int status)
{
    char *str = cJSON_PrintUnformatted(json);
    if (str == NULL) return ESP_FAIL;

    const char *status_str = (status == 200) ? "200 OK" :
                             (status == 400) ? "400 Bad Request" :
                             (status == 404) ? "404 Not Found" : "500 Internal Server Error";

    httpd_resp_set_status(req, status_str);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, str, strlen(str));
    free(str);
    cJSON_Delete(json);
    return ESP_OK;
}

/**
 * @brief Helper: send error response
 */
static esp_err_t send_error(httpd_req_t *req, const char *msg, int status)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "error", msg);
    return send_json(req, json, status);
}

/**
 * @brief GET/PUT /api/node - node info / identity
 */
static esp_err_t api_node_handler(httpd_req_t *req)
{
    if (req->method == HTTP_PUT) {
        char buf[256];
        int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (len <= 0) return send_error(req, "Empty body", 400);
        buf[len] = '\0';

        cJSON *incoming = cJSON_Parse(buf);
        if (incoming == NULL) return send_error(req, "Invalid JSON", 400);

        cJSON *cfg = node_config_get();
        if (cfg == NULL) cfg = cJSON_CreateObject();

        cJSON *node = cJSON_GetObjectItem(cfg, "node");
        if (!cJSON_IsObject(node)) {
            node = cJSON_AddObjectToObject(cfg, "node");
        }

        const char *keys[] = { "name", "device_id" };
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
            cJSON *v = cJSON_GetObjectItem(incoming, keys[i]);
            if (cJSON_IsString(v) && v->valuestring[0] != '\0') {
                cJSON_ReplaceItemInObject(node, keys[i], cJSON_CreateString(v->valuestring));
            }
        }

        esp_err_t err = node_config_set(cfg);
        cJSON_Delete(cfg);
        cJSON_Delete(incoming);

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
        return send_json(req, resp, err == ESP_OK ? 200 : 500);
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "device_id", node_config_get_device_id());
    cJSON_AddStringToObject(json, "name", node_config_get_name());
    cJSON_AddStringToObject(json, "version", CONFIG_FIRMWARE_VERSION);
    cJSON_AddNumberToObject(json, "device_count", device_get_count());

    return send_json(req, json, 200);
}

/**
 * @brief GET /api/devices - list devices with values
 */
static esp_err_t api_devices_list_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateArray();

    for (size_t i = 0; i < device_get_count(); i++) {
        const device_t *dev = device_get_by_index(i);
        if (dev == NULL) continue;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", dev->id);
        cJSON_AddStringToObject(item, "type", dev->type->name);
        cJSON_AddStringToObject(item, "description", dev->type->description);
        cJSON_AddBoolToObject(item, "enabled", dev->enabled);
        cJSON_AddBoolToObject(item, "initialized", dev->initialized);

        if (dev->config != NULL) {
            cJSON_AddItemToObject(item, "config", cJSON_Duplicate((cJSON *)dev->config, true));
        }

        /* Current value (only for live, readable devices) */
        if (dev->enabled && dev->initialized && dev->type->read) {
            cJSON *value = cJSON_CreateObject();
            if (dev->type->read((device_t *)dev, value) == ESP_OK) {
                cJSON_AddItemToObject(item, "value", value);
            } else {
                cJSON_Delete(value);
            }
        }

        cJSON_AddItemToArray(json, item);
    }

    return send_json(req, json, 200);
}

/**
 * @brief POST /api/devices - add device
 */
static esp_err_t api_device_add_handler(httpd_req_t *req)
{
    char buf[1024];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return send_error(req, "Empty body", 400);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) return send_error(req, "Invalid JSON", 400);

    cJSON *id = cJSON_GetObjectItem(root, "id");
    cJSON *type = cJSON_GetObjectItem(root, "type");
    cJSON *config = cJSON_GetObjectItem(root, "config");

    if (!cJSON_IsString(id) || !cJSON_IsString(type)) {
        cJSON_Delete(root);
        return send_error(req, "Missing id or type", 400);
    }

    esp_err_t err = device_add(id->valuestring, type->valuestring, config);
    if (err == ESP_OK) {
        device_manager_save();
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
    cJSON_AddStringToObject(resp, "error", err != ESP_OK ? esp_err_to_name(err) : "");
    int status = (err == ESP_OK) ? 200 : 400;
    cJSON_Delete(root);
    return send_json(req, resp, status);
}

/**
 * @brief Helper: parse device id and action from /api/devices/{id}[/{action}]
 *
 * @param uri      Request URI
 * @param id_out   Output buffer for device id
 * @param id_size  Size of id_out
 * @param action_out Output buffer for action (may be empty string)
 * @param action_size Size of action_out
 * @return true if URI matched
 */
static bool parse_device_uri(const char *uri, char *id_out, size_t id_size,
                             char *action_out, size_t action_size)
{
    const char *prefix = "/api/devices/";
    size_t prefix_len = strlen(prefix);

    if (strncmp(uri, prefix, prefix_len) != 0) {
        return false;
    }

    const char *rest = uri + prefix_len;
    const char *slash = strchr(rest, '/');

    if (slash == NULL) {
        strncpy(id_out, rest, id_size - 1);
        id_out[id_size - 1] = '\0';
        if (action_out && action_size > 0) action_out[0] = '\0';
    } else {
        size_t id_len = slash - rest;
        if (id_len >= id_size) id_len = id_size - 1;
        memcpy(id_out, rest, id_len);
        id_out[id_len] = '\0';

        if (action_out && action_size > 0) {
            strncpy(action_out, slash + 1, action_size - 1);
            action_out[action_size - 1] = '\0';
        }
    }

    return true;
}

/**
 * @brief Dispatcher for /api/devices/{id}[/action]
 *
 * GET    /api/devices/{id}          - get device info
 * DELETE /api/devices/{id}          - remove device
 * POST   /api/devices/{id}/read     - read value
 * POST   /api/devices/{id}/write    - write value
 * POST   /api/devices/{id}/enable   - enable/disable
 */
static esp_err_t api_device_dispatch_handler(httpd_req_t *req)
{
    char id[64];
    char action[32];

    if (!parse_device_uri(req->uri, id, sizeof(id), action, sizeof(action))) {
        return send_error(req, "Invalid URI", 400);
    }

    // ---- POST actions ----
    if (req->method == HTTP_POST) {
        if (strcmp(action, "read") == 0) {
            cJSON *json = cJSON_CreateObject();
            esp_err_t err = device_read(id, json);
            if (err != ESP_OK) {
                cJSON_Delete(json);
                return send_error(req, esp_err_to_name(err), 400);
            }
            return send_json(req, json, 200);
        }

        if (strcmp(action, "write") == 0) {
            char buf[512];
            int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
            if (len <= 0) return send_error(req, "Empty body", 400);
            buf[len] = '\0';

            cJSON *root = cJSON_Parse(buf);
            if (root == NULL) return send_error(req, "Invalid JSON", 400);

            esp_err_t err = device_write(id, root);
            cJSON_Delete(root);

            cJSON *resp = cJSON_CreateObject();
            cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
            if (err != ESP_OK) {
                cJSON_AddStringToObject(resp, "error", esp_err_to_name(err));
            }
            return send_json(req, resp, err == ESP_OK ? 200 : 400);
        }

        if (strcmp(action, "enable") == 0) {
            char buf[128];
            int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
            if (len <= 0) return send_error(req, "Empty body", 400);
            buf[len] = '\0';

            cJSON *root = cJSON_Parse(buf);
            bool enabled = cJSON_IsTrue(cJSON_GetObjectItem(root, "enabled"));
            cJSON_Delete(root);

            esp_err_t err = device_set_enabled(id, enabled);
            if (err == ESP_OK) {
                device_manager_save();
            }

            cJSON *resp = cJSON_CreateObject();
            cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
            return send_json(req, resp, err == ESP_OK ? 200 : 404);
        }

        // POST /api/devices/{id} with new config -> update config
        if (action[0] == '\0') {
            char buf[512];
            int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
            if (len <= 0) return send_error(req, "Empty body", 400);
            buf[len] = '\0';

            cJSON *root = cJSON_Parse(buf);
            if (root == NULL) return send_error(req, "Invalid JSON", 400);

            cJSON *config = cJSON_GetObjectItem(root, "config");
            esp_err_t err = device_update_config(id, config);
            if (err == ESP_OK) {
                device_manager_save();
            }
            cJSON_Delete(root);

            cJSON *resp = cJSON_CreateObject();
            cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
            return send_json(req, resp, err == ESP_OK ? 200 : 400);
        }

        return send_error(req, "Unknown action", 400);
    }

    // ---- GET /api/devices/{id} ----
    if (req->method == HTTP_GET) {
        cJSON *json = cJSON_CreateObject();
        esp_err_t err = device_get_json(id, json);
        if (err != ESP_OK) {
            cJSON_Delete(json);
            return send_error(req, "Device not found", 404);
        }
        return send_json(req, json, 200);
    }

    // ---- DELETE /api/devices/{id} ----
    if (req->method == HTTP_DELETE) {
        esp_err_t err = device_remove(id);
        if (err == ESP_OK) {
            device_manager_save();
        }
        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
        return send_json(req, resp, err == ESP_OK ? 200 : 404);
    }

    return send_error(req, "Method not allowed", 400);
}

/**
 * @brief POST /api/devices/reload - reload all devices
 */
static esp_err_t api_devices_reload_handler(httpd_req_t *req)
{
    esp_err_t err = device_manager_reload();
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
    return send_json(req, resp, err == ESP_OK ? 200 : 500);
}

/**
 * @brief GET /api/device-types - list device types
 */
static esp_err_t api_device_types_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateArray();

    for (size_t i = 0; i < device_type_count(); i++) {
        const device_type_t *t = device_type_get_by_index(i);
        if (t == NULL) continue;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", t->name);
        cJSON_AddStringToObject(item, "description", t->description);
        cJSON_AddNumberToObject(item, "capabilities", t->capabilities);

        cJSON *cfg = cJSON_CreateObject();
        if (t->get_default_config) {
            t->get_default_config(cfg);
        }
        cJSON_AddItemToObject(item, "default_config", cfg);

        cJSON_AddItemToArray(json, item);
    }

    return send_json(req, json, 200);
}

/**
 * @brief POST /api/config - apply a configuration document (YAML or JSON)
 *
 * Body: the document itself (Content-Type: text/yaml or application/json).
 * Same semantics as the MQTT {prefix}/cmd/config interface.
 */
static esp_err_t api_config_apply_handler(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 4096) {
        return send_error(req, "missing or oversized configuration body", 400);
    }

    char *buf = malloc(req->content_len + 1);
    if (buf == NULL) {
        return send_error(req, "out of memory", 500);
    }

    int received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, buf + received, req->content_len - received);
        if (r <= 0) {
            free(buf);
            return send_error(req, "failed to read body", 400);
        }
        received += r;
    }
    buf[received] = '\0';

    config_apply_result_t res;
    char err[128] = {0};
    esp_err_t rc = config_apply_payload(buf, &res, err, sizeof(err));
    free(buf);

    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", rc == ESP_OK && res.devices_failed == 0);

    cJSON *counts = cJSON_AddObjectToObject(out, "applied");
    cJSON_AddNumberToObject(counts, "added", res.devices_added);
    cJSON_AddNumberToObject(counts, "updated", res.devices_updated);
    cJSON_AddNumberToObject(counts, "removed", res.devices_removed);
    cJSON_AddNumberToObject(counts, "failed", res.devices_failed);

    cJSON_AddBoolToObject(out, "reboot_required", res.reboot_recommended);
    const char *msg = res.error[0] ? res.error : err;
    if (msg[0]) cJSON_AddStringToObject(out, "error", msg);

    return send_json(req, out, rc == ESP_OK ? 200 : 400);
}

/**
 * @brief GET /api/config - current configuration as JSON
 */
static esp_err_t api_config_get_handler(httpd_req_t *req)
{
    cJSON *cfg = config_export();
    if (cfg == NULL) {
        return send_error(req, "failed to export configuration", 500);
    }
    return send_json(req, cfg, 200);
}

/**
 * @brief GET /api/system/info - runtime info
 */
static esp_err_t api_system_info_handler(httpd_req_t *req)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "uptime", (double)(esp_timer_get_time() / 1000000ULL));
    cJSON_AddNumberToObject(json, "free_heap", (double)esp_get_free_heap_size());
    cJSON_AddNumberToObject(json, "min_free_heap", (double)esp_get_minimum_free_heap_size());

    /* Wi-Fi state + IP */
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        cJSON_AddStringToObject(json, "wifi_ssid", (const char *)ap.ssid);
        cJSON_AddNumberToObject(json, "wifi_rssi", ap.rssi);
    }

    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) {
        esp_netif_ip_info_t ip;
        if (esp_netif_get_ip_info(netif, &ip) == ESP_OK && ip.ip.addr != 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), IPSTR, IP2STR(&ip.ip));
            cJSON_AddStringToObject(json, "ip", buf);
        }
    }

    return send_json(req, json, 200);
}

/**
 * @brief GET/PUT /api/network - network (MQTT) configuration
 */
static esp_err_t api_network_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        cJSON *cfg = node_config_get();
        cJSON *net = cfg ? cJSON_GetObjectItem(cfg, "network") : NULL;
        cJSON *out = net ? cJSON_Duplicate(net, true) : cJSON_CreateObject();
        if (cfg) cJSON_Delete(cfg);
        return send_json(req, out, 200);
    }

    if (req->method == HTTP_PUT) {
        char buf[512];
        int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (len <= 0) return send_error(req, "Empty body", 400);
        buf[len] = '\0';

        cJSON *incoming = cJSON_Parse(buf);
        if (incoming == NULL) return send_error(req, "Invalid JSON", 400);

        cJSON *cfg = node_config_get();
        if (cfg == NULL) cfg = cJSON_CreateObject();

        cJSON *net = cJSON_GetObjectItem(cfg, "network");
        if (!cJSON_IsObject(net)) {
            net = cJSON_AddObjectToObject(cfg, "network");
        }

        cJSON *item;
        cJSON_ArrayForEach(item, incoming) {
            if (cJSON_IsString(item) || cJSON_IsNumber(item) || cJSON_IsBool(item)) {
                cJSON_ReplaceItemInObject(net, item->string,
                                          cJSON_Duplicate(item, true));
            }
        }

        esp_err_t err = node_config_set(cfg);
        cJSON_Delete(cfg);
        cJSON_Delete(incoming);

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
        return send_json(req, resp, err == ESP_OK ? 200 : 500);
    }

    return send_error(req, "Method not allowed", 400);
}

/**
 * @brief GET /api/ota/status - OTA status
 */
static esp_err_t api_ota_status_handler(httpd_req_t *req)
{
    ota_status_t st;
    ota_service_get_status(&st);

    static const char *names[] = {
        "IDLE", "CONNECTING", "DOWNLOADING", "VERIFYING",
        "APPLYING", "REBOOTING", "SUCCESS", "FAILED"
    };

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "state",
                            names[st.state <= OTA_STATE_FAILED ? st.state : OTA_STATE_FAILED]);
    cJSON_AddNumberToObject(json, "progress", st.progress);
    cJSON_AddNumberToObject(json, "bytes_read", st.bytes_read);
    cJSON_AddNumberToObject(json, "total_size", st.total_size);
    cJSON_AddStringToObject(json, "url", st.url);
    cJSON_AddStringToObject(json, "error", st.error);
    cJSON_AddStringToObject(json, "running_version", st.running_version);
    cJSON_AddBoolToObject(json, "running", ota_service_is_running());

    return send_json(req, json, 200);
}

/**
 * @brief POST /api/ota/start - start delta OTA
 * Body: {"url": "http://server/espx.patch"}  (url optional if stored)
 */
static esp_err_t api_ota_start_handler(httpd_req_t *req)
{
    char buf[384];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return send_error(req, "Empty body", 400);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) return send_error(req, "Invalid JSON", 400);

    cJSON *jurl = cJSON_GetObjectItem(root, "url");
    if (!cJSON_IsString(jurl) || jurl->valuestring[0] == '\0') {
        cJSON_Delete(root);
        return send_error(req, "Missing 'url'", 400);
    }

    esp_err_t err = ota_service_start(jurl->valuestring);
    cJSON_Delete(root);

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "started", err == ESP_OK);
    if (err != ESP_OK) {
        cJSON_AddStringToObject(resp, "error", esp_err_to_name(err));
    }
    return send_json(req, resp, err == ESP_OK ? 200 : 400);
}

/**
 * @brief POST /api/ota/cancel - cancel OTA
 */
static esp_err_t api_ota_cancel_handler(httpd_req_t *req)
{
    esp_err_t err = ota_service_cancel();
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "cancelled", err == ESP_OK);
    return send_json(req, resp, 200);
}

/**
 * @brief GET /api/certs/info - certificate info
 */
static esp_err_t api_certs_info_handler(httpd_req_t *req)
{
    char info[256];
    cert_manager_get_info(info, sizeof(info));
    cJSON *json = cJSON_Parse(info);
    if (json == NULL) {
        return send_error(req, "Failed to build cert info", 500);
    }
    return send_json(req, json, 200);
}

/**
 * @brief POST /api/system/testmode - reboot into manufacturing test mode
 */
static esp_err_t api_system_testmode_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Test mode requested via web");

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "rebooting_into_test_mode", true);
    send_json(req, resp, 200);

    vTaskDelay(pdMS_TO_TICKS(200));
    test_mode_request();
    esp_restart();
    return ESP_OK;
}

/**
 * @brief POST /api/system/reboot
 */
static esp_err_t api_system_reboot_handler(httpd_req_t *req)
{
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    send_json(req, resp, 200);

    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (g_server != NULL) {
        return ESP_OK;
    }

    // Get embedded certificate from cert_manager
    server_cert_t server_cert;
    esp_err_t err = cert_manager_get_server_cert(&server_cert);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get certificate: %s", esp_err_to_name(err));
        return err;
    }

    httpd_ssl_config_t config = HTTPD_SSL_CONFIG_DEFAULT();
    config.servercert = (uint8_t *)server_cert.cert_pem;
    config.servercert_len = server_cert.cert_len;
    config.prvtkey_pem = (uint8_t *)server_cert.key_pem;
    config.prvtkey_len = server_cert.key_len;
    config.httpd.max_uri_handlers = 24;

    ESP_LOGI(TAG, "Starting HTTPS server...");
        err = httpd_ssl_start(&g_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTPS: %s", esp_err_to_name(err));
        cert_manager_free_cert(&server_cert);
        return err;
    }

    // Register handlers - ORDER MATTERS: exact paths before wildcards
    const httpd_uri_t uris[] = {
        { .uri = "/",                   .method = HTTP_GET,    .handler = root_handler },
        { .uri = "/api/node",           .method = HTTP_GET,    .handler = api_node_handler },
        { .uri = "/api/node",           .method = HTTP_PUT,    .handler = api_node_handler },
        { .uri = "/api/system/info",    .method = HTTP_GET,    .handler = api_system_info_handler },
        { .uri = "/api/network",        .method = HTTP_GET,    .handler = api_network_handler },
        { .uri = "/api/network",        .method = HTTP_PUT,    .handler = api_network_handler },
        { .uri = "/api/devices",        .method = HTTP_GET,    .handler = api_devices_list_handler },
        { .uri = "/api/devices",        .method = HTTP_POST,   .handler = api_device_add_handler },
        { .uri = "/api/devices/reload", .method = HTTP_POST,   .handler = api_devices_reload_handler },
        { .uri = "/api/device-types",   .method = HTTP_GET,    .handler = api_device_types_handler },
        { .uri = "/api/config",         .method = HTTP_GET,    .handler = api_config_get_handler },
        { .uri = "/api/config",         .method = HTTP_POST,   .handler = api_config_apply_handler },
        { .uri = "/api/ota/status",     .method = HTTP_GET,    .handler = api_ota_status_handler },
        { .uri = "/api/ota/start",      .method = HTTP_POST,   .handler = api_ota_start_handler },
        { .uri = "/api/ota/cancel",     .method = HTTP_POST,   .handler = api_ota_cancel_handler },
        { .uri = "/api/certs/info",     .method = HTTP_GET,    .handler = api_certs_info_handler },
        { .uri = "/api/system/reboot",  .method = HTTP_POST,   .handler = api_system_reboot_handler },
        { .uri = "/api/system/testmode",.method = HTTP_POST,   .handler = api_system_testmode_handler },
        // Wildcard dispatcher must be last
        { .uri = "/api/devices/*",      .method = HTTP_GET,    .handler = api_device_dispatch_handler },
        { .uri = "/api/devices/*",      .method = HTTP_POST,   .handler = api_device_dispatch_handler },
        { .uri = "/api/devices/*",      .method = HTTP_DELETE, .handler = api_device_dispatch_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        if (httpd_register_uri_handler(g_server, &uris[i]) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s", uris[i].uri);
        }
    }

    free(server_cert.cert_pem);
    free(server_cert.key_pem);

    ESP_LOGI(TAG, "HTTPS server started on port 443");
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (g_server == NULL) {
        return ESP_OK;
    }
    esp_err_t err = httpd_ssl_stop(g_server);
    g_server = NULL;
    return err;
}

bool web_server_is_running(void)
{
    return g_server != NULL;
}
