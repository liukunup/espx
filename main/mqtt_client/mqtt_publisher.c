/**
 * @file mqtt_publisher.c
 * @brief MQTT auto-publisher
 *
 * Periodically publishes:
 *   - Heartbeat (state topic)
 *   - Sensor readings (sensors topic)
 */

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>

#include "mqtt_publisher.h"
#include "espx_mqtt_client.h"
#include "device_manager.h"
#include "device_type.h"

static const char *TAG = "mqtt_publisher";

#define HEARTBEAT_INTERVAL_S 30
#define SENSOR_INTERVAL_S    10

static TaskHandle_t g_task = NULL;
static volatile bool g_running = false;

static void publisher_task(void *arg)
{
    int64_t last_heartbeat = 0;
    int64_t last_sensor = 0;

    while (g_running) {
        int64_t now = esp_timer_get_time();

        // Heartbeat
        if (now - last_heartbeat > HEARTBEAT_INTERVAL_S * 1000000LL) {
            if (mqtt_client_is_connected()) {
                char payload[128];
                snprintf(payload, sizeof(payload),
                         "{\"online\":true,\"uptime\":%lu}",
                         (unsigned long)(now / 1000000));
                mqtt_client_publish("state", payload, strlen(payload), 1, true);
            }
            last_heartbeat = now;
        }

        // Sensor data
        if (now - last_sensor > SENSOR_INTERVAL_S * 1000000LL) {
            if (mqtt_client_is_connected()) {
                cJSON *sensors = cJSON_CreateObject();

                size_t count;
                const device_t *devices = device_get_all(&count);

                for (size_t i = 0; i < count; i++) {
                    const device_t *dev = &devices[i];
                    if (!dev->enabled || !dev->initialized) continue;
                    if (!(dev->type->capabilities & DEVICE_CAPABILITY_PERIODIC)) continue;

                    cJSON *value = cJSON_CreateObject();
                    if (dev->type->read && dev->type->read((device_t*)dev, value) == ESP_OK) {
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

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    g_task = NULL;
    vTaskDelete(NULL);
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
    xTaskCreate(publisher_task, "mqtt_pub", 4096, NULL, 1, &g_task);
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
