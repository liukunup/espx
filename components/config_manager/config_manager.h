/**
 * @file config_manager.h
 * @brief Configuration Manager component
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Device configuration structure
 */
typedef struct {
    // WiFi configuration
    char wifi_ssid[32];
    char wifi_password[64];

    // MQTT configuration
    char mqtt_broker[128];
    int mqtt_port;
    char mqtt_username[32];
    char mqtt_password[64];
    int mqtt_keepalive;
    char mqtt_client_id[32];

    // Device information
    char device_id[32];
    char device_name[64];
    char firmware_version[16];

    // Runtime configuration
    int telemetry_interval;
    int heartbeat_interval;
    int reconnect_base_delay;
    int reconnect_max_delay;

    // Calibration
    float sensor_calibration[8];

    // Version
    int config_version;
    int config_checksum;
} device_config_t;

/** @brief Default configuration values */
#define DEFAULT_FIRMWARE_VERSION "1.0.0"
#define DEFAULT_TELEMETRY_INTERVAL 60000    // 60 seconds
#define DEFAULT_HEARTBEAT_INTERVAL 30000    // 30 seconds
#define DEFAULT_RECONNECT_BASE_DELAY 1000   // 1 second
#define DEFAULT_RECONNECT_MAX_DELAY 60000   // 60 seconds

/**
 * @brief Initialize configuration manager
 *
 * @return 0 on success, negative on error
 */
int config_manager_init(void);

/**
 * @brief Load configuration from NVS
 *
 * @return 0 on success, negative on error
 */
int config_manager_load(void);

/**
 * @brief Save configuration to NVS
 *
 * @return 0 on success, negative on error
 */
int config_manager_save(void);

/**
 * @brief Get string configuration value
 *
 * @param key Key name
 * @param value Output buffer
 * @param len Buffer length
 * @return 0 on success, negative on error
 */
int config_get_string(const char *key, char *value, size_t len);

/**
 * @brief Get integer configuration value
 *
 * @param key Key name
 * @param value Output pointer
 * @return 0 on success, negative on error
 */
int config_get_int(const char *key, int *value);

/**
 * @brief Get float configuration value
 *
 * @param key Key name
 * @param value Output pointer
 * @return 0 on success, negative on error
 */
int config_get_float(const char *key, float *value);

/**
 * @brief Set string configuration value
 *
 * @param key Key name
 * @param value String value
 * @return 0 on success, negative on error
 */
int config_set_string(const char *key, const char *value);

/**
 * @brief Set integer configuration value
 *
 * @param key Key name
 * @param value Integer value
 * @return 0 on success, negative on error
 */
int config_set_int(const char *key, int value);

/**
 * @brief Set float configuration value
 *
 * @param key Key name
 * @param value Float value
 * @return 0 on success, negative on error
 */
int config_set_float(const char *key, float value);

/**
 * @brief Apply configuration update from JSON
 *
 * @param json_payload JSON configuration data
 * @return 0 on success, negative on error
 */
int config_apply_update(const char *json_payload);

/**
 * @brief Rollback configuration to previous version
 *
 * @return 0 on success, negative on error
 */
int config_rollback(void);

/**
 * @brief Export current configuration as JSON
 *
 * @param json_buffer Output buffer
 * @param len Buffer length
 * @return 0 on success, negative on error
 */
int config_export(char *json_buffer, size_t len);

/**
 * @brief Reset configuration to defaults
 *
 * @return 0 on success, negative on error
 */
int config_reset(void);

/**
 * @brief Get full device configuration
 *
 * @param config Output configuration structure
 * @return 0 on success, negative on error
 */
int config_get_all(device_config_t *config);

/**
 * @brief Set full device configuration
 *
 * @param config Configuration structure
 * @return 0 on success, negative on error
 */
int config_set_all(const device_config_t *config);

/**
 * @brief Check if WiFi is configured
 *
 * @return true if WiFi SSID is set
 */
bool config_has_wifi(void);

/**
 * @brief Get WiFi SSID
 *
 * @return WiFi SSID string
 */
const char* config_get_wifi_ssid(void);

/**
 * @brief Get WiFi password
 *
 * @return WiFi password string
 */
const char* config_get_wifi_password(void);

/**
 * @brief Get device ID
 *
 * @return Device ID string
 */
const char* config_get_device_id(void);

#ifdef __cplusplus
}
#endif
