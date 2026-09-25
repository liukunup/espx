/**
 * @file relay.h
 * @brief Relay Driver
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Relay active level configuration
 */
typedef enum {
    RELAY_ACTIVE_HIGH = 0,  // Relay energized when GPIO high
    RELAY_ACTIVE_LOW,       // Relay energized when GPIO low
} relay_active_level_t;

/**
 * @brief Relay configuration
 */
typedef struct {
    int8_t gpio_num;            // GPIO pin number (-1 to disable)
    relay_active_level_t level; // Active level
    bool default_state;         // Default state on init (false=off, true=on)
} relay_config_t;

/**
 * @brief Relay handle
 */
typedef struct relay_handle_s *relay_handle_t;

/**
 * @brief Create relay driver instance
 * @param config Configuration
 * @return Handle or NULL on error
 */
relay_handle_t relay_create(const relay_config_t *config);

/**
 * @brief Delete relay driver instance
 * @param handle Driver handle
 */
void relay_delete(relay_handle_t handle);

/**
 * @brief Initialize relay
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int relay_init(relay_handle_t handle);

/**
 * @brief Turn relay on (energize)
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int relay_on(relay_handle_t handle);

/**
 * @brief Turn relay off (de-energize)
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int relay_off(relay_handle_t handle);

/**
 * @brief Toggle relay state
 * @param handle Driver handle
 * @return Current state after toggle (true=on, false=off), negative on error
 */
int relay_toggle(relay_handle_t handle);

/**
 * @brief Set relay state
 * @param handle Driver handle
 * @param on true to turn on, false to turn off
 * @return 0 on success, negative on error
 */
int relay_set(relay_handle_t handle, bool on);

/**
 * @brief Get relay state
 * @param handle Driver handle
 * @param state Output state (true=on, false=off)
 * @return 0 on success, negative on error
 */
int relay_get_state(relay_handle_t handle, bool *state);

#ifdef __cplusplus
}
#endif
