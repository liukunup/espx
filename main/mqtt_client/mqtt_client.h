/**
 * @file mqtt_client.h
 * @brief MQTT Client for ESPX device
 */

#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MQTT event types
 */
typedef enum {
    MQTT_EVENT_CONNECTED,
    MQTT_EVENT_DISCONNECTED,
    MQTT_EVENT_SUBSCRIBED,
    MQTT_EVENT_PUBLISHED,
    MQTT_EVENT_DATA,
} mqtt_event_type_t;

/**
 * @brief MQTT message data
 */
typedef struct {
    const char *topic;
    size_t topic_len;
    const char *data;
    size_t data_len;
} mqtt_message_t;

/**
 * @brief MQTT event callback
 */
typedef void (*mqtt_event_cb_t)(mqtt_event_type_t event, mqtt_message_t *msg, void *user_data);

/**
 * @brief Initialize MQTT client
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_client_init(void);

/**
 * @brief Start MQTT client
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_client_start(void);

/**
 * @brief Stop MQTT client
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_client_stop(void);

/**
 * @brief Check if MQTT is connected
 *
 * @return true if connected, false otherwise
 */
bool mqtt_client_is_connected(void);

/**
 * @brief Publish message to topic
 *
 * @param topic Topic name
 * @param data Message data
 * @param len Data length
 * @param qos QoS level (0, 1, or 2)
 * @param retain Retain flag
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_client_publish(const char *topic, const char *data, size_t len, int qos, bool retain);

/**
 * @brief Subscribe to topic
 *
 * @param topic Topic pattern
 * @param qos QoS level
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_client_subscribe(const char *topic, int qos);

/**
 * @brief Unsubscribe from topic
 *
 * @param topic Topic name
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_client_unsubscribe(const char *topic);

/**
 * @brief Set event callback
 *
 * @param cb Callback function
 * @param user_data User data passed to callback
 */
void mqtt_client_set_event_callback(mqtt_event_cb_t cb, void *user_data);

/**
 * @brief Reconnect MQTT client
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mqtt_client_reconnect(void);

#ifdef __cplusplus
}
#endif

#endif // MQTT_CLIENT_H
