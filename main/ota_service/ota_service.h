/**
 * @file ota_service.h
 * @brief Delta OTA service (compressed delta firmware update)
 *
 * Downloads a detools-format patch from an HTTP(S) URL, applies it against
 * the currently running firmware, writes the result to the next OTA
 * partition and reboots.
 */

#ifndef OTA_SERVICE_H
#define OTA_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_CONNECTING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_APPLYING,
    OTA_STATE_REBOOTING,
    OTA_STATE_SUCCESS,
    OTA_STATE_FAILED,
} ota_state_t;

typedef struct {
    ota_state_t state;
    int progress;           /**< 0-100 */
    int bytes_read;         /**< patch bytes received */
    int total_size;         /**< patch size, -1 if unknown */
    char url[256];
    char error[128];
    char running_version[32];
} ota_status_t;

/** Progress callback (called from OTA task) */
typedef void (*ota_progress_cb_t)(const ota_status_t *status, void *user_data);

/**
 * @brief Initialize OTA service
 */
esp_err_t ota_service_init(void);

/**
 * @brief Start a delta OTA update from a patch URL
 *
 * Runs in the background. On success the device reboots into the new
 * firmware; the previous slot remains as fallback.
 *
 * @param url  HTTP(S) URL of the .patch file
 * @return ESP_OK if the update task started
 */
esp_err_t ota_service_start(const char *url);

/**
 * @brief Cancel an in-progress update (best effort)
 */
esp_err_t ota_service_cancel(void);

/**
 * @brief Get current OTA status
 */
esp_err_t ota_service_get_status(ota_status_t *status);

/**
 * @brief Check whether an update is currently running
 */
bool ota_service_is_running(void);

/**
 * @brief Register a progress callback
 */
void ota_service_set_progress_cb(ota_progress_cb_t cb, void *user_data);

/**
 * @brief Confirm the running firmware is good (cancels rollback)
 *
 * Call after a successful boot to mark the new firmware as valid.
 */
esp_err_t ota_service_mark_valid(void);

#ifdef __cplusplus
}
#endif

#endif // OTA_SERVICE_H
