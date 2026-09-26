/**
 * @file ws2812.h
 * @brief WS2812 RGB LED strip driver
 */

#ifndef WS2812_H
#define WS2812_H

#include "device_type.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ws2812_driver_register(void);

#ifdef __cplusplus
}
#endif

#endif // WS2812_H
