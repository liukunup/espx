/**
 * @file node_config.c
 * @brief Node configuration implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <nvs.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <cJSON.h>

#include "app_info.h"
#include "node_config.h"
#include "event_bus.h"
#include "nvs_utils.h"
#include "json_utils.h"
#include "str_utils.h"

static const char *TAG = "node_config";

#define NVS_NAMESPACE "espx_node"
#define NVS_KEY_CONFIG "config"

static char g_device_id[32] = {0};
static char g_name[64] = {0};
static cJSON *g_config = NULL;

esp_err_t node_config_init(void)
{
    // Generate default device id from MAC (lowercase)
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(g_device_id, sizeof(g_device_id), "espx-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Default name: Node-{last 6 of mac, uppercase} e.g. Node-772E74
    snprintf(g_name, sizeof(g_name), "Node-%02X%02X%02X",
             mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "Node config initialized, device_id: %s", g_device_id);
    return ESP_OK;
}

esp_err_t node_config_load(void)
{
    if (g_config != NULL) {
        cJSON_Delete(g_config);
        g_config = NULL;
    }

    /* A missing namespace/key just means "never configured": not an error. */
    esp_err_t err = nvs_load_json(NVS_NAMESPACE, NVS_KEY_CONFIG, &g_config);
    if (err == ESP_ERR_INVALID_STATE) {
        /* Corrupt stored document: fall back to defaults rather than abort. */
        ESP_LOGW(TAG, "stored node config is not valid JSON, using defaults");
    } else if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "failed to load node config: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    if (g_config != NULL) {
        cJSON *node = cJSON_GetObjectItem(g_config, "node");
        if (cJSON_IsObject(node)) {
            const char *id = json_get_string(node, "device_id", NULL);
            const char *name = json_get_string(node, "name", NULL);

            if (id != NULL) {
                str_copy(g_device_id, sizeof(g_device_id), id);
            }
            if (name != NULL) {
                str_copy(g_name, sizeof(g_name), name);
            }
        }
    }

    return ESP_OK;
}

esp_err_t node_config_save(void)
{
    if (g_config == NULL) {
        g_config = cJSON_CreateObject();
    }

    cJSON *node = cJSON_GetObjectItem(g_config, "node");
    if (!cJSON_IsObject(node)) {
        node = cJSON_AddObjectToObject(g_config, "node");
    }

    cJSON_ReplaceItemInObject(node, "device_id", cJSON_CreateString(g_device_id));
    cJSON_ReplaceItemInObject(node, "name", cJSON_CreateString(g_name));
    cJSON_ReplaceItemInObject(node, "fw_version", cJSON_CreateString(app_version()));

    esp_err_t err = nvs_save_json(NVS_NAMESPACE, NVS_KEY_CONFIG, g_config);
    ESP_LOGI(TAG, "node_config_save: result=%s", esp_err_to_name(err));
    return err;
}

cJSON* node_config_get(void)
{
    if (g_config == NULL) {
        g_config = cJSON_CreateObject();
        cJSON *node = cJSON_AddObjectToObject(g_config, "node");
        // Strip "espx-" prefix for API responses
        const char *id = g_device_id;
        if (strncmp(id, "espx-", 5) == 0) {
            id += 5;
        }
        cJSON_AddStringToObject(node, "device_id", id);
        cJSON_AddStringToObject(node, "name", g_name);
        cJSON_AddStringToObject(node, "fw_version", app_version());
    }
    return cJSON_Duplicate(g_config, true);
}

esp_err_t node_config_set(const cJSON *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_config != NULL) {
        cJSON_Delete(g_config);
    }
    g_config = cJSON_Duplicate(config, true);

    cJSON *node = cJSON_GetObjectItem(g_config, "node");
    if (cJSON_IsObject(node)) {
        const char *id = json_get_string(node, "device_id", NULL);
        const char *name = json_get_string(node, "name", NULL);

        if (id != NULL) {
            str_copy(g_device_id, sizeof(g_device_id), id);
        }
        if (name != NULL) {
            str_copy(g_name, sizeof(g_name), name);
        }
    }

    esp_err_t err = node_config_save();
    if (err == ESP_OK) {
        event_bus_publish(EVENT_CONFIG_CHANGED, NULL, NULL);
    }
    return err;
}

const char* node_config_get_device_id(void)
{
    return g_device_id;
}

const char* node_config_get_name(void)
{
    return g_name;
}
