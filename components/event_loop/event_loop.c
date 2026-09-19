/**
 * @file event_loop.c
 * @brief Event Loop implementation for ESP32 IoT firmware
 */

#include "event_loop.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "event_loop";

/** @brief Maximum number of handlers per event type */
#define MAX_HANDLERS_PER_EVENT 4

/** @brief Maximum queue size for events */
#define EVENT_QUEUE_SIZE 16

/** @brief Event handler entry */
typedef struct {
    event_handler_t handler;
    bool in_use;
} handler_entry_t;

/** @brief Handler registry for each event type */
static handler_entry_t g_handlers[EVENT_TYPE_COUNT][MAX_HANDLERS_PER_EVENT];

/** @brief Event queue handle */
static QueueHandle_t g_event_queue = NULL;

/** @brief Event loop task handle */
static TaskHandle_t g_event_task_handle = NULL;

/** @brief Whether event loop is initialized */
static bool g_initialized = false;

/** @brief Forward declaration of event loop task */
static void event_loop_task(void *params);

/**
 * @brief Event type to string mapping
 */
static const char* g_event_names[EVENT_TYPE_COUNT] = {
    [EVENT_WIFI_CONNECTED] = "WIFI_CONNECTED",
    [EVENT_WIFI_DISCONNECTED] = "WIFI_DISCONNECTED",
    [EVENT_MQTT_CONNECTED] = "MQTT_CONNECTED",
    [EVENT_MQTT_DISCONNECTED] = "MQTT_DISCONNECTED",
    [EVENT_SENSOR_DATA_READY] = "SENSOR_DATA_READY",
    [EVENT_TELEMETRY_SEND] = "TELEMETRY_SEND",
    [EVENT_OTA_START] = "OTA_START",
    [EVENT_OTA_PROGRESS] = "OTA_PROGRESS",
    [EVENT_OTA_COMPLETE] = "OTA_COMPLETE",
    [EVENT_OTA_FAILED] = "OTA_FAILED",
    [EVENT_CONFIG_UPDATED] = "CONFIG_UPDATED",
    [EVENT_CMD_RECEIVED] = "CMD_RECEIVED",
    [EVENT_LOW_MEMORY] = "LOW_MEMORY",
    [EVENT_WATCHDOG_TIMEOUT] = "WATCHDOG_TIMEOUT",
    [EVENT_EXCEPTION] = "EXCEPTION",
};

const char* event_type_to_string(event_type_t type) {
    if (type >= EVENT_TYPE_COUNT) {
        return "UNKNOWN";
    }
    return g_event_names[type] ? g_event_names[type] : "UNKNOWN";
}

int event_loop_init(void) {
    if (g_initialized) {
        ESP_LOGW(TAG, "Event loop already initialized");
        return 0;
    }

    // Initialize handler registry
    memset(g_handlers, 0, sizeof(g_handlers));

    // Create event queue
    g_event_queue = xQueueCreate(EVENT_QUEUE_SIZE, sizeof(event_t));
    if (g_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return -1;
    }

    // Create event loop task
    BaseType_t ret = xTaskCreatePinnedToCore(
        event_loop_task,
        "event_loop",
        4096,
        NULL,
        tskIDLE_PRIORITY + 2,
        &g_event_task_handle,
        0  // Pin to core 0
    );

    if (ret != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create event loop task");
        vQueueDelete(g_event_queue);
        g_event_queue = NULL;
        return -1;
    }

    g_initialized = true;
    ESP_LOGI(TAG, "Event loop initialized");
    return 0;
}

int event_publish(event_type_t type, const void *data, size_t len) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Event loop not initialized");
        return -1;
    }

    if (type >= EVENT_TYPE_COUNT) {
        ESP_LOGE(TAG, "Invalid event type: %d", type);
        return -1;
    }

    event_t event = {
        .type = type,
        .timestamp = (uint32_t)(esp_timer_get_time() / 1000),
        .data = (void*)data,
        .data_len = len
    };

    BaseType_t ret = xQueueSend(g_event_queue, &event, 0);
    if (ret != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full, dropping event: %s", event_type_to_string(type));
        return -2;  // Queue full
    }

    ESP_LOGD(TAG, "Published event: %s", event_type_to_string(type));
    return 0;
}

int event_subscribe(event_type_t type, event_handler_t handler) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "Event loop not initialized");
        return -1;
    }

    if (type >= EVENT_TYPE_COUNT) {
        ESP_LOGE(TAG, "Invalid event type: %d", type);
        return -1;
    }

    if (handler == NULL) {
        ESP_LOGE(TAG, "Handler cannot be NULL");
        return -1;
    }

    // Find an empty slot
    for (int i = 0; i < MAX_HANDLERS_PER_EVENT; i++) {
        if (!g_handlers[type][i].in_use) {
            g_handlers[type][i].handler = handler;
            g_handlers[type][i].in_use = true;
            ESP_LOGD(TAG, "Subscribed handler to event: %s (slot %d)",
                     event_type_to_string(type), i);
            return 0;
        }
    }

    ESP_LOGE(TAG, "No free handler slot for event: %s", event_type_to_string(type));
    return -2;  // No slot available
}

void event_loop_task(void *params) {
    (void)params;

    event_t event;
    ESP_LOGI(TAG, "Event loop task started");

    while (1) {
        // Wait for event with timeout
        BaseType_t ret = xQueueReceive(g_event_queue, &event, pdMS_TO_TICKS(1000));

        if (ret == pdTRUE) {
            ESP_LOGD(TAG, "Processing event: %s", event_type_to_string(event.type));

            // Dispatch to all registered handlers
            for (int i = 0; i < MAX_HANDLERS_PER_EVENT; i++) {
                if (g_handlers[event.type][i].in_use) {
                    handler_entry_t *entry = &g_handlers[event.type][i];
                    if (entry->handler != NULL) {
                        // Call handler
                        entry->handler(&event);
                    }
                }
            }
        }
        // If timeout, just continue the loop
    }
}
