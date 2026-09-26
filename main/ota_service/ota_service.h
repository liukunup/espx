/**
 * @file ota_service.h
 * @brief OTA Service for ESPX device
 */

#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA state
 */
typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_CHECKING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_APPLYING,
    OTA_STATE_REBOOTING,
    OTA_STATE_FAILED,
    OTA_STATE_SUCCESS,
} ota_state_t;

/**
 * @brief OTA status
 */
typedef struct {
    ota_state_t state;
    float progress;
    char version[32];
    char error_msg[128];
} ota_status_t;

/**
 * @brief OTA event callback
 */
typedef void (*ota_event_cb_t)(ota_state_t state, float progress, void *user_data);

/**
 * @brief Initialize OTA service
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t ota_service_init(void);

/**
 * @brief Check for OTA updates
 *
 * @return ESP_OK if update available, ESP_FAIL if no update, error code on failure
 */
esp_err_t ota_service_check(void);

/**
 * @brief Start OTA update from URL
 *
 * @param url OTA manifest URL or direct binary URL
 * @return ESP_OK on success, error code on failure
 */
esp_err_t ota_service_start(const char *url);

/**
 * @brief Cancel ongoing OTA update
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t ota_service_cancel(void);

/**
 * @brief Get OTA status
 *
 * @param status Output status structure
 * @return ESP_OK on success, error code on failure
 */
esp_err_t ota_service_get_status(ota_status_t *status);

/**
 * @brief Set OTA event callback
 *
 * @param cb Callback function
 * @param user_data User data passed to callback
 */
void ota_service_set_callback(ota_event_cb_t cb, void *user_data);

#ifdef __cplusplus
}
#endif

#endif // OTA_SERVICE_H
