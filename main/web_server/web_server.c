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
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_https_server.h>
#include <esp_http_server.h>
#include <nvs_flash.h>
#include <cJSON.h>

#include "app_info.h"
#include "web_server.h"
#include "node_config.h"
#include "device_manager.h"
#include "device_type.h"
#include "config_apply.h"
#include "ota_service/ota_service.h"
#include "cert_manager/cert_manager.h"
#include "test_mode/test_mode.h"
#include "ws_server.h"
#include "net_services/time_sync.h"
#include "net_services/mdns_service.h"
#include "sys_stats.h"
#include "sys_info.h"
#include "str_utils.h"

static const char *TAG = "web_server";

static httpd_handle_t g_server = NULL;

/**
 * @brief Upsert a string key into a JSON object.
 *
 * cJSON_ReplaceItemInObject() silently ignores keys that do not already
 * exist, which loses new settings (e.g. a first-time wifi_ssid). Delete
 * first so both insert and update work.
 */
static void json_set_string(cJSON *obj, const char *key, const char *value)
{
    if (obj == NULL || key == NULL || value == NULL) return;
    cJSON_DeleteItemFromObject(obj, key);
    cJSON_AddStringToObject(obj, key, value);
}

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

        const char *keys[] = { "name" };
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
            cJSON *v = cJSON_GetObjectItem(incoming, keys[i]);
            if (cJSON_IsString(v) && v->valuestring[0] != '\0') {
                json_set_string(node, keys[i], v->valuestring);
            }
        }
        /* device_id is intentionally ignored here — it is set at the factory
         * and cannot be changed at runtime. Any incoming value is discarded. */

        esp_err_t err = node_config_set(cfg);
        cJSON_Delete(cfg);
        cJSON_Delete(incoming);

        cJSON *resp = cJSON_CreateObject();
        cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
        return send_json(req, resp, err == ESP_OK ? 200 : 500);
    }

    cJSON *json = cJSON_CreateObject();
    {
        const char *id = node_config_get_device_id();
        if (strncmp(id, "espx-", 5) == 0) {
            id += 5;
        }
        cJSON_AddStringToObject(json, "device_id", id);
    }
    cJSON_AddStringToObject(json, "name", node_config_get_name());
    cJSON_AddStringToObject(json, "version", app_version());
    cJSON_AddNumberToObject(json, "peripheral_count", device_get_count());

    return send_json(req, json, 200);
}

/**
 * @brief GET /api/peripherals - list devices with values
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
 * @brief POST /api/peripherals - add device
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
 * @brief Helper: parse device id and action from /api/peripherals/{id}[/{action}]
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
    const char *prefix = "/api/peripherals/";
    size_t prefix_len = strlen(prefix);

    if (strncmp(uri, prefix, prefix_len) != 0) {
        return false;
    }

    const char *rest = uri + prefix_len;
    const char *slash = strchr(rest, '/');

    if (slash == NULL) {
        str_copy(id_out, id_size, rest);
        if (action_out && action_size > 0) action_out[0] = '\0';
    } else {
        size_t id_len = slash - rest;
        if (id_len >= id_size) id_len = id_size - 1;
        memcpy(id_out, rest, id_len);
        id_out[id_len] = '\0';

        if (action_out && action_size > 0) {
            str_copy(action_out, action_size, slash + 1);
        }
    }

    return true;
}

/**
 * @brief Dispatcher for /api/peripherals/{id}[/action]
 *
 * GET    /api/peripherals/{id}          - get device info
 * DELETE /api/peripherals/{id}          - remove device
 * POST   /api/peripherals/{id}/read     - read value
 * POST   /api/peripherals/{id}/write    - write value
 * POST   /api/peripherals/{id}/enable   - enable/disable
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

        // POST /api/peripherals/{id} with new config -> update config
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

    // ---- GET /api/peripherals/{id} ----
    if (req->method == HTTP_GET) {
        cJSON *json = cJSON_CreateObject();
        esp_err_t err = device_get_json(id, json);
        if (err != ESP_OK) {
            cJSON_Delete(json);
            return send_error(req, "Device not found", 404);
        }
        return send_json(req, json, 200);
    }

    // ---- DELETE /api/peripherals/{id} ----
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
 * @brief POST /api/peripherals/reload - reload all devices
 */
