/**
 * @file nvs_storage.c
 * @brief NVS Storage implementation
 */

#include "nvs_storage.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "nvs_storage";

/** @brief NVS handle for config namespace */
static nvs_handle_t g_nvs_config = 0;

/** @brief NVS handle for system namespace */
static nvs_handle_t g_nvs_system = 0;

/** @brief Flag indicating if NVS is initialized */
static bool g_initialized = false;

int nvs_init(void) {
    if (g_initialized) {
        ESP_LOGW(TAG, "NVS already initialized");
        return 0;
    }

    // Initialize NVS flash
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return -1;
    }

    // Open config namespace
    err = nvs_open(NVS_NAMESPACE_CONFIG, NVS_READWRITE, &g_nvs_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open config namespace: %s", esp_err_to_name(err));
        return -2;
    }

    // Open system namespace
    err = nvs_open(NVS_NAMESPACE_SYSTEM, NVS_READWRITE, &g_nvs_system);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open system namespace: %s", esp_err_to_name(err));
        nvs_close(g_nvs_config);
        return -3;
    }

    g_initialized = true;
    ESP_LOGI(TAG, "NVS initialized");
    return 0;
}

int nvs_set_string(const char *key, const char *value) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return -1;
    }

    if (key == NULL || value == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -2;
    }

    esp_err_t err = nvs_set_str(g_nvs_config, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set string %s: %s", key, esp_err_to_name(err));
        return -3;
    }

    return 0;
}

int nvs_get_string(const char *key, char *value, size_t len) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return -1;
    }

    if (key == NULL || value == NULL || len == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -2;
    }

    size_t required_size = len;
    esp_err_t err = nvs_get_str(g_nvs_config, key, value, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Key %s not found", key);
        return -4;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get string %s: %s", key, esp_err_to_name(err));
        return -3;
    }

    return 0;
}

int nvs_set_int(const char *key, int value) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return -1;
    }

    if (key == NULL) {
        ESP_LOGE(TAG, "Invalid key");
        return -2;
    }

    esp_err_t err = nvs_set_i32(g_nvs_config, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set int %s: %s", key, esp_err_to_name(err));
        return -3;
    }

    return 0;
}

int nvs_get_int(const char *key, int *value) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return -1;
    }

    if (key == NULL || value == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -2;
    }

    esp_err_t err = nvs_get_i32(g_nvs_config, key, value);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Key %s not found", key);
        return -4;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get int %s: %s", key, esp_err_to_name(err));
        return -3;
    }

    return 0;
}

int nvs_set_blob(const char *key, const void *value, size_t len) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return -1;
    }

    if (key == NULL || value == NULL || len == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -2;
    }

    esp_err_t err = nvs_set_blob(g_nvs_config, key, value, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set blob %s: %s", key, esp_err_to_name(err));
        return -3;
    }

    return 0;
}

int nvs_get_blob(const char *key, void *value, size_t *len) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return -1;
    }

    if (key == NULL || value == NULL || len == NULL || *len == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -2;
    }

    size_t required_size = *len;
    esp_err_t err = nvs_get_blob(g_nvs_config, key, value, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Key %s not found", key);
        return -4;
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get blob %s: %s", key, esp_err_to_name(err));
        return -3;
    }

    *len = required_size;
    return 0;
}

int nvs_set_encrypted(const char *key, const void *value, size_t len) {
    // For now, use regular blob storage
    // ESP32 flash encryption provides hardware-level encryption
    // When secure boot is enabled, this provides additional protection
    return nvs_set_blob(key, value, len);
}

int nvs_get_decrypted(const char *key, void *value, size_t *len) {
    // Decryption is handled automatically by ESP32 hardware
    return nvs_get_blob(key, value, len);
}

int nvs_erase(const char *key) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return -1;
    }

    if (key == NULL) {
        ESP_LOGE(TAG, "Invalid key");
        return -2;
    }

    esp_err_t err = nvs_erase_key(g_nvs_config, key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase key %s: %s", key, esp_err_to_name(err));
        return -3;
    }

    return 0;
}

int nvs_erase_all(void) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return -1;
    }

    esp_err_t err = nvs_erase_all(g_nvs_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase all: %s", esp_err_to_name(err));
        return -2;
    }

    return 0;
}

int nvs_commit(void) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "NVS not initialized");
        return -1;
    }

    esp_err_t err = nvs_commit(g_nvs_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit: %s", esp_err_to_name(err));
        return -2;
    }

    err = nvs_commit(g_nvs_system);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit system: %s", esp_err_to_name(err));
        return -3;
    }

    return 0;
}
