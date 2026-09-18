/**
 * @file config_manager.c
 * @brief Configuration Manager implementation
 */

#include "config_manager.h"
#include <string.h>
#include "nvs_storage.h"
#include "esp_log.h"

static const char *TAG = "config_mgr";

/** @brief NVS key for configuration backup */
#define CONFIG_BACKUP_KEY "config_backup"

/** @brief Current configuration */
static device_config_t g_config = {0};

/** @brief Whether config is loaded */
static bool g_loaded = false;

/**
 * @brief Set default values
 */
static void set_defaults(void) {
    memset(&g_config, 0, sizeof(g_config));

    strncpy(g_config.firmware_version, DEFAULT_FIRMWARE_VERSION,
             sizeof(g_config.firmware_version) - 1);
    g_config.telemetry_interval = DEFAULT_TELEMETRY_INTERVAL;
    g_config.heartbeat_interval = DEFAULT_HEARTBEAT_INTERVAL;
    g_config.reconnect_base_delay = DEFAULT_RECONNECT_BASE_DELAY;
    g_config.reconnect_max_delay = DEFAULT_RECONNECT_MAX_DELAY;
    g_config.mqtt_port = 8883;
    g_config.mqtt_keepalive = 60;
}

int config_manager_init(void) {
    if (g_loaded) {
        ESP_LOGW(TAG, "Config manager already initialized");
        return 0;
    }

    // Initialize NVS
    int ret = nvs_init();
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %d", ret);
        return -1;
    }

    // Set defaults
    set_defaults();

    // Try to load from NVS
    ret = config_manager_load();
    if (ret != 0) {
        ESP_LOGI(TAG, "No saved config found, using defaults");
    }

    g_loaded = true;
    ESP_LOGI(TAG, "Config manager initialized");
    return 0;
}

int config_manager_load(void) {
    // Load string values
    if (nvs_get_string("wifi_ssid", g_config.wifi_ssid, sizeof(g_config.wifi_ssid)) == 0) {
        ESP_LOGI(TAG, "Loaded WiFi SSID: %s", g_config.wifi_ssid);
    }

    // Load WiFi password (encrypted)
    size_t pwd_len = sizeof(g_config.wifi_password);
    if (nvs_get_decrypted("wifi_password", g_config.wifi_password, &pwd_len) != 0) {
        g_config.wifi_password[0] = '\0';
    }

    nvs_get_string("mqtt_broker", g_config.mqtt_broker, sizeof(g_config.mqtt_broker));
    nvs_get_int("mqtt_port", &g_config.mqtt_port);
    nvs_get_string("mqtt_username", g_config.mqtt_username, sizeof(g_config.mqtt_username));
    nvs_get_string("mqtt_client_id", g_config.mqtt_client_id, sizeof(g_config.mqtt_client_id));

    // Load MQTT password (encrypted)
    size_t mqtt_pwd_len = sizeof(g_config.mqtt_password);
    nvs_get_decrypted("mqtt_password", g_config.mqtt_password, &mqtt_pwd_len);

    nvs_get_string("device_id", g_config.device_id, sizeof(g_config.device_id));
    nvs_get_string("device_name", g_config.device_name, sizeof(g_config.device_name));
    nvs_get_string("firmware_version", g_config.firmware_version, sizeof(g_config.firmware_version));

    nvs_get_int("telemetry_interval", &g_config.telemetry_interval);
    nvs_get_int("heartbeat_interval", &g_config.heartbeat_interval);
    nvs_get_int("reconnect_base_delay", &g_config.reconnect_base_delay);
    nvs_get_int("reconnect_max_delay", &g_config.reconnect_max_delay);

    // Load calibration
    size_t cal_len = sizeof(g_config.sensor_calibration);
    nvs_get_blob("sensor_calibration", g_config.sensor_calibration, &cal_len);

    nvs_get_int("config_version", &g_config.config_version);

    // Set defaults if not loaded
    if (g_config.telemetry_interval == 0) {
        g_config.telemetry_interval = DEFAULT_TELEMETRY_INTERVAL;
    }
    if (g_config.heartbeat_interval == 0) {
        g_config.heartbeat_interval = DEFAULT_HEARTBEAT_INTERVAL;
    }
    if (g_config.mqtt_port == 0) {
        g_config.mqtt_port = 8883;
    }

    ESP_LOGI(TAG, "Configuration loaded");
    return 0;
}

