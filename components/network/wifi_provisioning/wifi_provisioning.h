/**
 * @file provisioning.h
 * @brief AP Provisioning component
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Provisioning state enumeration
 */
typedef enum {
    PROV_STATE_IDLE,
    PROV_STATE_AP_READY,
    PROV_STATE_RECEIVING,
    PROV_STATE_COMPLETE,
    PROV_STATE_FAILED
} provisioning_state_t;

/**
 * @brief WiFi credentials
 */
typedef struct {
    char ssid[32];
    char password[64];
} wifi_cred_t;

/**
 * @brief Initialize provisioning
 *
 * @return 0 on success, negative on error
 */
int provisioning_init(void);

/**
 * @brief Start AP provisioning mode
 *
 * @return 0 on success, negative on error
 */
int provisioning_start(void);

/**
 * @brief Stop provisioning
 *
 * @return 0 on success, negative on error
 */
int provisioning_stop(void);

/**
 * @brief Get provisioning state
 *
 * @return Current state
 */
provisioning_state_t provisioning_get_state(void);

/**
 * @brief Get current AP SSID
 *
 * @return Pointer to SSID string
 */
const char* provisioning_get_ap_ssid(void);

#ifdef __cplusplus
}
#endif
