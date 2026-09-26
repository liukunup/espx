/**
 * @file param_store.h
 * @brief Parameter store module for ESPX device
 *
 * Provides centralized parameter management with NVS persistence,
 * type-safe access, and runtime configuration capabilities.
 */

#ifndef PARAM_STORE_H
#define PARAM_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Parameter data types
 */
typedef enum {
    PARAM_TYPE_INT,       /**< 32-bit integer */
    PARAM_TYPE_FLOAT,     /**< 32-bit floating point */
    PARAM_TYPE_STRING,    /**< Null-terminated string */
    PARAM_TYPE_BOOL,      /**< Boolean value */
} param_type_t;

/**
 * @brief Parameter access mode
 */
typedef enum {
    PARAM_ACCESS_RO,      /**< Read-only */
    PARAM_ACCESS_RW,      /**< Read-write */
} param_access_t;

/**
 * @brief Parameter definition structure
 */
typedef struct {
    const char *key;          /**< Parameter key name */
    param_type_t type;        /**< Data type */
    param_access_t access;    /**< Access permission */
    void *value;              /**< Pointer to runtime value */
    const void *default_val;   /**< Default value */
    const void *min;          /**< Minimum value (numeric types) */
    const void *max;          /**< Maximum value (numeric types) */
    const char *description;   /**< Parameter description */
} param_def_t;

/**
 * @brief Initialize parameter store
 *
 * Must be called before any other param_store functions.
 * Initializes NVS and loads persisted values.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t param_store_init(void);

/**
 * @brief Get parameter value
 *
 * @param key Parameter key
 * @param value Output buffer for value
 * @param len Size of output buffer (for strings, includes null terminator)
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if key not found
 */
esp_err_t param_store_get(const char *key, void *value, size_t *len);

/**
 * @brief Set parameter value
 *
 * @param key Parameter key
 * @param value New value
 * @param len Size of value (for strings, includes null terminator)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if read-only or invalid
 */
esp_err_t param_store_set(const char *key, const void *value, size_t len);

/**
 * @brief Save single parameter to NVS
 *
 * @param key Parameter key
 * @return ESP_OK on success, error code on failure
 */
esp_err_t param_store_save(const char *key);

/**
 * @brief Save all RW parameters to NVS
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t param_store_save_all(void);

/**
 * @brief Reset parameter to default value
 *
 * @param key Parameter key
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if key not found
 */
esp_err_t param_store_reset(const char *key);

/**
 * @brief Reset all parameters to defaults
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t param_store_reset_all(void);

/**
 * @brief Get all parameters as JSON string
 *
 * @param json Output buffer for JSON string
 * @param max_len Maximum buffer size
 * @return ESP_OK on success, ESP_ERR_NO_MEM if buffer too small
 */
esp_err_t param_store_get_all(char *json, size_t max_len);

/**
 * @brief Get device unique ID (MAC address)
 *
 * @return Device ID string (do not free)
 */
const char* param_store_get_device_id(void);

/**
 * @brief Batch update parameters from JSON
 *
 * @param json JSON string with key-value pairs
 * @return ESP_OK on success, error code on failure
 */
esp_err_t param_store_batch_set(const char *json);

/**
 * @brief Get parameter type as string
 *
 * @param type Parameter type enum
 * @return Type name string
 */
const char* param_type_to_string(param_type_t type);

/**
 * @brief Get parameter count
 *
 * @return Number of registered parameters
 */
int param_store_get_count(void);

/**
 * @brief Get parameter definition by index
 *
 * @param index Parameter index
 * @return Parameter definition or NULL if out of range
 */
const param_def_t* param_store_get_def(int index);

#ifdef __cplusplus
}
#endif

#endif // PARAM_STORE_H
