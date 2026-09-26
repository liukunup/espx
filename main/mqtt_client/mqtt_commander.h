/**
 * @file mqtt_commander.h
 * @brief MQTT command handler
 */

#ifndef MQTT_COMMANDER_H
#define MQTT_COMMANDER_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize MQTT commander
 */
esp_err_t mqtt_commander_init(void);

/**
 * @brief Handle incoming MQTT message
 *
 * Called from MQTT event handler.
 *
 * @param topic Topic name
 * @param payload Payload data
 * @param payload_len Payload length
 */
void mqtt_commander_handle(const char *topic, const char *payload, int payload_len);

#ifdef __cplusplus
}
#endif

#endif // MQTT_COMMANDER_H
