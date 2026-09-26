/**
 * @file ina226.h
 * @brief INA226 power monitor (I2C) driver
 */

#ifndef INA226_H
#define INA226_H

#include "device_type.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ina226_driver_register(void);

#ifdef __cplusplus
}
#endif

#endif // INA226_H
