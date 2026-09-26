/* Application Module

   This example code is in the Public Domain (or CC0 licensed, at your option.)
*/

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "app.h"
#include "wifi_prov/wifi_prov.h"
#include "mqtt_client/mqtt_client.h"
#include "ota_service/ota_service.h"

static const char *TAG = "app";

/**
 * @brief Heartbeat task - publishes device status periodically
 */
static void heartbeat_task(void *arg)
{
    char topic[128];
    char payload[256];
    const char *device_id = "test-device";  // Should come from param_store

    while (1) {
        // Publish heartbeat every 60 seconds
        vTaskDelay(pdMS_TO_TICKS(60000));

        if (mqtt_client_is_connected()) {
            snprintf(topic, sizeof(topic), "%s/status/heartbeat", "espx");
            snprintf(payload, sizeof(payload), 
                     "{\"uptime\": %lu, \"free_heap\": %u, \"wifi_rssi\": %d}",
                     esp_timer_get_time() / 1000000,
                     esp_get_free_heap_size(),
                     0);  // Would get actual RSSI

            mqtt_client_publish(topic, payload, strlen(payload), 1, false);
        }
    }
}

/**
 * @brief OTA check task - periodically checks for updates
 */
static void ota_check_task(void *arg)
{
    while (1) {
        // Check for OTA updates every hour
        vTaskDelay(pdMS_TO_TICKS(3600000));

        ESP_LOGI(TAG, "Checking for OTA updates...");
        if (ota_service_check() == ESP_OK) {
            ESP_LOGI(TAG, "OTA update available!");
            // Could trigger update automatically or notify via MQTT
        }
    }
}

/* ============================================
 * Public API Implementation
 * ============================================ */

esp_err_t app_init(void)
{
    // Wi-Fi provisioning is now handled in app_main.c
    // This function is kept for compatibility
    return ESP_OK;
}

void app_loop(void)
{
    // Create background tasks
    xTaskCreate(heartbeat_task, "heartbeat", 4096, NULL, 3, NULL);
    xTaskCreate(ota_check_task, "ota_check", 4096, NULL, 2, NULL);

    // Main loop - just idle
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGD(TAG, "Main loop idle...");
    }
}