static esp_err_t api_devices_reload_handler(httpd_req_t *req)
{
    esp_err_t err = device_manager_reload();
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "success", err == ESP_OK);
    return send_json(req, resp, err == ESP_OK ? 200 : 500);
}

/**
 * @brief GET /api/peripheral/options - list device types
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
        cJSON_AddStringToObject(item, "description_zh", t->description_zh);
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
    cJSON *json = sys_info_build();
    if (json == NULL) {
        return send_error(req, "failed to build system info", 500);
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

        cJSON *item = incoming->child;
        while (item != NULL) {
            if (cJSON_IsString(item)) {
                json_set_string(net, item->string, item->valuestring);
            } else if (cJSON_IsNumber(item) || cJSON_IsBool(item)) {
                cJSON *dup = cJSON_Duplicate(item, true);
                cJSON_DeleteItemFromObject(net, item->string);
                cJSON_AddItemToObject(net, item->string, dup);
            }
            /* Never log secrets */
            if (strstr(item->string, "password") != NULL) {
                ESP_LOGI(TAG, "Network config: %s = <set>", item->string);
            } else {
                ESP_LOGI(TAG, "Network config: %s = %s", item->string,
                         cJSON_IsString(item) ? item->valuestring : "<non-string>");
            }
            item = item->next;
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
 * @brief Helper: convert auth mode to string
 */
static const char* wifi_auth_mode_str(wifi_auth_mode_t mode)
{
    switch (mode) {
        case WIFI_AUTH_OPEN:            return "Open";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA-PSK";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2-PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2-PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3-PSK";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3-PSK";
        default:                        return "Unknown";
    }
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

/**
 * @brief POST /api/wifi/scan - scan for Wi-Fi networks
 */
static esp_err_t api_wifi_scan_handler(httpd_req_t *req)
{
    wifi_scan_config_t scan_config = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
        return send_error(req, "Scan failed", 500);
    }

    uint16_t ap_num = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_num));

    if (ap_num == 0) {
        cJSON *json = cJSON_CreateObject();
        cJSON_AddArrayToObject(json, "networks");
        return send_json(req, json, 200);
    }

    if (ap_num > 32) ap_num = 32;
    wifi_ap_record_t *ap_info = malloc(sizeof(wifi_ap_record_t) * ap_num);
    if (ap_info == NULL) {
        return send_error(req, "Out of memory", 500);
    }

    uint16_t count = ap_num;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&count, ap_info));

    cJSON *json = cJSON_CreateObject();
    cJSON *networks = cJSON_AddArrayToObject(json, "networks");

    for (int i = 0; i < count; i++) {
        cJSON *net = cJSON_CreateObject();
        cJSON_AddStringToObject(net, "ssid", (const char *)ap_info[i].ssid);
        char bssid[18];
        mac_to_str((const uint8_t *)&ap_info[i].bssid, bssid, sizeof(bssid));
        cJSON_AddStringToObject(net, "bssid", bssid);
        cJSON_AddNumberToObject(net, "rssi", ap_info[i].rssi);
        cJSON_AddStringToObject(net, "auth", wifi_auth_mode_str(ap_info[i].authmode));
        cJSON_AddItemToArray(networks, net);
    }

    free(ap_info);
    return send_json(req, json, 200);
}

/**
 * @brief PUT /api/wifi/config - save Wi-Fi credentials and reboot
 */
