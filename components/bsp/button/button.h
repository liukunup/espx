/**
 * @file button.h
 * @brief Button/Key Driver with Debounce
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
 * @brief Button active level
 */
typedef enum {
    BUTTON_ACTIVE_LOW = 0,  // Button pressed = low (common with pull-up)
    BUTTON_ACTIVE_HIGH,     // Button pressed = high (common with pull-down)
} button_active_level_t;

/**
 * @brief Button pull mode
 */
typedef enum {
    BUTTON_PULL_NONE = 0,
    BUTTON_PULL_UP,         // Internal pull-up resistor
    BUTTON_PULL_DOWN,       // Internal pull-down resistor
} button_pull_mode_t;

/**
 * @brief Button event type
 */
typedef enum {
    BUTTON_EVENT_PRESSED = 0,   // Button pressed
    BUTTON_EVENT_RELEASED,      // Button released
    BUTTON_EVENT_CLICKED,       // Short click (press + release)
    BUTTON_EVENT_LONG_PRESSED,  // Long press started
    BUTTON_EVENT_LONG_RELEASED, // Long press released
    BUTTON_EVENT_DOUBLE_CLICK,  // Double click detected
} button_event_type_t;

/**
 * @brief Button event structure
 */
typedef struct {
    uint8_t button_id;          // Button identifier
    button_event_type_t type;    // Event type
    uint32_t press_duration_ms; // How long button was pressed
    uint32_t timestamp;         // Event timestamp
} button_event_t;

/**
 * @brief Button configuration
 */
typedef struct {
    uint8_t button_id;              // Unique button identifier
    int8_t gpio_num;                // GPIO pin number (-1 to disable)
    button_active_level_t active_level;
    button_pull_mode_t pull_mode;
    uint32_t debounce_ms;           // Debounce time (default 50ms)
    uint32_t long_press_ms;         // Long press threshold (default 1000ms)
    uint32_t double_click_ms;       // Max time between double clicks (default 300ms)
} button_config_t;

/**
 * @brief Button handle
 */
typedef struct button_handle_s *button_handle_t;

/**
 * @brief Create button driver instance
 * @param config Configuration
 * @return Handle or NULL on error
 */
button_handle_t button_create(const button_config_t *config);

/**
 * @brief Delete button driver instance
 * @param handle Driver handle
 */
void button_delete(button_handle_t handle);

/**
 * @brief Initialize button
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int button_init(button_handle_t handle);

/**
 * @brief Enable button polling (call periodically, e.g., every 10ms)
 * @param handle Driver handle
 */
void button_poll(button_handle_t handle);

/**
 * @brief Check if button event is available (non-blocking)
 * @param handle Driver handle
 * @param event Output event
 * @return 0 if event available, -1 if queue empty
 */
int button_check_event(button_handle_t handle, button_event_t *event);

/**
 * @brief Wait for button event (blocking with timeout)
 * @param handle Driver handle
 * @param event Output event
 * @param timeout_ms Timeout in milliseconds
 * @return 0 if event received, -ESP_ERR_TIMEOUT if timeout
 */
int button_wait_event(button_handle_t handle, button_event_t *event, uint32_t timeout_ms);

/**
 * @brief Register callback for button events
 * @param handle Driver handle
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return 0 on success, negative on error
 */
typedef void (*button_callback_t)(const button_event_t *event, void *user_data);
int button_register_callback(button_handle_t handle, button_callback_t callback, void *user_data);

/**
 * @brief Get current button state
 * @param handle Driver handle
 * @param pressed Output state (true=pressed, false=released)
 * @return 0 on success, negative on error
 */
int button_get_state(button_handle_t handle, bool *pressed);

/**
 * @brief Get number of events in queue
 * @param handle Driver handle
 * @return Number of queued events
 */
uint32_t button_event_queue_size(button_handle_t handle);

/**
 * @brief Clear event queue
 * @param handle Driver handle
 */
void button_clear_queue(button_handle_t handle);

/**
 * @brief Button manager - manages multiple buttons
 */
#define BUTTON_MAX_COUNT 8

/**
 * @brief Button manager configuration
 */
typedef struct {
    uint32_t poll_interval_ms;  // Polling interval (default 10ms)
    uint32_t queue_size;        // Event queue size per button
} button_manager_config_t;

/**
 * @brief Button manager handle
 */
typedef struct button_manager_s *button_manager_handle_t;

/**
 * @brief Create button manager
 * @param config Configuration (NULL for defaults)
 * @return Handle or NULL on error
 */
button_manager_handle_t button_manager_create(const button_manager_config_t *config);

/**
 * @brief Delete button manager
 * @param handle Manager handle
 */
void button_manager_delete(button_manager_handle_t handle);

/**
 * @brief Register a button with manager
 * @param handle Manager handle
 * @param config Button configuration
 * @return Button handle or NULL on error
 */
button_handle_t button_manager_add_button(button_manager_handle_t handle, const button_config_t *config);

/**
 * @brief Remove a button from manager
 * @param handle Manager handle
 * @param button Button handle
 * @return 0 on success, negative on error
 */
int button_manager_remove_button(button_manager_handle_t handle, button_handle_t button);

/**
 * @brief Poll all buttons (call periodically)
 * @param handle Manager handle
 */
void button_manager_poll(button_manager_handle_t handle);

/**
 * @brief Wait for any button event
 * @param handle Manager handle
 * @param event Output event
 * @param timeout_ms Timeout in milliseconds
 * @return 0 if event received, -ESP_ERR_TIMEOUT if timeout
 */
int button_manager_wait_event(button_manager_handle_t handle, button_event_t *event, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
