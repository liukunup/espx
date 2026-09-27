/**
 * @file mqtt_publisher.c
 * @brief MQTT auto-publisher
 *
 * Periodically publishes:
 *   - Heartbeat (state topic)
 *   - Sensor readings (sensors topic)
 *   - Node status: heap, RAM, CPU, clock, Wi-Fi (status topic)
 */

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>

#include "task_util.h"
#include "mqtt_publisher.h"
#include "espx_mqtt_client.h"
#include "node_config.h"
#include "device_manager.h"
#include "device_type.h"
#include "sys_info.h"

static const char *TAG = "mqtt_publisher";

#define HEARTBEAT_INTERVAL_S 30
#define SENSOR_INTERVAL_S    10
#define STATUS_INTERVAL_S    30

static TaskHandle_t g_task = NULL;
static volatile bool g_running = false;

static void publisher_task(void *arg)
{
    int64_t last_heartbeat = 0;
    int64_t last_sensor = 0;
    int64_t last_status = 0;

    while (g_running) {
        int64_t now = esp_timer_get_time();

        // Heartbeat
        if (now - last_heartbeat > HEARTBEAT_INTERVAL_S * 1000000LL) {
            if (mqtt_client_is_connected()) {
                /* Same shape as the connect-time announcement: a consumer must
                 * not have to handle two different schemas for one topic. */
                char payload[160];
                snprintf(payload, sizeof(payload),
                         "{\"online\":true,\"device_id\":\"%s\",\"uptime\":%lu}",
                         node_config_get_device_id(),
                         (unsigned long)(now / 1000000));
                mqtt_client_publish("state", payload, strlen(payload), 1, true);
            }
            last_heartbeat = now;
        }

        // Sensor data
        if (now - last_sensor > SENSOR_INTERVAL_S * 1000000LL) {
            if (mqtt_client_is_connected()) {
                cJSON *sensors = cJSON_CreateObject();

                for (size_t i = 0; i < device_get_count(); i++) {
                    const device_t *dev = device_get_by_index(i);
                    if (dev == NULL || !dev->enabled || !dev->initialized) continue;
                    if (!(dev->type->capabilities & DEVICE_CAPABILITY_PERIODIC)) continue;

                    cJSON *value = cJSON_CreateObject();
                    if (dev->type->read && dev->type->read((device_t *)dev, value) == ESP_OK) {
                        cJSON_AddItemToObject(sensors, dev->id, value);
                    } else {
                        cJSON_Delete(value);
                    }
                }

                char *json_str = cJSON_PrintUnformatted(sensors);
                if (json_str) {
                    mqtt_client_publish("sensors", json_str, strlen(json_str), 1, false);
                    free(json_str);
                }
                cJSON_Delete(sensors);
            }
            last_sensor = now;
        }

        // Node status: heap, RAM, CPU, clock, Wi-Fi. Retained so a dashboard
        // that connects between reports still sees the latest snapshot.
        if (now - last_status > STATUS_INTERVAL_S * 1000000LL) {
            if (mqtt_client_is_connected()) {
                cJSON *status = sys_info_build();
                if (status != NULL) {
                    char *json_str = cJSON_PrintUnformatted(status);
                    if (json_str) {
                        mqtt_client_publish("status", json_str, strlen(json_str), 1, true);
                        free(json_str);
                    }
                    cJSON_Delete(status);
                }
            }
            last_status = now;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    g_task = NULL;
    espx_task_delete_self();
}

esp_err_t mqtt_publisher_init(void)
{
    ESP_LOGI(TAG, "MQTT publisher initialized");
    return ESP_OK;
}

esp_err_t mqtt_publisher_start(void)
{
    if (g_task != NULL) {
        return ESP_OK;
    }

    g_running = true;
    espx_task_create(publisher_task, "mqtt_pub", 4096, NULL, 1, &g_task);
    return ESP_OK;
}

esp_err_t mqtt_publisher_stop(void)
{
    g_running = false;
    if (g_task) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return ESP_OK;
}
