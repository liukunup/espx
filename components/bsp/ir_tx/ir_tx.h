/**
 * @file ir_tx.h
 * @brief IR Transmitter Driver (NEC/RC5 Protocol)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IR protocol type
 */
typedef enum {
    IR_PROTOCOL_NEC = 0,   // NEC protocol (most common for remotes)
    IR_PROTOCOL_RC5,       // RC5 protocol (Philips)
    IR_PROTOCOL_SONY,      // Sony SIRC protocol
} ir_protocol_t;

/**
 * @brief IR configuration
 */
typedef struct {
    int8_t gpio_num;       // GPIO pin number (-1 to disable)
    bool carrier_enable;   // Enable 38kHz carrier modulation
    uint32_t carrier_freq; // Carrier frequency (default 38000 Hz)
} ir_tx_config_t;

/**
 * @brief IR TX handle
 */
typedef struct ir_tx_handle_s *ir_tx_handle_t;

/**
 * @brief Create IR transmitter driver instance
 * @param config Configuration
 * @return Handle or NULL on error
 */
ir_tx_handle_t ir_tx_create(const ir_tx_config_t *config);

/**
 * @brief Delete IR transmitter driver instance
 * @param handle Driver handle
 */
void ir_tx_delete(ir_tx_handle_t handle);

/**
 * @brief Initialize IR transmitter
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int ir_tx_init(ir_tx_handle_t handle);

/**
 * @brief Send raw IR timing data
 * @param handle Driver handle
 * @param timings Array of timing values (us)
 * @param count Number of timings
 * @return 0 on success, negative on error
 */
int ir_tx_send_raw(ir_tx_handle_t handle, const uint32_t *timings, uint16_t count);

/**
 * @brief Send NEC protocol command
 * @param handle Driver handle
 * @param address Address (8 or 16 bits)
 * @param command Command (8 bits)
 * @return 0 on success, negative on error
 */
int ir_tx_send_nec(ir_tx_handle_t handle, uint16_t address, uint8_t command);

/**
 * @brief Send NEC extended protocol command
 * @param handle Driver handle
 * @param address Address (16 bits)
 * @param command Command (8 bits)
 * @return 0 on success, negative on error
 */
int ir_tx_send_nec_extended(ir_tx_handle_t handle, uint16_t address, uint8_t command);

/**
 * @brief Send RC5 protocol command
 * @param handle Driver handle
 * @param address Address (5 bits)
 * @param command Command (6 bits)
 * @return 0 on success, negative on error
 */
int ir_tx_send_rc5(ir_tx_handle_t handle, uint8_t address, uint8_t command);

/**
 * @brief Send Sony SIRC protocol command
 * @param handle Driver handle
 * @param address Address (7 or 5 bits)
 * @param command Command (7 bits)
 * @param extended Extended command (8 bits, optional, 0 if not used)
 * @return 0 on success, negative on error
 */
int ir_tx_send_sony(ir_tx_handle_t handle, uint8_t address, uint8_t command, uint8_t extended);

/**
 * @brief Send repeat code (NEC only)
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int ir_tx_send_nec_repeat(ir_tx_handle_t handle);

/**
 * @brief Enable/disable carrier modulation
 * @param handle Driver handle
 * @param enable true to enable carrier
 * @return 0 on success, negative on error
 */
int ir_tx_set_carrier(ir_tx_handle_t handle, bool enable);

#ifdef __cplusplus
}
#endif
