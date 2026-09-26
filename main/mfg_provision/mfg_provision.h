/**
 * @file mfg_provision.h
 * @brief Manufacturing Provisioning for ESPX device
 */

#ifndef MFG_PROVISION_H
#define MFG_PROVISION_H

#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if manufacturing data exists
 *
 * @return true if data exists, false otherwise
 */
bool mfg_provision_has_data(void);

/**
 * @brief Load and apply manufacturing data
 *
 * Loads data from mfg_data partition and applies to param_store.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mfg_provision_load(void);

/**
 * @brief Clear manufacturing data after loading
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t mfg_provision_clear(void);

#ifdef __cplusplus
}
#endif

#endif // MFG_PROVISION_H
