/**
 * @file peripherals.h
 * @brief Register all peripheral drivers
 */

#ifndef PERIPHERALS_H
#define PERIPHERALS_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t peripherals_register_all(void);

#ifdef __cplusplus
}
#endif

#endif // PERIPHERALS_H
