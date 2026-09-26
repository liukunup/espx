/**
 * @file tja1050.h
 * @brief TJA1050 CAN (TWAI) transceiver driver
 */

#ifndef TJA1050_H
#define TJA1050_H

#include "device_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the TJA1050 CAN driver
 * @return ESP_OK on success
 */
esp_err_t tja1050_driver_register(void);

#ifdef __cplusplus
}
#endif

#endif // TJA1050_H
