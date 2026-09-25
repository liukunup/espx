/**
 * @file ir_rx.h
 * @brief IR Receiver Driver (NEC/RC5 Protocol Decoding)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IR protocol type
 */
typedef enum {
    IR_PROTOCOL_UNKNOWN = 0,
    IR_PROTOCOL_NEC,
    IR_PROTOCOL_NEC_EXTENDED,
    IR_PROTOCOL_RC5,
    IR_PROTOCOL_SONY,
} ir_protocol_t;

/**
 * @brief IR command structure
 */
typedef struct {
    ir_protocol_t protocol;
    uint16_t address;
    uint8_t command;
    uint8_t extended;      // For Sony extended
    uint32_t timestamp;    // Timestamp when received
} ir_command_t;

/**
 * @brief IR RX configuration
 */
typedef struct {
    int8_t gpio_num;           // GPIO pin number (-1 to disable)
    uint32_t idle_timeout_us;  // Timeout to detect end of signal (default 50000)
} ir_rx_config_t;

/**
 * @brief IR RX handle
 */
typedef struct ir_rx_handle_s *ir_rx_handle_t;

/**
 * @brief Create IR receiver driver instance
 * @param config Configuration
 * @return Handle or NULL on error
 */
ir_rx_handle_t ir_rx_create(const ir_rx_config_t *config);

/**
 * @brief Delete IR receiver driver instance
 * @param handle Driver handle
 */
void ir_rx_delete(ir_rx_handle_t handle);

/**
 * @brief Initialize IR receiver
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int ir_rx_init(ir_rx_handle_t handle);

/**
 * @brief Start receiving IR signals
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int ir_rx_start(ir_rx_handle_t handle);

/**
 * @brief Stop receiving IR signals
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int ir_rx_stop(ir_rx_handle_t handle);

/**
 * @brief Check if command is available (non-blocking)
 * @param handle Driver handle
 * @param cmd Output command
 * @return 0 if command available, -1 if queue empty
 */
int ir_rx_check_command(ir_rx_handle_t handle, ir_command_t *cmd);

/**
 * @brief Wait for command (blocking with timeout)
 * @param handle Driver handle
 * @param cmd Output command
 * @param timeout_ms Timeout in milliseconds
 * @return 0 if command received, -ESP_ERR_TIMEOUT if timeout
 */
int ir_rx_wait_command(ir_rx_handle_t handle, ir_command_t *cmd, uint32_t timeout_ms);

/**
 * @brief Register callback for received commands
 * @param handle Driver handle
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return 0 on success, negative on error
 */
typedef void (*ir_rx_callback_t)(const ir_command_t *cmd, void *user_data);
int ir_rx_register_callback(ir_rx_handle_t handle, ir_rx_callback_t callback, void *user_data);

/**
 * @brief Get number of commands in queue
 * @param handle Driver handle
 * @return Number of queued commands
 */
uint32_t ir_rx_queue_size(ir_rx_handle_t handle);

/**
 * @brief Clear command queue
 * @param handle Driver handle
 */
void ir_rx_clear_queue(ir_rx_handle_t handle);

#ifdef __cplusplus
}
#endif
