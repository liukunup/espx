/**
 * @file ws2812.h
 * @brief WS2812 RGB LED Driver
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED color structure
 */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ws2812_color_t;

/**
 * @brief WS2812 pixel structure
 */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} ws2812_pixel_t;

/**
 * @brief WS2812 configuration
 */
typedef struct {
    int8_t gpio_num;          // GPIO pin number (-1 to disable)
    uint16_t led_count;       // Number of LEDs in the strip
    uint32_t brightness;      // Global brightness (0-255)
} ws2812_config_t;

/**
 * @brief WS2812 handle
 */
typedef struct ws2812_handle_s *ws2812_handle_t;

/**
 * @brief Create WS2812 driver instance
 * @param config Configuration
 * @return Handle or NULL on error
 */
ws2812_handle_t ws2812_create(const ws2812_config_t *config);

/**
 * @brief Delete WS2812 driver instance
 * @param handle Driver handle
 */
void ws2812_delete(ws2812_handle_t handle);

/**
 * @brief Initialize WS2812 driver
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int ws2812_init(ws2812_handle_t handle);

/**
 * @brief Set single LED color
 * @param handle Driver handle
 * @param index LED index (0 to led_count-1)
 * @param color Color (RGB)
 * @return 0 on success, negative on error
 */
int ws2812_set_pixel(ws2812_handle_t handle, uint16_t index, const ws2812_color_t *color);

/**
 * @brief Set all LEDs to same color
 * @param handle Driver handle
 * @param color Color (RGB)
 * @return 0 on success, negative on error
 */
int ws2812_set_all(ws2812_handle_t handle, const ws2812_color_t *color);

/**
 * @brief Set LED with RGB values directly
 * @param handle Driver handle
 * @param index LED index
 * @param r Red (0-255)
 * @param g Green (0-255)
 * @param b Blue (0-255)
 * @return 0 on success, negative on error
 */
int ws2812_set_pixel_rgb(ws2812_handle_t handle, uint16_t index, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set all LEDs with same RGB value
 * @param handle Driver handle
 * @param r Red (0-255)
 * @param g Green (0-255)
 * @param b Blue (0-255)
 * @return 0 on success, negative on error
 */
int ws2812_set_all_rgb(ws2812_handle_t handle, uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Clear all LEDs (turn off)
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int ws2812_clear(ws2812_handle_t handle);

/**
 * @brief Set global brightness
 * @param handle Driver handle
 * @param brightness Brightness (0-255)
 * @return 0 on success, negative on error
 */
int ws2812_set_brightness(ws2812_handle_t handle, uint8_t brightness);

/**
 * @brief Show/refresh LEDs (send data to strip)
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int ws2812_show(ws2812_handle_t handle);

/**
 * @brief Set rainbow effect on all LEDs
 * @param handle Driver handle
 * @param hue Hue value (0-360)
 * @return 0 on success, negative on error
 */
int ws2812_set_hue(ws2812_handle_t handle, uint16_t hue);

/**
 * @brief Convert HSV to RGB color
 * @param h Hue (0-360)
 * @param s Saturation (0-100)
 * @param v Value/Brightness (0-100)
 * @param color Output RGB color
 */
void ws2812_hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, ws2812_color_t *color);

#ifdef __cplusplus
}
#endif
