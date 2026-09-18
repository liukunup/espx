/**
 * @file network_manager.c
 * @brief Network Manager implementation
 */

#include "network_manager.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "network_mgr";

/** @brief WiFi event bits */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/** @brief Event group for WiFi status */
static EventGroupHandle_t g_wifi_event_group = NULL;

/** @brief Current network state */
static volatile network_state_t g_network_state = NETWORK_STATE_IDLE;

/** @brief Current SSID (for reconnection) */
static char g_ssid[32] = {0};

/** @brief Current password */
static char g_password[64] = {0};

/** @brief Callback list */
static network_state_callback_t g_callback = NULL;
static void *g_callback_user_data = NULL;

/** @brief Reconnect policy */
static reconnect_policy_t g_reconnect_policy = DEFAULT_RECONNECT_POLICY;

/** @brief Retry counter */
static uint32_t g_retry_count = 0;

/** @brief Reconnect task handle */
static TaskHandle_t g_reconnect_task_handle = NULL;

/** @brief Mutex for thread safety */
static SemaphoreHandle_t g_mutex = NULL;

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi started, connecting to SSID: %s", g_ssid);
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t*)event_data;
        ESP_LOGW(TAG, "WiFi disconnected: reason=%d", event->reason);

        if (g_network_state == NETWORK_STATE_CONNECTING ||
            g_network_state == NETWORK_STATE_CONNECTED) {
            g_network_state = NETWORK_STATE_DISCONNECTED;
            xEventGroupSetBits(g_wifi_event_group, WIFI_FAIL_BIT);

            // Notify callback
            if (g_callback) {
                g_callback(g_network_state, g_callback_user_data);
            }

            // Start reconnect if policy allows
            if (g_reconnect_policy.max_retries == 0 ||
                g_retry_count < g_reconnect_policy.max_retries) {
                g_retry_count++;
                ESP_LOGI(TAG, "Scheduling reconnect, attempt %lu", g_retry_count);
                start_reconnect_task(1000);  // Start after 1 second
            } else {
                ESP_LOGE(TAG, "Max retries reached");
                g_network_state = NETWORK_STATE_FAILED;
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        g_retry_count = 0;
        g_network_state = NETWORK_STATE_CONNECTED;
        xEventGroupSetBits(g_wifi_event_group, WIFI_CONNECTED_BIT);

        // Notify callback
        if (g_callback) {
            g_callback(g_network_state, g_callback_user_data);
        }
    }
}

/**
 * @brief Start reconnect task with delay
 */
static void start_reconnect_task(uint32_t delay_ms) {
    if (g_reconnect_task_handle != NULL) {
        vTaskDelete(g_reconnect_task_handle);
        g_reconnect_task_handle = NULL;
    }

    uint32_t *delay = malloc(sizeof(uint32_t));
    *delay = delay_ms;
    xTaskCreatePinnedToCore(reconnect_task, "wifi_reconnect", 4096, delay, 3, &g_reconnect_task_handle, 0);
}

/**
 * @brief Reconnect task
 */
static void reconnect_task(void *params) {
    uint32_t delay_ms = *(uint32_t*)params;
    free(params);

    // Calculate exponential backoff delay
    uint32_t delay = delay_ms;
    if (g_retry_count > 1) {
        delay = g_reconnect_policy.base_delay_ms;
        for (uint32_t i = 1; i < g_retry_count && i < 10; i++) {
            delay *= g_reconnect_policy.backoff_factor;
            if (delay > g_reconnect_policy.max_delay_ms) {
                delay = g_reconnect_policy.max_delay_ms;
                break;
            }
        }
        // Add jitter (0-25% of delay)
        delay += (esp_random() % (delay / 4));
    }

    ESP_LOGI(TAG, "Reconnecting in %lu ms...", delay);
    vTaskDelay(pdMS_TO_TICKS(delay));

    if (strlen(g_ssid) > 0) {
        g_network_state = NETWORK_STATE_CONNECTING;
        ESP_LOGI(TAG, "Attempting reconnection...");
        esp_wifi_connect();
    }

    g_reconnect_task_handle = NULL;
    vTaskDelete(NULL);
}

