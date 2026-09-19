/**
 * @file event_loop.h
 * @brief Event Loop component for ESP32 IoT firmware
 *
 * Provides event-driven communication between components using FreeRTOS queues.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Event types enumeration
 */
typedef enum {
    EVENT_WIFI_CONNECTED,
    EVENT_WIFI_DISCONNECTED,
    EVENT_MQTT_CONNECTED,
    EVENT_MQTT_DISCONNECTED,
    EVENT_SENSOR_DATA_READY,
    EVENT_TELEMETRY_SEND,
    EVENT_OTA_START,
    EVENT_OTA_PROGRESS,
    EVENT_OTA_COMPLETE,
    EVENT_OTA_FAILED,
    EVENT_CONFIG_UPDATED,
    EVENT_CMD_RECEIVED,
    EVENT_LOW_MEMORY,
    EVENT_WATCHDOG_TIMEOUT,
    EVENT_EXCEPTION,
    EVENT_TYPE_COUNT
} event_type_t;

/**
 * @brief Event data structure
 */
typedef struct {
    event_type_t type;
    uint32_t timestamp;
    void *data;
    size_t data_len;
} event_t;

/**
 * @brief Event handler callback type
 */
typedef void (*event_handler_t)(const event_t *event);

/**
 * @brief Initialize the event loop
 *
 * @return 0 on success, negative on error
 */
int event_loop_init(void);

/**
 * @brief Publish an event to the event queue
 *
 * @param type Event type
 * @param data Event data (can be NULL)
 * @param len Size of event data in bytes
 * @return 0 on success, negative on error
 */
int event_publish(event_type_t type, const void *data, size_t len);

/**
 * @brief Subscribe to an event type
 *
 * @param type Event type to subscribe
 * @param handler Callback handler function
 * @return 0 on success, negative on error
 */
int event_subscribe(event_type_t type, event_handler_t handler);

/**
 * @brief Get event type name as string
 *
 * @param type Event type
 * @return String representation of event type
 */
const char* event_type_to_string(event_type_t type);

#ifdef __cplusplus
}
#endif
