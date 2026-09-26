/**
 * @file buzzer.h
 * @brief Passive buzzer (PWM via LEDC) driver
 */

#ifndef BUZZER_H
#define BUZZER_H

#include "device_type.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t buzzer_driver_register(void);

#ifdef __cplusplus
}
#endif

#endif // BUZZER_H
