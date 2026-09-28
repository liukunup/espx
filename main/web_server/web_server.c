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
#include "handlers/handlers.h"

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
    return api_send_json(req, json, status);
}

/**
 * @brief Helper: send error response
 */
static esp_err_t send_error(httpd_req_t *req, const char *msg, int status)
{
    return api_send_error(req, msg, status);
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
        { .uri = "/api/system/info",    .method = HTTP_GET,    .handler = api_system_info_handler },
        { .uri = "/api/ota/status",     .method = HTTP_GET,    .handler = api_ota_status_handler },
        { .uri = "/api/ota/start",      .method = HTTP_POST,   .handler = api_ota_start_handler },
        { .uri = "/api/ota/cancel",     .method = HTTP_POST,   .handler = api_ota_cancel_handler },
        { .uri = "/api/certs/info",     .method = HTTP_GET,    .handler = api_certs_info_handler },
        { .uri = "/api/system/reboot",  .method = HTTP_POST,   .handler = api_system_reboot_handler },
        { .uri = "/api/system/testmode",.method = HTTP_POST,   .handler = api_system_testmode_handler },
        { .uri = "/api/wifi/scan",       .method = HTTP_POST,   .handler = api_wifi_scan_handler },
        { .uri = "/api/wifi/config",     .method = HTTP_PUT,    .handler = api_wifi_config_handler },
    };

    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        if (httpd_register_uri_handler(g_server, &uris[i]) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register %s", uris[i].uri);
        }
    }

    /* Migrated modules register themselves. The device module owns the
     * /api/peripherals wildcard routes and must register last. */
    node_handler_register(g_server);
    network_handler_register(g_server);
    config_handler_register(g_server);

    /* LAST: owns the /api/peripherals wildcard routes */
    device_handler_register(g_server);

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
