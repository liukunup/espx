/**
 * @file event_bus.c
 * @brief Event bus implementation
 */

#include <string.h>
#include <esp_log.h>
#include "event_bus.h"

static const char *TAG = "event_bus";

static event_handler_entry_t g_handlers[EVENT_BUS_MAX_HANDLERS];
static size_t g_handler_count = 0;

const char* event_type_to_string(event_type_t type)
{
    switch (type) {
    case EVENT_DEVICE_ADDED: return "device_added";
    case EVENT_DEVICE_REMOVED: return "device_removed";
    case EVENT_DEVICE_CHANGED: return "device_changed";
    case EVENT_DEVICE_VALUE_CHANGED: return "device_value_changed";
    case EVENT_NODE_READY: return "node_ready";
    case EVENT_NODE_RESET: return "node_reset";
    case EVENT_WIFI_CONNECTED: return "wifi_connected";
    case EVENT_WIFI_DISCONNECTED: return "wifi_disconnected";
    case EVENT_MQTT_CONNECTED: return "mqtt_connected";
    case EVENT_MQTT_DISCONNECTED: return "mqtt_disconnected";
    case EVENT_OTA_START: return "ota_start";
    case EVENT_OTA_PROGRESS: return "ota_progress";
    case EVENT_OTA_COMPLETE: return "ota_complete";
    case EVENT_OTA_FAILED: return "ota_failed";
    default: return "unknown";
    }
}

esp_err_t event_bus_init(void)
{
    memset(g_handlers, 0, sizeof(g_handlers));
    g_handler_count = 0;
    ESP_LOGI(TAG, "Event bus initialized");
    return ESP_OK;
}

esp_err_t event_bus_subscribe(event_type_t type, event_handler_t handler, void *user_data)
{
    if (handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_handler_count >= EVENT_BUS_MAX_HANDLERS) {
        ESP_LOGE(TAG, "Max handlers reached");
        return ESP_ERR_NO_MEM;
    }

    // Check duplicate
    for (size_t i = 0; i < g_handler_count; i++) {
        if (g_handlers[i].type == type && g_handlers[i].handler == handler) {
            return ESP_OK;  // Already subscribed
        }
    }

    g_handlers[g_handler_count].type = type;
    g_handlers[g_handler_count].handler = handler;
    g_handlers[g_handler_count].user_data = user_data;
    g_handler_count++;

    return ESP_OK;
}

esp_err_t event_bus_unsubscribe(event_type_t type, event_handler_t handler)
{
    for (size_t i = 0; i < g_handler_count; i++) {
        if (g_handlers[i].type == type && g_handlers[i].handler == handler) {
            // Shift remaining
            for (size_t j = i; j < g_handler_count - 1; j++) {
                g_handlers[j] = g_handlers[j + 1];
            }
            g_handler_count--;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t event_bus_publish(event_type_t type, const char *topic, void *data)
{
    event_t event = {
        .type = type,
        .topic = topic,
        .data = data,
    };

    for (size_t i = 0; i < g_handler_count; i++) {
        if (g_handlers[i].type == type && g_handlers[i].handler) {
            g_handlers[i].handler(&event, g_handlers[i].user_data);
        }
    }

    return ESP_OK;
}
