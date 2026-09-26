/**
 * @file mqtt_publisher.h
 * @brief MQTT auto-publisher for device state
 */

#ifndef MQTT_PUBLISHER_H
#define MQTT_PUBLISHER_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mqtt_publisher_init(void);
esp_err_t mqtt_publisher_start(void);
esp_err_t mqtt_publisher_stop(void);

#ifdef __cplusplus
}
#endif

#endif // MQTT_PUBLISHER_H
