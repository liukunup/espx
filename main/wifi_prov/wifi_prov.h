/* Wi-Fi Provisioning Manager

   This component provides Wi-Fi provisioning functionality using ESP-IDF's
   network_provisioning component. It supports both BLE and SoftAP transport
   methods with configurable security options.

   Features:
   - BLE and SoftAP provisioning transport
   - Security levels 0, 1, 2
   - QR code display for easy provisioning
   - Application callback support
   - Reprovisioning support

   Usage:
   1. Configure parameters in wifi_prov_config.h
   2. Call wifi_prov_start() in your app_main()
   3. Wait for WIFI_PROV_EVENT_CONNECTED event

   This example code is in the Public Domain (or CC0 licensed, at your option.)
*/

#ifndef WIFI_PROV_H
#define WIFI_PROV_H

#include <stdint.h>
#include <stdbool.h>
#include "wifi_prov_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================
 * Transport Types
 * ============================================ */
#define WIFI_PROV_TRANSPORT_SOFTAP 0
#define WIFI_PROV_TRANSPORT_BLE    1

/* ============================================
 * Event Types
 * ============================================ */
typedef enum {
    WIFI_PROV_EVENT_WIFI_CRED_RECV,
    WIFI_PROV_EVENT_WIFI_CRED_SUCCESS,
    WIFI_PROV_EVENT_WIFI_CRED_FAIL,
    WIFI_PROV_EVENT_CONNECTED,
    WIFI_PROV_EVENT_DISCONNECTED,
    WIFI_PROV_EVENT_PROVISIONING_END,
} wifi_prov_event_t;

/* ============================================
 * Callback Types
 * ============================================ */
typedef void (*wifi_prov_event_cb_t)(wifi_prov_event_t event, void *event_data, void *user_data);

/**
 * @brief Application callback handler structure
 */
typedef struct {
    wifi_prov_event_cb_t event_cb;
    void *user_data;
} wifi_prov_event_handler_t;

/* ============================================
 * Public API
 * ============================================ */

/**
 * @brief Initialize Wi-Fi provisioning
 *
 * This function initializes NVS, TCP/IP stack, event loop, and Wi-Fi.
 * It also registers event handlers for provisioning and Wi-Fi events.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t wifi_prov_init(void);

/**
 * @brief Start Wi-Fi provisioning
 *
 * This function starts the provisioning process. It checks if the device
 * is already provisioned:
 * - If already provisioned: starts Wi-Fi STA mode
 * - If not provisioned: starts provisioning service
 *
 * @param[in] event_handler Optional application event handler (can be NULL)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t wifi_prov_start(wifi_prov_event_handler_t *event_handler);

/**
 * @brief Wait for Wi-Fi connection
 *
 * Blocks until Wi-Fi is connected and IP address is obtained.
 * This should be called after wifi_prov_start().
 */
void wifi_prov_wait_for_connection(void);

/**
 * @brief Deinitialize Wi-Fi provisioning
 *
 * Releases all resources allocated by wifi_prov_init().
 * This function is automatically called when provisioning ends.
 */
void wifi_prov_deinit(void);

/**
 * @brief Reset Wi-Fi provisioning state
 *
 * Resets the device to factory state, clearing all provisioned credentials.
 * Useful for reprovisioning.
 */
void wifi_prov_reset(void);

/**
 * @brief Check if device is already provisioned
 *
 * @return true if device has Wi-Fi credentials stored, false otherwise
 */
bool wifi_prov_is_provisioned(void);

/* ============================================
 * Application Callback (Optional)
 * ============================================ */

#if WIFI_PROV_ENABLE_APP_CALLBACK

/**
 * @brief Application callback for provisioning events
 *
 * This is a blocking callback - any configurations that need to be set
 * when a particular provisioning event is triggered can be set here.
 *
 * @param[in] user_data User data passed during initialization
 * @param[in] event The provisioning event that occurred
 * @param[in] event_data Event-specific data
 */
void wifi_prov_app_callback(void *user_data, wifi_prov_event_t event, void *event_data);

extern wifi_prov_event_handler_t wifi_prov_event_handler;

#endif /* WIFI_PROV_ENABLE_APP_CALLBACK */

#ifdef __cplusplus
}
#endif

#endif /* WIFI_PROV_H */
