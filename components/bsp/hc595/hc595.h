/**
 * @file hc595.h
 * @brief 74HC595 Shift Register Driver
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HC595 output polarity
 */
typedef enum {
    HC595_POLARITY_NORMAL = 0,    // High bit = high output
    HC595_POLARITY_INVERTED,      // High bit = low output
} hc595_polarity_t;

/**
 * @brief HC595 configuration
 */
typedef struct {
    int8_t ser_gpio;       // Serial data input (DS pin)
    int8_t rclk_gpio;      // Register clock/latch (RCLK pin)
    int8_t srclk_gpio;     // Shift register clock (SRCLK pin)
    int8_t srclr_gpio;     // Shift register clear (SRCLR pin, -1 if always high)
    uint8_t num_chips;     // Number of cascaded 74HC595 chips (1-8)
    hc595_polarity_t polarity;
} hc595_config_t;

/**
 * @brief HC595 handle
 */
typedef struct hc595_handle_s *hc595_handle_t;

/**
 * @brief Create HC595 driver instance
 * @param config Configuration
 * @return Handle or NULL on error
 */
hc595_handle_t hc595_create(const hc595_config_t *config);

/**
 * @brief Delete HC595 driver instance
 * @param handle Driver handle
 */
void hc595_delete(hc595_handle_t handle);

/**
 * @brief Initialize HC595
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int hc595_init(hc595_handle_t handle);

/**
 * @brief Set single output state
 * @param handle Driver handle
 * @param bit Bit number (0 to num_chips*8-1)
 * @param state true = high, false = low
 * @return 0 on success, negative on error
 */
int hc595_set_bit(hc595_handle_t handle, uint8_t bit, bool state);

/**
 * @brief Get single output state
 * @param handle Driver handle
 * @param bit Bit number
 * @param state Output state
 * @return 0 on success, negative on error
 */
int hc595_get_bit(hc595_handle_t handle, uint8_t bit, bool *state);

/**
 * @brief Set all outputs at once
 * @param handle Driver handle
 * @param value 8/16/24/32-bit value (depends on num_chips)
 * @return 0 on success, negative on error
 */
int hc595_set_value(hc595_handle_t handle, uint32_t value);

/**
 * @brief Get current output value
 * @param handle Driver handle
 * @param value Output value
 * @return 0 on success, negative on error
 */
int hc595_get_value(hc595_handle_t handle, uint32_t *value);

/**
 * @brief Clear all outputs (set to 0)
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int hc595_clear(hc595_handle_t handle);

/**
 * @brief Set all outputs (set to 1)
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int hc595_set_all(hc595_handle_t handle);

/**
 * @brief Update outputs (latch data to outputs)
 * Call this after set_bit/set_value to actually output the data
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int hc595_update(hc595_handle_t handle);

/**
 * @brief Set and update in one call
 * @param handle Driver handle
 * @param value Output value
 * @return 0 on success, negative on error
 */
int hc595_write(hc595_handle_t handle, uint32_t value);

/**
 * @brief Set single bit and update
 * @param handle Driver handle
 * @param bit Bit number
 * @param state true = high, false = low
 * @return 0 on success, negative on error
 */
int hc595_write_bit(hc595_handle_t handle, uint8_t bit, bool state);

#ifdef __cplusplus
}
#endif
