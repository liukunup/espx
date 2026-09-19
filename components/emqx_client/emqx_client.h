/**
 * @file mqtt_client.h
 * @brief MQTT Client component for EMQX communication
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "mqtt_client.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MQTT state enumeration
 */
typedef enum {
    MQTT_STATE_IDLE,
    MQTT_STATE_CONNECTING,
    MQTT_STATE_CONNECTED,
    MQTT_STATE_DISCONNECTED,
    MQTT_STATE_ERROR
} mqtt_state_t;

/**
 * @brief MQTT QoS level
 */
typedef enum {
    MQTT_QOS_0,
    MQTT_QOS_1,
    MQTT_QOS_2
} mqtt_qos_t;

/**
 * @brief MQTT message callback
 */
typedef void (*mqtt_message_cb_t)(const char *topic, const char *data, int len);

/**
 * @brief Connection status callback
 */
typedef void (*mqtt_connect_cb_t)(mqtt_state_t state);

/** @brief Default MQTT port */
#define DEFAULT_MQTT_PORT 8883

/** @brief Default keepalive interval (seconds) */
#define DEFAULT_KEEPALIVE 60

/**
 * @brief Initialize MQTT client
 *
 * @return 0 on success, negative on error
 */
int mqtt_client_init(void);

/**
 * @brief Configure MQTT client with broker URL
 *
 * @param broker Broker URL (e.g., mqtt://192.168.1.100:8883)
 * @return 0 on success, negative on error
 */
int mqtt_client_configure(const char *broker);

/**
 * @brief Set TLS/SSL configuration
 *
 * @param ca_cert CA certificate (PEM format)
 * @param client_cert Client certificate (PEM format, can be NULL)
 * @param client_key Client private key (PEM format, can be NULL)
 * @return 0 on success, negative on error
 */
int mqtt_client_set_tls(const char *ca_cert, const char *client_cert, const char *client_key);

/**
 * @brief Set authentication credentials
 *
 * @param username MQTT username
 * @param password MQTT password
 * @return 0 on success, negative on error
 */
int mqtt_client_set_auth(const char *username, const char *password);

/**
 * @brief Set client ID
 *
 * @param client_id Client identifier
 * @return 0 on success, negative on error
 */
int mqtt_client_set_client_id(const char *client_id);

/**
 * @brief Start MQTT connection
 *
 * @return 0 on success, negative on error
 */
int mqtt_client_start(void);

/**
 * @brief Stop MQTT connection
 *
 * @return 0 on success, negative on error
 */
int mqtt_client_stop(void);

/**
 * @brief Reconnect to broker
 *
 * @return 0 on success, negative on error
 */
int mqtt_client_reconnect(void);

/**
 * @brief Publish a message
 *
 * @param topic Topic name
 * @param data Message payload
 * @param len Payload length (-1 for auto-calculate)
 * @param qos QoS level
 * @param retain Retain flag
 * @return Message ID on success, negative on error
 */
int mqtt_publish(const char *topic, const char *data, int len, mqtt_qos_t qos, bool retain);

/**
 * @brief Subscribe to a topic
 *
 * @param topic Topic pattern (e.g., /device/+/cmd)
 * @param callback Message callback
 * @param qos QoS level
 * @return 0 on success, negative on error
 */
int mqtt_subscribe(const char *topic, mqtt_message_cb_t callback, mqtt_qos_t qos);

/**
 * @brief Unsubscribe from a topic
 *
 * @param topic Topic name
 * @return 0 on success, negative on error
 */
int mqtt_unsubscribe(const char *topic);

/**
 * @brief Get current MQTT state
 *
 * @return Current state
 */
mqtt_state_t mqtt_get_state(void);

/**
 * @brief Check if connected
 *
 * @return true if connected
 */
bool mqtt_is_connected(void);

/**
 * @brief Register connection status callback
 *
 * @param callback Callback function
 * @return 0 on success
 */
int mqtt_register_connect_callback(mqtt_connect_cb_t callback);

// Convenience publish functions for common message types

/**
 * @brief Publish telemetry data
 *
 * @param json_data JSON-formatted telemetry data
 * @return Message ID on success, negative on error
 */
int mqtt_publish_telemetry(const char *json_data);

/**
 * @brief Publish device status
 *
 * @param json_status JSON-formatted status data
 * @return Message ID on success, negative on error
 */
int mqtt_publish_status(const char *json_status);

/**
 * @brief Publish log message
 *
 * @param level Log level (1-5)
 * @param message Log message
 * @return Message ID on success, negative on error
 */
int mqtt_publish_log(int level, const char *message);

/**
 * @brief Publish command response
 *
 * @param cmd_id Command ID
 * @param code Response code (0 = success)
 * @param message Response message
 * @param result JSON result (can be NULL)
 * @return Message ID on success, negative on error
 */
int mqtt_publish_cmd_response(const char *cmd_id, int code, const char *message, const char *result);

/**
 * @brief Publish OTA progress
 *
 * @param progress Progress percentage (0-100)
 * @param message Status message
 * @return Message ID on success, negative on error
 */
int mqtt_publish_ota_progress(int progress, const char *message);

// Topic format macros (use with %%s for device_id)
#define TOPIC_TELEMETRY_FMT "/device/%s/telemetry"
#define TOPIC_STATUS_FMT "/device/%s/status"
#define TOPIC_LOG_FMT "/device/%s/log"
#define TOPIC_CMD_FMT "/device/%s/cmd"
#define TOPIC_CMD_RESPONSE_FMT "/device/%s/cmd/response"
#define TOPIC_CONFIG_FMT "/device/%s/config"
#define TOPIC_OTA_START_FMT "/device/%s/ota/start"
#define TOPIC_OTA_DATA_FMT "/device/%s/ota/data"
#define TOPIC_OTA_END_FMT "/device/%s/ota/end"
#define TOPIC_OTA_PROGRESS_FMT "/device/%s/ota/progress"

#ifdef __cplusplus
}
#endif
