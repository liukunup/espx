/**
 * @file dht11.h
 * @brief DHT11 temperature/humidity sensor driver
 */

#ifndef DHT11_H
#define DHT11_H

#include "device_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register DHT11 device type
 */
esp_err_t dht11_driver_register(void);

#ifdef __cplusplus
}
#endif

#endif // DHT11_H
