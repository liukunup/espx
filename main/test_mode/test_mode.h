/**
 * @file test_mode.h
 * @brief Manufacturing Test Mode (hardware self-test)
 *
 * Entry methods (any of them):
 *   1. NVS request flag  - set by test_mode_request(); survives reboot.
 *                          Reachable from the Web UI, MQTT, or the long-press
 *                          watchdog (hold BOOT ≈3 s while the app is running).
 *   2. Trigger GPIO      - optional, only for a dedicated (non-strapping) pin.
 *                          Disabled when TEST_MODE_GPIO < 0.
 *
 * NOTE: do NOT use a strapping pin such as GPIO0 as the trigger. Holding
 * GPIO0 low across reset puts the chip into the ROM UART download mode, so
 * the application never runs and the trigger is never evaluated. GPIO0 is
 * supported only via the post-boot long-press watchdog.
 */

#ifndef TEST_MODE_H
#define TEST_MODE_H

#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test-mode trigger GPIO, or -1 to disable
 *
 * Configure via CONFIG_MFG_TEST_GPIO. Use a free GPIO, never a strapping pin.
 */
#ifndef TEST_MODE_GPIO
#define TEST_MODE_GPIO CONFIG_MFG_TEST_GPIO
#endif

/** BOOT button GPIO used by the long-press watchdog (ESP32-S3 default) */
#ifndef TEST_MODE_BOOT_GPIO
#define TEST_MODE_BOOT_GPIO 0
#endif

/** Hold time for the long-press watchdog */
#define TEST_MODE_LONGPRESS_MS 3000

/**
 * @brief Request test mode on the next boot
 *
 * Persists a flag in NVS. Call esp_restart() afterwards.
 */
esp_err_t test_mode_request(void);

/**
 * @brief Clear a pending test-mode request
 */
esp_err_t test_mode_clear_request(void);

/**
 * @brief Check whether test mode should be entered
 *
 * Consumes a pending NVS request, or samples TEST_MODE_GPIO when enabled.
 *
 * @return ESP_OK if test mode should be entered, ESP_FAIL otherwise
 */
esp_err_t test_mode_check_trigger(void);

/**
 * @brief Enter test mode
 *
 * Initializes the device registry + peripherals (no Wi-Fi/MQTT) and runs the
 * interactive UART console. Does not return (reboots on exit).
 */
void test_mode_enter(void);

/**
 * @brief Start the BOOT-button long-press watchdog
 *
 * When the button is held for TEST_MODE_LONGPRESS_MS while the application is
 * running, test mode is requested and the device reboots into it. This is the
 * reliable way to enter test mode on boards where the only button is GPIO0.
 */
esp_err_t test_mode_start_longpress_watchdog(void);

#ifdef __cplusplus
}
#endif

#endif // TEST_MODE_H
