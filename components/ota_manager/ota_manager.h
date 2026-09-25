/**
 * @file ota_manager.h
 * @brief OTA Manager component for firmware updates
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA state enumeration
 */
typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_RECEIVING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_WRITING,
    OTA_STATE_REBOOTING,
    OTA_STATE_COMMITTED,
    OTA_STATE_ROLLBACK,
    OTA_STATE_FAILED
} ota_state_t;

/**
 * @brief OTA info structure
 */
typedef struct {
    char target_version[16];
    uint32_t target_size;
    uint8_t  target_sha256[32];
    uint8_t  signature[256];
    int  total_batches;
    int  current_batch;
} ota_info_t;

/**
 * @brief OTA rollout info
 */
typedef struct {
    char target_version[16];
    int  total_devices;
    int  current_batch;
    int  total_batches;
    int  batch_percentage;
    uint32_t start_time;
    uint32_t timeout;
} ota_rollout_t;

/**
 * @brief Initialize OTA manager
 *
 * @return 0 on success, negative on error
 */
int ota_manager_init(void);

/**
 * @brief Handle OTA start command
 *
 * @param json_payload JSON payload with OTA info
 * @return 0 on success, negative on error
 */
int ota_handle_start(const char *json_payload);

/**
 * @brief Handle OTA data (for MQTT OTA)
 *
 * @param topic Topic name
 * @param data Data buffer
 * @param len Data length
 * @return 0 on success, negative on error
 */
int ota_handle_data(const char *topic, const uint8_t *data, int len);

/**
 * @brief Handle OTA end command
 *
 * @param json_payload JSON payload with verification info
 * @return 0 on success, negative on error
 */
int ota_handle_end(const char *json_payload);

/**
 * @brief Start HTTP OTA
 *
 * @param url Firmware URL
 * @return 0 on success, negative on error
 */
int ota_start_http(const char *url);

/**
 * @brief Get current OTA state
 *
 * @return OTA state
 */
ota_state_t ota_get_state(void);

/**
 * @brief Get OTA progress (0-100)
 *
 * @return Progress percentage
 */
int ota_get_progress(void);

/**
 * @brief Get last error code
 *
 * @return Error code
 */
int ota_get_last_error(void);

/**
 * @brief Rollback to previous partition
 *
 * @return 0 on success, negative on error
 */
int ota_rollback(void);

/**
 * @brief Commit the updated partition
 *
 * @return 0 on success, negative on error
 */
int ota_commit(void);

/**
 * @brief Check if version is valid for upgrade
 *
 * @param version Version string
 * @return true if valid
 */
bool ota_is_valid_version(const char *version);

/**
 * @brief Get current firmware version
 *
 * @return Version string
 */
const char* ota_get_current_version(void);

#ifdef __cplusplus
}
#endif
