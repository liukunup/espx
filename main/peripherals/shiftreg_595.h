/**
 * @file shiftreg_595.h
 * @brief 74HC595 shift register driver
 */

#ifndef SHIFTREG_595_H
#define SHIFTREG_595_H

#include "device_type.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t shiftreg_595_driver_register(void);

#ifdef __cplusplus
}
#endif

#endif // SHIFTREG_595_H
