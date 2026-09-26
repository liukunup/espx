/**
 * @file led_driver.h
 * @brief LED Driver for ESPX device
 */

#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED patterns
 */
typedef enum {
    LED_PATTERN_NONE,
    LED_PATTERN_BLINK,
    LED_PATTERN_BREATHE,
    LED_PATTERN_PULSE,
} led_pattern_t;

/**
 * @brief Initialize LED driver
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t led_driver_init(void);

/**
 * @brief Set LED color (RGB)
 *
 * @param r Red value (0-255)
 * @param g Green value (0-255)
 * @param b Blue value (0-255)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t led_set_color(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set LED brightness
 *
 * @param brightness Brightness level (0-100)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t led_set_brightness(uint8_t brightness);

/**
 * @brief Set LED pattern
 *
 * @param pattern Pattern to display
 * @param period Period in milliseconds (for blink/breath)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t led_set_pattern(led_pattern_t pattern, uint32_t period);

/**
 * @brief Stop LED pattern
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t led_stop_pattern(void);

/**
 * @brief Set status indicator
 *
 * @param status Status type (connected, disconnected, error, etc.)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t led_set_status(const char *status);

/**
 * @brief Turn LED off
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t led_off(void);

#ifdef __cplusplus
}
#endif

#endif // LED_DRIVER_H
