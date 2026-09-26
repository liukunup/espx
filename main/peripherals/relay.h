/**
 * @file relay.h
 * @brief Relay output driver
 */

#ifndef RELAY_H
#define RELAY_H

#include "device_type.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t relay_driver_register(void);

#ifdef __cplusplus
}
#endif

#endif // RELAY_H
