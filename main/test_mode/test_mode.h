/**
 * @file test_mode.h
 * @brief Manufacturing Test Mode (hardware self-test)
 *
 * Triggered at boot by pulling TEST_MODE_GPIO low. Provides an interactive
 * UART console to configure devices and run hardware self-tests.
 */

#ifndef TEST_MODE_H
#define TEST_MODE_H

#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/** GPIO held low at boot to enter test mode */
#ifndef TEST_MODE_GPIO
#define TEST_MODE_GPIO CONFIG_MFG_TEST_GPIO
#endif

/**
 * @brief Check whether test mode should be entered
 *
 * Configures TEST_MODE_GPIO as input with pull-up and samples it.
 *
 * @return ESP_OK if triggered (pin low), ESP_FAIL otherwise
 */
esp_err_t test_mode_check_trigger(void);

/**
 * @brief Enter test mode
 *
 * Initializes device registry + peripherals (without Wi-Fi/MQTT) and runs
 * the interactive UART console. Does not return (reboots on exit).
 */
void test_mode_enter(void);

#ifdef __cplusplus
}
#endif

#endif // TEST_MODE_H