int network_manager_init(void) {
    if (g_wifi_event_group != NULL) {
        ESP_LOGW(TAG, "Network manager already initialized");
        return 0;
    }

    // Create event group
    g_wifi_event_group = xEventGroupCreate();
    if (g_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return -1;
    }

    // Create mutex
    g_mutex = xSemaphoreCreateMutex();
    if (g_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        vEventGroupDelete(g_wifi_event_group);
        return -2;
    }

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());

    // Create default event loop
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create WiFi station
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    if (sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create WiFi sta");
        return -3;
    }

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Register event handler
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    g_network_state = NETWORK_STATE_IDLE;
    ESP_LOGI(TAG, "Network manager initialized");
    return 0;
}

int network_connect(const char *ssid, const char *password) {
    if (ssid == NULL || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "Invalid SSID");
        return -1;
    }

    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -2;
    }

    // Stop any existing reconnect task
    if (g_reconnect_task_handle != NULL) {
        vTaskDelete(g_reconnect_task_handle);
        g_reconnect_task_handle = NULL;
    }

    // Store credentials for reconnection
    strncpy(g_ssid, ssid, sizeof(g_ssid) - 1);
    g_ssid[sizeof(g_ssid) - 1] = '\0';
    if (password) {
        strncpy(g_password, password, sizeof(g_password) - 1);
        g_password[sizeof(g_password) - 1] = '\0';
    } else {
        g_password[0] = '\0';
    }

    // Configure WiFi
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    wifi_config.sta.threshold.authmode = password ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_config.sta.pmf_cfg.required = true;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Reset retry count
    g_retry_count = 0;

    // Start WiFi
    g_network_state = NETWORK_STATE_CONNECTING;
    ESP_ERROR_CHECK(esp_wifi_start());

    xSemaphoreGive(g_mutex);

    // Notify callback
    if (g_callback) {
        g_callback(g_network_state, g_callback_user_data);
    }

    ESP_LOGI(TAG, "WiFi connection initiated");
    return 0;
}

int network_disconnect(void) {
    if (xSemaphoreTake(g_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -1;
    }

    // Stop any existing reconnect task
    if (g_reconnect_task_handle != NULL) {
        vTaskDelete(g_reconnect_task_handle);
        g_reconnect_task_handle = NULL;
    }

    g_network_state = NETWORK_STATE_DISCONNECTED;
    esp_wifi_disconnect();

    xSemaphoreGive(g_mutex);

    // Notify callback
    if (g_callback) {
        g_callback(g_network_state, g_callback_user_data);
    }

    return 0;
}

int network_reconnect(void) {
    if (strlen(g_ssid) == 0) {
        ESP_LOGE(TAG, "No previous SSID to reconnect to");
        return -1;
    }

    g_retry_count = 0;
    return network_connect(g_ssid, strlen(g_password) > 0 ? g_password : NULL);
}

network_state_t network_get_state(void) {
    return g_network_state;
}

int network_get_rssi(void) {
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    }
    return -127;  // Invalid RSSI
}

int network_get_ssid(char *buffer, size_t len) {
    if (buffer == NULL || len == 0) {
        return -1;
    }

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        strncpy(buffer, (char*)ap_info.ssid, len - 1);
        buffer[len - 1] = '\0';
        return 0;
    }
    return -2;
}

int network_register_callback(network_state_callback_t callback, void *user_data) {
    g_callback = callback;
    g_callback_user_data = user_data;
    return 0;
}

bool network_is_connected(void) {
    return g_network_state == NETWORK_STATE_CONNECTED;
}

uint32_t network_get_retry_count(void) {
    return g_retry_count;
}
