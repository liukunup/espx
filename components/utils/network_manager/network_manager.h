/**
 * @file network_manager.h
 * @brief Network Manager component for WiFi connectivity
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Network state enumeration
 */
typedef enum {
    NETWORK_STATE_IDLE,
    NETWORK_STATE_CONNECTING,
    NETWORK_STATE_CONNECTED,
    NETWORK_STATE_DISCONNECTED,
    NETWORK_STATE_FAILED
} network_state_t;

/**
 * @brief Reconnect policy type
 */
typedef enum {
    RECONNECT_POLICY_EXPONENTIAL,
    RECONNECT_POLICY_LINEAR
} reconnect_policy_type_t;

/**
 * @brief Reconnect policy configuration
 */
typedef struct {
    uint32_t base_delay_ms;    /**< Base reconnect delay */
    uint32_t max_delay_ms;     /**< Maximum delay cap */
    uint8_t backoff_factor;    /**< Exponential backoff factor */
    uint32_t max_retries;      /**< Maximum retry count (0 = infinite) */
} reconnect_policy_t;

/** @brief Default reconnect policy */
#define DEFAULT_RECONNECT_POLICY { \
    .base_delay_ms = 1000, \
    .max_delay_ms = 60000, \
    .backoff_factor = 2, \
    .max_retries = 0 \
}

/**
 * @brief Network state change callback
 */
typedef void (*network_state_callback_t)(network_state_t state, void *user_data);

/**
 * @brief Initialize network manager
 *
 * @return 0 on success, negative on error
 */
int network_manager_init(void);

/**
 * @brief Connect to WiFi network
 *
 * @param ssid SSID name
 * @param password Password (can be NULL for open networks)
 * @return 0 on success, negative on error
 */
int network_connect(const char *ssid, const char *password);

/**
 * @brief Disconnect from WiFi
 *
 * @return 0 on success, negative on error
 */
int network_disconnect(void);

/**
 * @brief Reconnect to last used network
 *
 * @return 0 on success, negative on error
 */
int network_reconnect(void);

/**
 * @brief Get current network state
 *
 * @return Current network state
 */
network_state_t network_get_state(void);

/**
 * @brief Get WiFi signal strength
 *
 * @return RSSI value (negative dBm)
 */
int network_get_rssi(void);

/**
 * @brief Get current SSID
 *
 * @param buffer Output buffer
 * @param len Buffer length
 * @return 0 on success, negative on error
 */
int network_get_ssid(char *buffer, size_t len);

/**
 * @brief Register state change callback
 *
 * @param callback Callback function
 * @param user_data User data passed to callback
 * @return 0 on success, negative on error
 */
int network_register_callback(network_state_callback_t callback, void *user_data);

/**
 * @brief Check if connected to WiFi
 *
 * @return true if connected, false otherwise
 */
bool network_is_connected(void);

/**
 * @brief Get current reconnect retry count
 *
 * @return Retry count
 */
uint32_t network_get_retry_count(void);

#ifdef __cplusplus
}
#endif