int config_manager_save(void) {
    // Save string values
    nvs_set_string("wifi_ssid", g_config.wifi_ssid);
    nvs_set_string("mqtt_broker", g_config.mqtt_broker);
    nvs_set_int("mqtt_port", g_config.mqtt_port);
    nvs_set_string("mqtt_username", g_config.mqtt_username);
    nvs_set_string("mqtt_client_id", g_config.mqtt_client_id);
    nvs_set_string("device_id", g_config.device_id);
    nvs_set_string("device_name", g_config.device_name);
    nvs_set_string("firmware_version", g_config.firmware_version);

    // Save encrypted passwords
    if (strlen(g_config.wifi_password) > 0) {
        nvs_set_encrypted("wifi_password", g_config.wifi_password, strlen(g_config.wifi_password));
    }
    if (strlen(g_config.mqtt_password) > 0) {
        nvs_set_encrypted("mqtt_password", g_config.mqtt_password, strlen(g_config.mqtt_password));
    }

    nvs_set_int("telemetry_interval", g_config.telemetry_interval);
    nvs_set_int("heartbeat_interval", g_config.heartbeat_interval);
    nvs_set_int("reconnect_base_delay", g_config.reconnect_base_delay);
    nvs_set_int("reconnect_max_delay", g_config.reconnect_max_delay);

    // Save calibration
    nvs_set_blob("sensor_calibration", g_config.sensor_calibration, sizeof(g_config.sensor_calibration));

    g_config.config_version++;
    nvs_set_int("config_version", g_config.config_version);

    // Commit
    int ret = nvs_commit();
    if (ret == 0) {
        ESP_LOGI(TAG, "Configuration saved, version %d", g_config.config_version);
    } else {
        ESP_LOGE(TAG, "Failed to save configuration");
    }

    return ret;
}

int config_get_string(const char *key, char *value, size_t len) {
    if (!g_loaded) {
        return -1;
    }
    return nvs_get_string(key, value, len);
}

int config_get_int(const char *key, int *value) {
    if (!g_loaded) {
        return -1;
    }
    return nvs_get_int(key, value);
}

int config_get_float(const char *key, float *value) {
    if (!g_loaded) {
        return -1;
    }
    // NVS doesn't have native float, use blob
    size_t len = sizeof(float);
    return nvs_get_blob(key, value, &len);
}

int config_set_string(const char *key, const char *value) {
    if (!g_loaded) {
        return -1;
    }
    return nvs_set_string(key, value);
}

int config_set_int(const char *key, int value) {
    if (!g_loaded) {
        return -1;
    }
    return nvs_set_int(key, value);
}

int config_set_float(const char *key, float value) {
    if (!g_loaded) {
        return -1;
    }
    return nvs_set_blob(key, &value, sizeof(float));
}

int config_apply_update(const char *json_payload) {
    if (!g_loaded || json_payload == NULL) {
        return -1;
    }

    // Simple JSON parsing (in production, use cJSON or similar)
    // For now, this is a placeholder
    ESP_LOGI(TAG, "Configuration update received (parsing not implemented)");
    return -2;  // Not implemented
}

int config_rollback(void) {
    if (!g_loaded) {
        return -1;
    }

    // Load previous config from backup
    size_t backup_len = sizeof(device_config_t);
    int ret = nvs_get_blob(CONFIG_BACKUP_KEY, &g_config, &backup_len);
    if (ret != 0) {
        ESP_LOGW(TAG, "No backup config found");
        return -2;
    }

    // Apply backup
    ret = nvs_set_blob(CONFIG_BACKUP_KEY, &g_config, sizeof(g_config));
    if (ret == 0) {
        nvs_commit();
        ESP_LOGI(TAG, "Configuration rolled back to version %d", g_config.config_version);
    }

    return ret;
}

int config_export(char *json_buffer, size_t len) {
    if (!g_loaded || json_buffer == NULL || len == 0) {
        return -1;
    }

    int written = snprintf(json_buffer, len,
        "{"
        "\"wifi_ssid\":\"%s\","
        "\"mqtt_broker\":\"%s\","
        "\"mqtt_port\":%d,"
        "\"mqtt_username\":\"%s\","
        "\"mqtt_client_id\":\"%s\","
        "\"device_id\":\"%s\","
        "\"device_name\":\"%s\","
        "\"firmware_version\":\"%s\","
        "\"telemetry_interval\":%d,"
        "\"heartbeat_interval\":%d,"
        "\"reconnect_base_delay\":%d,"
        "\"reconnect_max_delay\":%d,"
        "\"config_version\":%d"
        "}",
        g_config.wifi_ssid,
        g_config.mqtt_broker,
        g_config.mqtt_port,
        g_config.mqtt_username,
        g_config.mqtt_client_id,
        g_config.device_id,
        g_config.device_name,
        g_config.firmware_version,
        g_config.telemetry_interval,
        g_config.heartbeat_interval,
        g_config.reconnect_base_delay,
        g_config.reconnect_max_delay,
        g_config.config_version
    );

    return (written > 0 && written < (int)len) ? 0 : -2;
}

int config_reset(void) {
    if (!g_loaded) {
        return -1;
    }

    nvs_erase_all();
    set_defaults();
    config_manager_save();

    ESP_LOGI(TAG, "Configuration reset to defaults");
    return 0;
}

int config_get_all(device_config_t *config) {
    if (!g_loaded || config == NULL) {
        return -1;
    }

    *config = g_config;
    return 0;
}

int config_set_all(const device_config_t *config) {
    if (!g_loaded || config == NULL) {
        return -1;
    }

    // Backup current config
    nvs_set_blob(CONFIG_BACKUP_KEY, &g_config, sizeof(g_config));

    g_config = *config;
    return config_manager_save();
}

bool config_has_wifi(void) {
    return strlen(g_config.wifi_ssid) > 0;
}

const char* config_get_wifi_ssid(void) {
    return g_config.wifi_ssid;
}

const char* config_get_wifi_password(void) {
    return g_config.wifi_password;
}

const char* config_get_device_id(void) {
    return g_config.device_id;
}
