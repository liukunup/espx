/**
 * @file mfg_provision.c
 * @brief Manufacturing Provisioning implementation
 */

#include <stdio.h>
#include <string.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>

#include "mfg_provision.h"
#include "param_store.h"

static const char *TAG = "mfg_provision";

#define MFG_NVS_NAMESPACE "mfg_data"

static bool g_has_data = false;

/**
 * @brief Check if mfg_data partition has valid data
 */
bool mfg_provision_has_data(void)
{
    nvs_handle_t nvs;
    esp_err_t err;

    // Try to open mfg_data namespace in NVS
    // Note: In production, this would be a separate partition
    err = nvs_open(MFG_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        g_has_data = false;
        return false;
    }

    // Check if there's any data
    char value[256];
    size_t len = sizeof(value);
    err = nvs_get_str(nvs, "mfg_wifi_ssid", value, &len);
    nvs_close(nvs);

    g_has_data = (err == ESP_OK);
    return g_has_data;
}

/**
 * @brief Load manufacturing data and apply to param_store
 */
esp_err_t mfg_provision_load(void)
{
    nvs_handle_t nvs;
    esp_err_t err;
    char value[256];
    size_t len;

    ESP_LOGI(TAG, "Loading manufacturing data...");

    err = nvs_open(MFG_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No manufacturing data found");
        return ESP_ERR_NOT_FOUND;
    }

    // Load Wi-Fi SSID
    len = sizeof(value);
    err = nvs_get_str(nvs, "mfg_wifi_ssid", value, &len);
    if (err == ESP_OK && len > 1) {
        param_store_set("wifi_ssid", value, len);
        param_store_save("wifi_ssid");
        ESP_LOGI(TAG, "Loaded Wi-Fi SSID: %s", value);
    }

    // Load Wi-Fi Password
    len = sizeof(value);
    err = nvs_get_str(nvs, "mfg_wifi_password", value, &len);
    if (err == ESP_OK && len > 1) {
        param_store_set("wifi_password", value, len);
        param_store_save("wifi_password");
        ESP_LOGI(TAG, "Loaded Wi-Fi password");
    }

    // Load MQTT Broker
    len = sizeof(value);
    err = nvs_get_str(nvs, "mfg_mqtt_broker", value, &len);
    if (err == ESP_OK && len > 1) {
        param_store_set("mqtt_broker", value, len);
        param_store_save("mqtt_broker");
        ESP_LOGI(TAG, "Loaded MQTT Broker: %s", value);
    }

    // Load MQTT Username
    len = sizeof(value);
    err = nvs_get_str(nvs, "mfg_mqtt_username", value, &len);
    if (err == ESP_OK && len > 1) {
        param_store_set("mqtt_username", value, len);
        param_store_save("mqtt_username");
        ESP_LOGI(TAG, "Loaded MQTT username");
    }

    // Load MQTT Password
    len = sizeof(value);
    err = nvs_get_str(nvs, "mfg_mqtt_password", value, &len);
    if (err == ESP_OK && len > 1) {
        param_store_set("mqtt_password", value, len);
        param_store_save("mqtt_password");
        ESP_LOGI(TAG, "Loaded MQTT password");
    }

    // Load OTA URL
    len = sizeof(value);
    err = nvs_get_str(nvs, "mfg_ota_url", value, &len);
    if (err == ESP_OK && len > 1) {
        param_store_set("ota_server_url", value, len);
        param_store_save("ota_server_url");
        ESP_LOGI(TAG, "Loaded OTA URL: %s", value);
    }

    nvs_close(nvs);

    // Clear manufacturing data after loading
    mfg_provision_clear();

    ESP_LOGI(TAG, "Manufacturing data loaded successfully");
    return ESP_OK;
}

/**
 * @brief Clear manufacturing data
 */
esp_err_t mfg_provision_clear(void)
{
    nvs_handle_t nvs;
    esp_err_t err;

    err = nvs_open(MFG_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_all(nvs);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }

    nvs_close(nvs);

    g_has_data = false;
    ESP_LOGI(TAG, "Manufacturing data cleared");

    return err;
}
