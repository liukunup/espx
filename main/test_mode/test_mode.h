/**
 * @file test_mode.h
 * @brief Manufacturing Test Mode for ESPX device
 */

#ifndef TEST_MODE_H
#define TEST_MODE_H

#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test item types
 */
typedef enum {
    TEST_ITEM_LED,
    TEST_ITEM_BUTTON,
    TEST_ITEM_WIFI,
    TEST_ITEM_MQTT,
    TEST_ITEM_UART,
    TEST_ITEM_COUNT
} test_item_t;

/**
 * @brief Test result
 */
typedef enum {
    TEST_RESULT_NONE,
    TEST_RESULT_PASS,
    TEST_RESULT_FAIL,
} test_result_t;

/**
 * @brief Test mode GPIO pin
 */
#define TEST_MODE_GPIO CONFIG_MFG_TEST_GPIO

/**
 * @brief Check if test mode should be triggered
 *
 * Called during boot to check if test mode GPIO is pressed.
 *
 * @return ESP_OK if test mode triggered, ESP_FAIL otherwise
 */
esp_err_t test_mode_check_trigger(void);

/**
 * @brief Enter test mode
 *
 * Runs the interactive test menu via UART.
 * This function does not return - device reboots on exit.
 */
void test_mode_enter(void);

/**
 * @brief Get test result for a specific item
 *
 * @param item Test item
 * @return Test result
 */
test_result_t test_mode_get_result(test_item_t item);

/**
 * @brief Set test result for a specific item
 *
 * @param item Test item
 * @param result Test result
 */
void test_mode_set_result(test_item_t item, test_result_t result);

#ifdef __cplusplus
}
#endif

#endif // TEST_MODE_H
