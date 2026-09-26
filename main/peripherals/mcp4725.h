/**
 * @file mcp4725.h
 * @brief MCP4725 12-bit DAC (I2C) driver
 */

#ifndef MCP4725_H
#define MCP4725_H

#include "device_type.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mcp4725_driver_register(void);

#ifdef __cplusplus
}
#endif

#endif // MCP4725_H
