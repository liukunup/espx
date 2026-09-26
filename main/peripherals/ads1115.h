/**
 * @file ads1115.h
 * @brief ADS1115 16-bit ADC (I2C) driver
 */

#ifndef ADS1115_H
#define ADS1115_H

#include "device_type.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ads1115_driver_register(void);

#ifdef __cplusplus
}
#endif

#endif // ADS1115_H
