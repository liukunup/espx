/**
 * @file event_bus.h
 * @brief Event bus for inter-module communication
 */

#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cJSON;
typedef struct cJSON cJSON;

/**
 * @brief Event types
 */
typedef enum {
    EVENT_DEVICE_ADDED,
    EVENT_DEVICE_REMOVED,
    EVENT_DEVICE_CHANGED,
    EVENT_DEVICE_VALUE_CHANGED,
    EVENT_NODE_READY,
    EVENT_NODE_RESET,
    EVENT_CONFIG_CHANGED,
    EVENT_WIFI_CONNECTED,
    EVENT_WIFI_DISCONNECTED,
    EVENT_MQTT_CONNECTED,
    EVENT_MQTT_DISCONNECTED,
    EVENT_OTA_START,
    EVENT_OTA_PROGRESS,
    EVENT_OTA_COMPLETE,
    EVENT_OTA_FAILED,
} event_type_t;

/**
 * @brief Event structure
 */
typedef struct {
    event_type_t type;
    const char *topic;
    void *data;        // cJSON * or NULL
} event_t;

/**
 * @brief Event handler
 */
typedef void (*event_handler_t)(const event_t *event, void *user_data);

/**
 * @brief Handler entry
 */
typedef struct {
    event_type_t type;
    event_handler_t handler;
    void *user_data;
} event_handler_entry_t;

#define EVENT_BUS_MAX_HANDLERS 32

/**
 * @brief Initialize event bus
 */
esp_err_t event_bus_init(void);

/**
 * @brief Subscribe to an event type
 */
esp_err_t event_bus_subscribe(event_type_t type, event_handler_t handler, void *user_data);

/**
 * @brief Unsubscribe from an event
 */
esp_err_t event_bus_unsubscribe(event_type_t type, event_handler_t handler);

/**
 * @brief Publish an event
 */
esp_err_t event_bus_publish(event_type_t type, const char *topic, void *data);

/**
 * @brief Get event type name
 */
const char* event_type_to_string(event_type_t type);

#ifdef __cplusplus
}
#endif

#endif // EVENT_BUS_H
