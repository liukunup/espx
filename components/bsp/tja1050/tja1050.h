/**
 * @file tja1050.h
 * @brief TJA1050 CAN Transceiver Driver
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TJA1050 operating mode
 */
typedef enum {
    TJA1050_MODE_NORMAL = 0,    // Normal mode - CAN active
    TJA1050_MODE_STANDBY,       // Standby mode - low power
    TJA1050_MODE_SILENT,        // Silent mode - listen only
} tja1050_mode_t;

/**
 * @brief TJA1050 configuration
 */
typedef struct {
    int8_t tx_gpio;      // TXD pin GPIO (-1 to skip)
    int8_t rx_gpio;      // RXD pin GPIO (-1 to skip)
    int8_t stb_gpio;     // STB (standby) pin GPIO (-1 to skip)
    int8_t en_gpio;      // EN (enable) pin GPIO (-1 to skip)
    tja1050_mode_t default_mode;
} tja1050_config_t;

/**
 * @brief TJA1050 handle
 */
typedef struct tja1050_handle_s *tja1050_handle_t;

/**
 * @brief Create TJA1050 driver instance
 * @param config Configuration
 * @return Handle or NULL on error
 */
tja1050_handle_t tja1050_create(const tja1050_config_t *config);

/**
 * @brief Delete TJA1050 driver instance
 * @param handle Driver handle
 */
void tja1050_delete(tja1050_handle_t handle);

/**
 * @brief Initialize TJA1050
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int tja1050_init(tja1050_handle_t handle);

/**
 * @brief Set operating mode
 * @param handle Driver handle
 * @param mode Operating mode
 * @return 0 on success, negative on error
 */
int tja1050_set_mode(tja1050_handle_t handle, tja1050_mode_t mode);

/**
 * @brief Get current operating mode
 * @param handle Driver handle
 * @param mode Output mode
 * @return 0 on success, negative on error
 */
int tja1050_get_mode(tja1050_handle_t handle, tja1050_mode_t *mode);

/**
 * @brief Send CAN frame via TXD pin (bit-bang or external CAN controller)
 * @param handle Driver handle
 * @param can_tx_level true = dominant (0), false = recessive (1)
 * @return 0 on success, negative on error
 */
int tja1050_send_bit(tja1050_handle_t handle, bool can_tx_level);

/**
 * @brief Read CAN RXD pin level
 * @param handle Driver handle
 * @param can_rx_level Output level (true = dominant, false = recessive)
 * @return 0 on success, negative on error
 */
int tja1050_read_bit(tja1050_handle_t handle, bool *can_rx_level);

/**
 * @brief Check if TJA1050 is properly powered
 * @param handle Driver handle
 * @param powered Output power status
 * @return 0 on success, negative on error
 */
int tja1050_check_power(tja1050_handle_t handle, bool *powered);

#ifdef __cplusplus
}
#endif
