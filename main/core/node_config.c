/**
 * @file node_config.c
 * @brief Node configuration implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_system.h>
#include <cJSON.h>

#include "app_info.h"
#include "node_config.h"

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
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return ESP_OK;
    }

    size_t len = 0;
    err = nvs_get_str(nvs, NVS_KEY_CONFIG, NULL, &len);
    if (err != ESP_OK || len == 0) {
        nvs_close(nvs);
        return ESP_OK;
    }

    char *json_str = malloc(len);
    if (json_str == NULL) {
        nvs_close(nvs);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_str(nvs, NVS_KEY_CONFIG, json_str, &len);
    nvs_close(nvs);
    if (err != ESP_OK) {
        free(json_str);
        return err;
    }

    if (g_config != NULL) {
        cJSON_Delete(g_config);
    }
    g_config = cJSON_Parse(json_str);
    free(json_str);

    if (g_config != NULL) {
        cJSON *node = cJSON_GetObjectItem(g_config, "node");
        if (cJSON_IsObject(node)) {
            cJSON *id = cJSON_GetObjectItem(node, "device_id");
            cJSON *name = cJSON_GetObjectItem(node, "name");

            if (cJSON_IsString(id)) {
                strncpy(g_device_id, id->valuestring, sizeof(g_device_id) - 1);
            }
            if (cJSON_IsString(name)) {
                strncpy(g_name, name->valuestring, sizeof(g_name) - 1);
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

    char *json_str = cJSON_PrintUnformatted(g_config);
    if (json_str == NULL) {
        return ESP_ERR_NO_MEM;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        free(json_str);
        return err;
    }

    err = nvs_set_str(nvs, NVS_KEY_CONFIG, json_str);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    free(json_str);

    return err;
}

cJSON* node_config_get(void)
{
    if (g_config == NULL) {
        g_config = cJSON_CreateObject();
        cJSON *node = cJSON_AddObjectToObject(g_config, "node");
        cJSON_AddStringToObject(node, "device_id", g_device_id);
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
        cJSON *id = cJSON_GetObjectItem(node, "device_id");
        cJSON *name = cJSON_GetObjectItem(node, "name");

        if (cJSON_IsString(id)) {
            strncpy(g_device_id, id->valuestring, sizeof(g_device_id) - 1);
        }
        if (cJSON_IsString(name)) {
            strncpy(g_name, name->valuestring, sizeof(g_name) - 1);
        }
    }

    return node_config_save();
}

const char* node_config_get_device_id(void)
{
    return g_device_id;
}

const char* node_config_get_name(void)
{
    return g_name;
}
