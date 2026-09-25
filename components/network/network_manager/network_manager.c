/**
 * @file network_manager.c
 * @brief Network Manager - Based on official ESP-IDF station example
 */

#include "network_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

static const char *TAG = "network_mgr";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define EXAMPLE_ESP_MAXIMUM_RETRY  5

static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_num = 0;
static char s_ssid[32] = {0};
static char s_password[64] = {0};
static network_state_callback_t g_callback = NULL;
static void *g_callback_user_data = NULL;

static void event_handler(void* arg, esp_event_base_t event_base,
                         int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "STA_START, connecting to %s", s_ssid);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = (wifi_event_sta_disconnected_t*)event_data;
        ESP_LOGI(TAG, "DISCONNECTED, reason=%d", ev->reason);
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP (attempt %d)", s_retry_num);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "GOT IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

int network_manager_init(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

    ESP_LOGI(TAG, "Network manager initialized");
    return 0;
}

int network_connect(const char *ssid, const char *password) {
    if (ssid == NULL || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "Invalid SSID");
        return -1;
    }

    strncpy(s_ssid, ssid, sizeof(s_ssid) - 1);
    s_ssid[sizeof(s_ssid) - 1] = '\0';
    if (password) {
        strncpy(s_password, password, sizeof(s_password) - 1);
        s_password[sizeof(s_password) - 1] = '\0';
    }

    ESP_LOGI(TAG, "network_connect: SSID=%s", ssid);

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "",
            .password = "",
            .channel = 0,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }

    ESP_LOGI(TAG, "Setting WiFi mode to STA");
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(err));
        return -2;
    }

    ESP_LOGI(TAG, "Setting WiFi config");
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
        return -3;
    }
    
    ESP_LOGI(TAG, "Starting WiFi...");
    err = esp_wifi_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        return -4;
    }

    ESP_LOGI(TAG, "WiFi started");

    return 0;
}

int network_wait_connected(uint32_t timeout_ms) {
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            timeout_ms / portTICK_PERIOD_MS);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "WiFi connected to %s", s_ssid);
        
        // Print network info
        esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta_netif) {
            esp_netif_ip_info_t ip_info;
            esp_netif_get_ip_info(sta_netif, &ip_info);
            ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&ip_info.ip));
            ESP_LOGI(TAG, "GW: " IPSTR, IP2STR(&ip_info.gw));
            ESP_LOGI(TAG, "NM: " IPSTR, IP2STR(&ip_info.netmask));
        }
        
        if (g_callback) {
            g_callback(NETWORK_STATE_CONNECTED, g_callback_user_data);
        }
        return 0;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to %s", s_ssid);
        if (g_callback) {
            g_callback(NETWORK_STATE_FAILED, g_callback_user_data);
        }
        return -1;
    }
    return -2;
}

bool network_is_connected(void) {
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

int network_disconnect(void) {
    return esp_wifi_disconnect() == ESP_OK ? 0 : -1;
}

int network_register_callback(network_state_callback_t callback, void *user_data) {
    g_callback = callback;
    g_callback_user_data = user_data;
    return 0;
}

network_state_t network_get_state(void) {
    EventBits_t bits = xEventGroupGetBits(s_wifi_event_group);
    if (bits & WIFI_CONNECTED_BIT) return NETWORK_STATE_CONNECTED;
    if (bits & WIFI_FAIL_BIT) return NETWORK_STATE_FAILED;
    return NETWORK_STATE_IDLE;
}