static esp_err_t api_wifi_config_handler(httpd_req_t *req)
{
    char buf[256];
    int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (len <= 0) return send_error(req, "Empty body", 400);
    buf[len] = '\0';

    cJSON *incoming = cJSON_Parse(buf);
    if (incoming == NULL) return send_error(req, "Invalid JSON", 400);

    cJSON *ssid = cJSON_GetObjectItem(incoming, "ssid");
    cJSON *password = cJSON_GetObjectItem(incoming, "password");

    if (!cJSON_IsString(ssid) || strlen(ssid->valuestring) == 0) {
        cJSON_Delete(incoming);
        return send_error(req, "SSID required", 400);
    }

    /* node_config is the AUTHORITATIVE source for Wi-Fi credentials: on boot
     * wifi_prov reads network.wifi_ssid / wifi_password from it and calls
     * esp_wifi_set_config(). Writing any other NVS namespace would be ignored. */
    cJSON *cfg = node_config_get();
    if (cfg == NULL) cfg = cJSON_CreateObject();

    cJSON *net = cJSON_GetObjectItem(cfg, "network");
    if (!cJSON_IsObject(net)) {
        net = cJSON_AddObjectToObject(cfg, "network");
    }

    json_set_string(net, "wifi_ssid", ssid->valuestring);
    if (cJSON_IsString(password)) {
        json_set_string(net, "wifi_password", password->valuestring);
    } else {
        cJSON_DeleteItemFromObject(net, "wifi_password");
    }

    char saved_ssid[64];
    snprintf(saved_ssid, sizeof(saved_ssid), "%s", ssid->valuestring);

    esp_err_t err = node_config_set(cfg);
    cJSON_Delete(cfg);
    cJSON_Delete(incoming);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Wi-Fi config saved: %s", saved_ssid);
    } else {
        ESP_LOGE(TAG, "Failed to save Wi-Fi config: %s", esp_err_to_name(err));
    }

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", err == ESP_OK);
    esp_err_t resp_err = send_json(req, resp, err == ESP_OK ? 200 : 500);

    if (err == ESP_OK) {
        // Wait long enough for the HTTP response to flush, then reboot
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    return resp_err;
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
    config.httpd.max_uri_handlers = 32;

    /* Wildcards are NOT matched by default: with uri_match_fn == NULL the
     * server does a plain string compare, so a pattern ending in a star never
     * matches a real path such as "/api/peripherals/relay_a/read". Every such
     * request would 404 with the server's own "Nothing matches the given URI".
     * Selecting the wildcard matcher is what makes the dispatcher reachable. */
    config.httpd.uri_match_fn = httpd_uri_match_wildcard;
    /* 4 comfortably covers one browser (1 WebSocket + a couple of in-flight requests). */
    config.httpd.max_open_sockets = 4;
    /* Increase stack size for HTTPD task to prevent stack overflow */
    config.httpd.stack_size = 8192;

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
        { .uri = "/api/peripherals",        .method = HTTP_GET,    .handler = api_devices_list_handler },
        { .uri = "/api/peripherals",        .method = HTTP_POST,   .handler = api_device_add_handler },
        { .uri = "/api/peripherals/reload", .method = HTTP_POST,   .handler = api_devices_reload_handler },
        { .uri = "/api/peripheral/options",   .method = HTTP_GET,    .handler = api_device_types_handler },
        { .uri = "/api/config",         .method = HTTP_GET,    .handler = api_config_get_handler },
        { .uri = "/api/config",         .method = HTTP_POST,   .handler = api_config_apply_handler },
        { .uri = "/api/ota/status",     .method = HTTP_GET,    .handler = api_ota_status_handler },
        { .uri = "/api/ota/start",      .method = HTTP_POST,   .handler = api_ota_start_handler },
        { .uri = "/api/ota/cancel",     .method = HTTP_POST,   .handler = api_ota_cancel_handler },
        { .uri = "/api/certs/info",     .method = HTTP_GET,    .handler = api_certs_info_handler },
        { .uri = "/api/system/reboot",  .method = HTTP_POST,   .handler = api_system_reboot_handler },
        { .uri = "/api/system/testmode",.method = HTTP_POST,   .handler = api_system_testmode_handler },
        { .uri = "/api/wifi/scan",       .method = HTTP_POST,   .handler = api_wifi_scan_handler },
        { .uri = "/api/wifi/config",     .method = HTTP_PUT,    .handler = api_wifi_config_handler },
        // Wildcard dispatcher must be last
        { .uri = "/api/peripherals/*",      .method = HTTP_GET,    .handler = api_device_dispatch_handler },
        { .uri = "/api/peripherals/*",      .method = HTTP_POST,   .handler = api_device_dispatch_handler },
        { .uri = "/api/peripherals/*",      .method = HTTP_DELETE, .handler = api_device_dispatch_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        if (httpd_register_uri_handler(g_server, &uris[i]) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s", uris[i].uri);
        }
    }

#if CONFIG_ESPX_WS_ENABLE
    if (ws_server_start(g_server) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start the WebSocket endpoint");
    }
#endif

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
