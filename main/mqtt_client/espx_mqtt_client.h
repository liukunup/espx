/**
 * @file mqtt_client.h
 * @brief ESPX MQTT Client
 */

#ifndef ESPX_MQTT_CLIENT_H
#define ESPX_MQTT_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mqtt_client_init(void);
esp_err_t mqtt_client_start(void);
esp_err_t mqtt_client_stop(void);

bool mqtt_client_is_connected(void);

/**
 * @brief Publish to a subtopic under the configured prefix
 */
esp_err_t mqtt_client_publish(const char *subtopic, const char *data, size_t len, int qos, bool retain);

/**
 * @brief Publish to an ABSOLUTE topic (no prefix added)
 *
 * Used by callers that own the whole topic path, such as the AT interface
 * where the host supplies it.
 */
esp_err_t mqtt_client_publish_absolute(const char *topic, const char *data,
                                       size_t len, int qos, bool retain);

/**
 * @brief Subscribe to an ABSOLUTE topic filter
 */
esp_err_t mqtt_client_subscribe(const char *topic_filter, int qos);

/**
 * @brief Unsubscribe from an ABSOLUTE topic filter
 */
esp_err_t mqtt_client_unsubscribe(const char *topic_filter);

/**
 * @brief Get current topic prefix
 */
const char* mqtt_client_get_prefix(void);

#ifdef __cplusplus
}
#endif

#endif // ESPX_MQTT_CLIENT_H
