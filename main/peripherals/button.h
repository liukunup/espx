/**
 * @file button.h
 * @brief Button input driver
 */

#ifndef BUTTON_H
#define BUTTON_H

#include "device_type.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t button_driver_register(void);

#ifdef __cplusplus
}
#endif

#endif // BUTTON_H
