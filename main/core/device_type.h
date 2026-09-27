/**
 * @file device_type.h
 * @brief Device driver type registry
 */

#ifndef DEVICE_TYPE_H
#define DEVICE_TYPE_H

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration
typedef struct device device_t;

// Forward declaration to avoid including cJSON in public header
struct cJSON;
typedef struct cJSON cJSON;

/**
 * @brief Device value types
 */
typedef enum {
    DEVICE_VALUE_TYPE_BOOL,
    DEVICE_VALUE_TYPE_INT,
    DEVICE_VALUE_TYPE_FLOAT,
    DEVICE_VALUE_TYPE_STRING,
    DEVICE_VALUE_TYPE_BLOB,
    DEVICE_VALUE_TYPE_OBJECT,
} device_value_type_t;

/**
 * @brief Device capabilities
 */
typedef enum {
    DEVICE_CAPABILITY_READ    = (1 << 0),  /**< Can read current value */
    DEVICE_CAPABILITY_WRITE   = (1 << 1),  /**< Can write/set value */
    DEVICE_CAPABILITY_NOTIFY  = (1 << 2),  /**< Can publish value changes */
    DEVICE_CAPABILITY_PERIODIC = (1 << 3), /**< Periodically auto-reads */
} device_capability_t;

/**
 * @brief Device driver type
 *
 * Each device type (dht11, button, relay, etc.) registers one of these.
 */
typedef struct device_type {
    const char *name;
    const char *description;
    const char *description_zh;
    uint32_t capabilities;
    bool save_state;  /**< Whether to persist runtime state to NVS */

    /** Required: Initialize device instance */
    esp_err_t (*init)(device_t *dev, const cJSON *config);

    /** Required: Cleanup device instance */
    esp_err_t (*deinit)(device_t *dev);

    /** Optional: Read current value */
    esp_err_t (*read)(device_t *dev, cJSON *value);

    /** Optional: Write/set value */
    esp_err_t (*write)(device_t *dev, const cJSON *value);

    /** Required: Get default config schema */
    esp_err_t (*get_default_config)(cJSON *config);

    /** Optional: Validate config (return ESP_OK if valid) */
    esp_err_t (*validate_config)(const cJSON *config);

    /** Optional: Periodic tick (called every loop iteration) */
    esp_err_t (*tick)(device_t *dev);
} device_type_t;

/**
 * @brief Register a device type
 *
 * @param type Device type to register
 * @return ESP_OK on success
 */
esp_err_t device_type_register(const device_type_t *type);

/**
 * @brief Get device type by name
 *
 * @param name Type name
 * @return Pointer to type or NULL if not found
 */
const device_type_t* device_type_get(const char *name);

/**
 * @brief Get number of registered device types
 */
size_t device_type_count(void);

/**
 * @brief Get a registered device type by index
 *
 * @param index 0 .. device_type_count()-1
 * @return Type pointer, or NULL if out of range
 */
const device_type_t* device_type_get_by_index(size_t index);

/**
 * @brief Initialize device type registry
 */
esp_err_t device_type_registry_init(void);

#ifdef __cplusplus
}
#endif

#endif // DEVICE_TYPE_H
