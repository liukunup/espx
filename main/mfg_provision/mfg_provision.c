/**
 * @file mfg_provision.c
 * @brief Manufacturing provisioning implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>
#include <cJSON.h>

#include "mfg_provision.h"
#include "node_config.h"
#include "device_manager.h"

static const char *TAG = "mfg_provision";

static bool s_partition_ready = false;

esp_err_t mfg_provision_init(void)
{
    if (s_partition_ready) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init_partition(MFG_PARTITION_NAME);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing mfg partition");
        ESP_ERROR_CHECK(nvs_flash_erase_partition(MFG_PARTITION_NAME));
        err = nvs_flash_init_partition(MFG_PARTITION_NAME);
    }

    if (err != ESP_OK) {
        ESP_LOGD(TAG, "mfg partition unavailable: %s", esp_err_to_name(err));
        return err;
    }

    s_partition_ready = true;
    ESP_LOGI(TAG, "Manufacturing partition '%s' ready", MFG_PARTITION_NAME);
    return ESP_OK;
}

bool mfg_provision_has_data(void)
{
    if (!s_partition_ready && mfg_provision_init() != ESP_OK) {
        return false;
    }

    nvs_handle_t nvs;
    if (nvs_open_from_partition(MFG_PARTITION_NAME, MFG_NVS_NAMESPACE,
                                NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }

    size_t len = 0;
    esp_err_t err = nvs_get_str(nvs, MFG_NVS_KEY, NULL, &len);
    nvs_close(nvs);

    return (err == ESP_OK && len > 1);
}

/**
 * @brief Apply node identity + network settings from factory JSON
 */
/**
 * @brief Upsert a string key into a JSON object.
 *
 * cJSON_ReplaceItemInObject() ignores keys that do not already exist, which
 * silently drops first-time settings such as wifi_ssid.
 */
static void json_set_string(cJSON *obj, const char *key, const char *value)
{
    if (obj == NULL || key == NULL || value == NULL) return;
    cJSON_DeleteItemFromObject(obj, key);
    cJSON_AddStringToObject(obj, key, value);
}

static void apply_node_section(cJSON *node)
{
    if (!cJSON_IsObject(node)) return;

    cJSON *current = node_config_get();
    if (current == NULL) {
        current = cJSON_CreateObject();
    }

    cJSON *cur_node = cJSON_GetObjectItem(current, "node");
    if (!cJSON_IsObject(cur_node)) {
        cur_node = cJSON_AddObjectToObject(current, "node");
    }

    const char *keys[] = { "device_id", "name" };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        cJSON *v = cJSON_GetObjectItem(node, keys[i]);
        if (cJSON_IsString(v)) {
            json_set_string(cur_node, keys[i], v->valuestring);
            ESP_LOGI(TAG, "  node.%s = %s", keys[i], v->valuestring);
        }
    }

    /* Network settings are stored under "network" for the app to consume */
    cJSON *network = cJSON_GetObjectItem(node, "network");
    if (cJSON_IsObject(network)) {
        cJSON *cur_net = cJSON_GetObjectItem(current, "network");
        if (!cJSON_IsObject(cur_net)) {
            cur_net = cJSON_AddObjectToObject(current, "network");
        }

        const char *net_keys[] = {
            "mqtt_broker", "mqtt_username", "mqtt_password", "mqtt_topic_prefix",
            "wifi_ssid", "wifi_password",
        };
        for (size_t i = 0; i < sizeof(net_keys) / sizeof(net_keys[0]); i++) {
            cJSON *v = cJSON_GetObjectItem(network, net_keys[i]);
            if (cJSON_IsString(v)) {
                json_set_string(cur_net, net_keys[i], v->valuestring);
                /* Never log credentials */
                if (strstr(net_keys[i], "password") != NULL) {
                    ESP_LOGI(TAG, "  network.%s = <set>", net_keys[i]);
                } else {
                    ESP_LOGI(TAG, "  network.%s = %s", net_keys[i], v->valuestring);
                }
            }
        }
    }

    node_config_set(current);
    cJSON_Delete(current);
}

/**
 * @brief Apply device bindings from factory JSON
 */
static void apply_devices_section(cJSON *devices)
{
    if (!cJSON_IsArray(devices)) return;

    cJSON *dev;
    cJSON_ArrayForEach(dev, devices) {
        cJSON *id = cJSON_GetObjectItem(dev, "id");
        cJSON *type = cJSON_GetObjectItem(dev, "type");
        cJSON *enabled = cJSON_GetObjectItem(dev, "enabled");
        cJSON *config = cJSON_GetObjectItem(dev, "config");

        if (!cJSON_IsString(id) || !cJSON_IsString(type)) {
            ESP_LOGW(TAG, "Skipping device entry without id/type");
            continue;
        }

        /* Replace existing device with the same id */
        if (device_get(id->valuestring) != NULL) {
            device_remove(id->valuestring);
        }

        esp_err_t err = device_add(id->valuestring, type->valuestring, config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add device '%s': %s",
                     id->valuestring, esp_err_to_name(err));
            continue;
        }

        if (cJSON_IsBool(enabled) && !cJSON_IsTrue(enabled)) {
            device_set_enabled(id->valuestring, false);
        }

        ESP_LOGI(TAG, "  device %s (%s)", id->valuestring, type->valuestring);
    }
}

esp_err_t mfg_provision_load(void)
{
    if (!mfg_provision_has_data()) {
        return ESP_ERR_NOT_FOUND;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open_from_partition(MFG_PARTITION_NAME, MFG_NVS_NAMESPACE,
                                             NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    size_t len = 0;
    err = nvs_get_str(nvs, MFG_NVS_KEY, NULL, &len);
    if (err != ESP_OK || len == 0) {
        nvs_close(nvs);
        return ESP_ERR_NOT_FOUND;
    }

    char *json = malloc(len);
    if (json == NULL) {
        nvs_close(nvs);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_str(nvs, MFG_NVS_KEY, json, &len);
    nvs_close(nvs);
    if (err != ESP_OK) {
        free(json);
        return err;
    }

    ESP_LOGI(TAG, "Applying factory configuration");

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (root == NULL) {
        ESP_LOGE(TAG, "Factory data is not valid JSON");
        return ESP_ERR_INVALID_ARG;
    }

    apply_node_section(cJSON_GetObjectItem(root, "node"));
    apply_devices_section(cJSON_GetObjectItem(root, "devices"));

    cJSON_Delete(root);

    /* Persist and consume the factory data so it applies only once */
    device_manager_save();
    mfg_provision_clear();

    ESP_LOGI(TAG, "Factory configuration applied");
    return ESP_OK;
}

esp_err_t mfg_provision_write(const char *json)
{
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_partition_ready && mfg_provision_init() != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    /* Validate */
    cJSON *parsed = cJSON_Parse(json);
    if (parsed == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON_Delete(parsed);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open_from_partition(MFG_PARTITION_NAME, MFG_NVS_NAMESPACE,
                                             NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, MFG_NVS_KEY, json);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Factory data written (%d bytes)", (int)strlen(json));
    }

    return err;
}

esp_err_t mfg_provision_clear(void)
{
    if (!s_partition_ready && mfg_provision_init() != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open_from_partition(MFG_PARTITION_NAME, MFG_NVS_NAMESPACE,
                                             NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(nvs, MFG_NVS_KEY);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Factory data cleared");
    }

    return err;
}
