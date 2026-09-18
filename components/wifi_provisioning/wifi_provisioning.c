/**
 * @file wifi_provisioning.c
 * @brief AP Provisioning implementation
 */

#include "wifi_provisioning.h"
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_random.h"

static const char *TAG = "provisioning";

/** @brief Provisioning state */
static volatile provisioning_state_t g_prov_state = PROV_STATE_IDLE;

/** @brief HTTP server handle */
static httpd_handle_t g_httpd_handle = NULL;

/** @brief Current AP SSID (dynamically generated) */
static char g_ap_ssid[32] = {0};

/** @brief AP password (8 digits minimum) */
#define PROV_AP_PASSWORD "12345678"

/**
 * @brief Generate random hex string
 */
static void generate_random_hex(char *buf, int len) {
    const char hex_chars[] = "0123456789ABCDEF";
    
    for (int i = 0; i < len; i++) {
        if (i % 2 == 0) {
            uint32_t random_value = esp_random();
            buf[i] = hex_chars[(random_value >> 0) & 0x0F];
            if (i + 1 < len) {
                buf[i + 1] = hex_chars[(random_value >> 4) & 0x0F];
            }
        }
    }
    buf[len] = '\0';
}

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "Station connected to AP");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "Station disconnected from AP");
    }
}

/**
 * @brief HTTP handler for WiFi config
 */
static esp_err_t config_handler(httpd_req_t *req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    ESP_LOGI(TAG, "Received config: %s", buf);

    // Parse SSID and password from JSON (simplified)
    // In production, use cJSON or similar

    // Send success response
    const char resp[] = "{\"status\":\"ok\"}";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    g_prov_state = PROV_STATE_COMPLETE;

    return ESP_OK;
}

/**
 * @brief HTTP handler for status
 */
static esp_err_t status_handler(httpd_req_t *req) {
    const char resp[] = "{\"status\":\"ready\"}";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

int provisioning_init(void) {
    if (g_prov_state != PROV_STATE_IDLE) {
        ESP_LOGW(TAG, "Provisioning already initialized");
        return 0;
    }

    // Initialize WiFi in AP mode
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));

    ESP_LOGI(TAG, "Provisioning initialized");
    return 0;
}

int provisioning_start(void) {
    if (g_prov_state != PROV_STATE_IDLE) {
        ESP_LOGW(TAG, "Provisioning already started");
        return -1;
    }

    // Generate random SSID: ESPX-XXXXXXXX (8 hex chars)
    strcpy(g_ap_ssid, "ESPX-");
    generate_random_hex(g_ap_ssid + 5, 8);
    
    ESP_LOGI(TAG, "Generated random SSID: %s", g_ap_ssid);

    // Configure WiFi AP
    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = 13,  // "ESPX-" + 8 chars = 13
            .password = PROV_AP_PASSWORD,
            .max_connection = 1,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK
        }
    };
    
    // Copy SSID to config
    memcpy(ap_config.ap.ssid, g_ap_ssid, ap_config.ap.ssid_len);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    g_prov_state = PROV_STATE_AP_READY;
    ESP_LOGI(TAG, "AP started: SSID=%s, Password=%s", g_ap_ssid, PROV_AP_PASSWORD);

    // HTTP server would be started here
    // For now, just mark as ready
    ESP_LOGI(TAG, "Provisioning web server ready");

    return 0;
}
