/**
 * @file device_manager.h
 * @brief Runtime device instance manager
 */

#ifndef DEVICE_MANAGER_H
#define DEVICE_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cJSON;
typedef struct cJSON cJSON;

/**
 * @brief Device instance
 */
struct device {
    char id[32];                 /**< Unique device id */
    const struct device_type *type; /**< Device driver type */
    bool enabled;                /**< Whether device is enabled */
    void *config;                /**< Type-specific config (cJSON) */
    void *state;                 /**< Last known state (cJSON) */
    void *driver_data;           /**< Private driver data */
    bool initialized;            /**< Whether init() has been called */
};

/**
 * @brief Initialize device manager
 */
esp_err_t device_manager_init(void);

/**
 * @brief Load devices from NVS
 *
 * Reads stored config and initializes devices.
 *
 * @return ESP_OK on success
 */
esp_err_t device_manager_load(void);

/**
 * @brief Save devices to NVS
 */
esp_err_t device_manager_save(void);

/**
 * @brief Reload all devices (uninit and re-init)
 *
 * Used after config changes.
 */
esp_err_t device_manager_reload(void);

/**
 * @brief Add a new device
 *
 * @param id Unique device id
 * @param type_name Device type name (must be registered)
 * @param config Type-specific config (cJSON object)
 * @return ESP_OK on success
 */
esp_err_t device_add(const char *id, const char *type_name, const cJSON *config);

/**
 * @brief Remove a device
 */
esp_err_t device_remove(const char *id);

/**
 * @brief Enable or disable a device
 */
esp_err_t device_set_enabled(const char *id, bool enabled);

/**
 * @brief Update device config
 */
esp_err_t device_update_config(const char *id, const cJSON *config);

/**
 * @brief Get device by id
 */
struct device* device_get(const char *id);

/**
 * @brief Get device by index
 *
 * Iterate with:
 *   for (size_t i = 0; i < device_get_count(); i++) {
 *       const struct device *dev = device_get_by_index(i);
 *       ...
 *   }
 *
 * @param index 0 .. device_get_count()-1
 * @return Device pointer, or NULL if out of range
 */
const struct device* device_get_by_index(size_t index);

/**
 * @brief Get number of devices
 */
size_t device_get_count(void);

/**
 * @brief Read device value
 *
 * @param id Device id
 * @param value Output value (cJSON object)
 */
esp_err_t device_read(const char *id, cJSON *value);

/**
 * @brief Read all device values
 */
esp_err_t device_read_all(cJSON *result);

/**
 * @brief Write device value
 */
esp_err_t device_write(const char *id, const cJSON *value);

/**
 * @brief Get all devices as JSON array
 */
esp_err_t device_get_json_array(cJSON *array);

/**
 * @brief Get single device as JSON object
 */
esp_err_t device_get_json(const char *id, cJSON *obj);

#ifdef __cplusplus
}
#endif

#endif // DEVICE_MANAGER_H
