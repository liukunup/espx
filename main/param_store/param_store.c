/**
 * @file param_store.c
 * @brief Parameter store implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <esp_log.h>
#include <cJSON.h>

#include "param_store.h"

static const char *TAG = "param_store";

// NVS namespace for parameters
#define PARAM_NVS_NAMESPACE "espx_params"

// Maximum string length for parameter values
#define PARAM_STRING_MAX_LEN 256

// Runtime value storage
static char g_device_id[32] = {0};
static char g_wifi_ssid[PARAM_STRING_MAX_LEN] = {0};
static char g_wifi_password[PARAM_STRING_MAX_LEN] = {0};
static char g_mqtt_broker[PARAM_STRING_MAX_LEN] = {0};
static char g_mqtt_username[PARAM_STRING_MAX_LEN] = {0};
static char g_mqtt_password[PARAM_STRING_MAX_LEN] = {0};
static char g_mqtt_client_id[PARAM_STRING_MAX_LEN] = {0};
static char g_mqtt_topic_prefix[PARAM_STRING_MAX_LEN] = {0};
static char g_ota_server_url[PARAM_STRING_MAX_LEN] = {0};
static char g_param_str_001[PARAM_STRING_MAX_LEN] = {0};

static int g_param_int_001 = 0;
static float g_param_float_001 = 0.0f;
static bool g_param_bool_001 = false;

// Build time string
static char g_build_time[64] = {0};

// Parameter definitions array
static param_def_t g_params[] = {
    // System parameters (RO)
    {
        .key = "device_name",
        .type = PARAM_TYPE_STRING,
        .access = PARAM_ACCESS_RO,
        .value = NULL,  // Set at runtime
        .default_val = "ESPX",
        .description = "Device Name"
    },
    {
        .key = "device_id",
        .type = PARAM_TYPE_STRING,
        .access = PARAM_ACCESS_RO,
        .value = g_device_id,
        .default_val = "",
        .description = "Device ID (MAC Address)"
    },
    {
        .key = "chip_model",
        .type = PARAM_TYPE_STRING,
        .access = PARAM_ACCESS_RO,
        .value = "ESP32-S3",
        .default_val = "ESP32-S3",
        .description = "Chip Model"
    },
    {
        .key = "chip_revision",
        .type = PARAM_TYPE_STRING,
        .access = PARAM_ACCESS_RO,
        .value = NULL,  // Set at runtime
        .default_val = "",
        .description = "Chip Revision"
    },
    {
        .key = "firmware_ver",
        .type = PARAM_TYPE_STRING,
        .access = PARAM_ACCESS_RO,
        .value = NULL,  // Set from Kconfig
        .default_val = CONFIG_FIRMWARE_VERSION,
        .description = "Firmware Version"
    },
    {
        .key = "build_time",
        .type = PARAM_TYPE_STRING,
        .access = PARAM_ACCESS_RO,
        .value = g_build_time,
        .default_val = "",
        .description = "Build Time"
    },
    // Runtime parameters (RW)
    {
        .key = "wifi_ssid",
        .type = PARAM_TYPE_STRING,
        .access = PARAM_ACCESS_RW,
        .value = g_wifi_ssid,
        .default_val = "",
        .description = "Wi-Fi SSID"
    },
    {
        .key = "wifi_password",
        .type = PARAM_TYPE_STRING,
        .access = PARAM_ACCESS_RW,
        .value = g_wifi_password,
        .default_val = "",
        .description = "Wi-Fi Password"
    },
    {
        .key = "mqtt_broker",
        .type = PARAM_TYPE_STRING,
        .access = PARAM_ACCESS_RW,
        .value = g_mqtt_broker,
        .default_val = CONFIG_MQTT_BROKER_URL,
        .description = "MQTT Broker URL"
    },
    {
        .key = "mqtt_username",
        .type = PARAM_TYPE_STRING,
        .access = PARAM_ACCESS_RW,
        .value = g_mqtt_username,
        .default_val = "",
        .description = "MQTT Username"
    },
    {
        .key = "mqtt_password",
        .type = PARAM_TYPE_STRING,
        .access = PARAM_ACCESS_RW,
        .value = g_mqtt_password,
        .default_val = "",
        .description = "MQTT Password"
    },
    {
        .key = "mqtt_client_id",
        .type = PARAM_TYPE_STRING,
        .access = PARAM_ACCESS_RW,
        .value = g_mqtt_client_id,
        .default_val = "",
        .description = "MQTT Client ID"
    },
    {
        .key = "mqtt_topic_prefix",
        .type = PARAM_TYPE_STRING,
        .access = PARAM_ACCESS_RW,
        .value = g_mqtt_topic_prefix,
        .default_val = "",
        .description = "MQTT Topic Prefix"
    },
    {
        .key = "ota_server_url",
        .type = PARAM_TYPE_STRING,
        .access = PARAM_ACCESS_RW,
        .value = g_ota_server_url,
        .default_val = CONFIG_OTA_SERVER_URL,
        .description = "OTA Server URL"
    },
    // Business parameters
    {
        .key = "param_int_001",
        .type = PARAM_TYPE_INT,
        .access = PARAM_ACCESS_RW,
        .value = &g_param_int_001,
        .default_val = &(int){0},
        .min = &(int){0},
        .max = &(int){1000},
        .description = "Business Int Parameter 1"
    },
    {
        .key = "param_float_001",
        .type = PARAM_TYPE_FLOAT,
        .access = PARAM_ACCESS_RW,
        .value = &g_param_float_001,
        .default_val = &(float){0.0f},
        .min = &(float){0.0f},
        .max = &(float){1000.0f},
        .description = "Business Float Parameter 1"
    },
    {
        .key = "param_str_001",
        .type = PARAM_TYPE_STRING,
        .access = PARAM_ACCESS_RW,
        .value = g_param_str_001,
        .default_val = "",
        .description = "Business String Parameter 1"
    },
    {
        .key = "param_bool_001",
        .type = PARAM_TYPE_BOOL,
        .access = PARAM_ACCESS_RW,
        .value = &g_param_bool_001,
        .default_val = &(bool){false},
        .description = "Business Bool Parameter 1"
    },
};

#define PARAM_COUNT (sizeof(g_params) / sizeof(g_params[0]))

// Helper: Get parameter by key
static param_def_t* get_param_by_key(const char *key)
{
    for (int i = 0; i < PARAM_COUNT; i++) {
        if (strcmp(g_params[i].key, key) == 0) {
            return &g_params[i];
        }
    }
    return NULL;
}

// Helper: Get string value
static esp_err_t get_string_value(param_def_t *param, char *value, size_t *len)
{
    if (param->type != PARAM_TYPE_STRING) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *src = (const char*)param->value;
    if (src == NULL) {
        src = (const char*)param->default_val;
    }
    size_t src_len = strlen(src) + 1;
    if (*len < src_len) {
        *len = src_len;
        return ESP_ERR_NO_MEM;
    }
    strcpy(value, src);
    *len = src_len;
    return ESP_OK;
}

// Helper: Set string value
static esp_err_t set_string_value(param_def_t *param, const char *value, size_t len)
{
    if (param->type != PARAM_TYPE_STRING) {
        return ESP_ERR_INVALID_ARG;
    }
    if (param->access == PARAM_ACCESS_RO) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t max_len = PARAM_STRING_MAX_LEN;
    if (len > max_len) {
        len = max_len;
    }
    strncpy((char*)param->value, value, max_len - 1);
    ((char*)param->value)[max_len - 1] = '\0';
    return ESP_OK;
}

// Helper: Load parameter from NVS
static esp_err_t load_param_from_nvs(param_def_t *param)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PARAM_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    esp_err_t result = ESP_OK;

    switch (param->type) {
    case PARAM_TYPE_STRING:
        if (param->access == PARAM_ACCESS_RW) {
            size_t len = PARAM_STRING_MAX_LEN;
            err = nvs_get_str(nvs, param->key, (char*)param->value, &len);
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                // Use default value
                const char *def = (const char*)param->default_val;
                if (def) {
                    strncpy((char*)param->value, def, PARAM_STRING_MAX_LEN - 1);
                }
            } else if (err != ESP_OK) {
                result = err;
            }
        }
        break;

    case PARAM_TYPE_INT:
        if (param->access == PARAM_ACCESS_RW) {
            err = nvs_get_i32(nvs, param->key, (int32_t*)param->value);
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                *(int*)param->value = *(int*)param->default_val;
            } else if (err != ESP_OK) {
                result = err;
            }
        }
        break;

    case PARAM_TYPE_FLOAT:
        if (param->access == PARAM_ACCESS_RW) {
            // Store as int (multiply by 1000 for 3 decimal precision)
            int32_t val;
            err = nvs_get_i32(nvs, param->key, &val);
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                *(float*)param->value = *(float*)param->default_val;
            } else if (err == ESP_OK) {
                *(float*)param->value = val / 1000.0f;
            } else {
                result = err;
            }
        }
        break;

    case PARAM_TYPE_BOOL:
        if (param->access == PARAM_ACCESS_RW) {
            uint8_t val;
            err = nvs_get_u8(nvs, param->key, &val);
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                *(bool*)param->value = *(bool*)param->default_val;
            } else if (err == ESP_OK) {
                *(bool*)param->value = (bool)val;
            } else {
                result = err;
            }
        }
        break;
    }

    nvs_close(nvs);
    return result;
}

// Helper: Save parameter to NVS
esp_err_t param_store_save(const char *key)
{
    param_def_t *param = get_param_by_key(key);
    if (param == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    if (param->access == PARAM_ACCESS_RO) {
        return ESP_OK;  // RO params don't need saving
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(PARAM_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    switch (param->type) {
    case PARAM_TYPE_STRING:
        err = nvs_set_str(nvs, param->key, (const char*)param->value);
        break;

    case PARAM_TYPE_INT:
        err = nvs_set_i32(nvs, param->key, *(int*)param->value);
        break;

    case PARAM_TYPE_FLOAT:
        // Store as int (multiply by 1000 for 3 decimal precision)
        err = nvs_set_i32(nvs, param->key, (int32_t)(*(float*)param->value * 1000));
        break;

    case PARAM_TYPE_BOOL:
        err = nvs_set_u8(nvs, param->key, *(bool*)param->value ? 1 : 0);
        break;
    }

    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    return err;
}

// Public API implementation
esp_err_t param_store_init(void)
{
    esp_err_t err;

    // Initialize NVS
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGI(TAG, "Initializing parameter store with %d parameters", PARAM_COUNT);

    // Get MAC address for device ID
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(g_device_id, sizeof(g_device_id), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Generate default MQTT client ID
    snprintf(g_mqtt_client_id, sizeof(g_mqtt_client_id), "espx-%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Generate default MQTT topic prefix
    snprintf(g_mqtt_topic_prefix, sizeof(g_mqtt_topic_prefix), "espx/%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Set build time
    snprintf(g_build_time, sizeof(g_build_time), "%s %s", __DATE__, __TIME__);

    // Load all RW parameters from NVS
    for (int i = 0; i < PARAM_COUNT; i++) {
        if (g_params[i].access == PARAM_ACCESS_RW) {
            err = load_param_from_nvs(&g_params[i]);
            if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
                ESP_LOGW(TAG, "Failed to load param %s: %s", g_params[i].key, esp_err_to_name(err));
            }
        }
    }

    ESP_LOGI(TAG, "Parameter store initialized. Device ID: %s", g_device_id);

    return ESP_OK;
}

esp_err_t param_store_get(const char *key, void *value, size_t *len)
{
    param_def_t *param = get_param_by_key(key);
    if (param == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    switch (param->type) {
    case PARAM_TYPE_STRING:
        return get_string_value(param, (char*)value, len);

    case PARAM_TYPE_INT:
        if (*len < sizeof(int)) {
            *len = sizeof(int);
            return ESP_ERR_NO_MEM;
        }
        *(int*)value = *(int*)param->value;
        *len = sizeof(int);
        break;

    case PARAM_TYPE_FLOAT:
        if (*len < sizeof(float)) {
            *len = sizeof(float);
            return ESP_ERR_NO_MEM;
        }
        *(float*)value = *(float*)param->value;
        *len = sizeof(float);
        break;

    case PARAM_TYPE_BOOL:
        if (*len < sizeof(bool)) {
            *len = sizeof(bool);
            return ESP_ERR_NO_MEM;
        }
        *(bool*)value = *(bool*)param->value;
        *len = sizeof(bool);
        break;
    }

    return ESP_OK;
}

esp_err_t param_store_set(const char *key, const void *value, size_t len)
{
    param_def_t *param = get_param_by_key(key);
    if (param == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    if (param->access == PARAM_ACCESS_RO) {
        return ESP_ERR_INVALID_ARG;
    }

    switch (param->type) {
    case PARAM_TYPE_STRING:
        return set_string_value(param, (const char*)value, len);

    case PARAM_TYPE_INT: {
        int val = *(const int*)value;
        if (param->min && val < *(int*)param->min) {
            val = *(int*)param->min;
        }
        if (param->max && val > *(int*)param->max) {
            val = *(int*)param->max;
        }
        *(int*)param->value = val;
        break;
    }

    case PARAM_TYPE_FLOAT: {
        float val = *(const float*)value;
        if (param->min && val < *(float*)param->min) {
            val = *(float*)param->min;
        }
        if (param->max && val > *(float*)param->max) {
            val = *(float*)param->max;
        }
        *(float*)param->value = val;
        break;
    }

    case PARAM_TYPE_BOOL:
        *(bool*)param->value = *(const bool*)value;
        break;
    }

    return ESP_OK;
}

esp_err_t param_store_save_all(void)
{
    esp_err_t err = ESP_OK;
    for (int i = 0; i < PARAM_COUNT; i++) {
        if (g_params[i].access == PARAM_ACCESS_RW) {
            esp_err_t save_err = param_store_save(g_params[i].key);
            if (save_err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to save %s: %s", g_params[i].key, esp_err_to_name(save_err));
                err = save_err;
            }
        }
    }
    return err;
}

esp_err_t param_store_reset(const char *key)
{
    param_def_t *param = get_param_by_key(key);
    if (param == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    switch (param->type) {
    case PARAM_TYPE_STRING:
        if (param->default_val) {
            strncpy((char*)param->value, (const char*)param->default_val, PARAM_STRING_MAX_LEN - 1);
            ((char*)param->value)[PARAM_STRING_MAX_LEN - 1] = '\0';
        }
        break;

    case PARAM_TYPE_INT:
        *(int*)param->value = *(int*)param->default_val;
        break;

    case PARAM_TYPE_FLOAT:
        *(float*)param->value = *(float*)param->default_val;
        break;

    case PARAM_TYPE_BOOL:
        *(bool*)param->value = *(bool*)param->default_val;
        break;
    }

    return ESP_OK;
}

esp_err_t param_store_reset_all(void)
{
    for (int i = 0; i < PARAM_COUNT; i++) {
        ESP_ERROR_CHECK(param_store_reset(g_params[i].key));
    }
    return ESP_OK;
}

esp_err_t param_store_get_all(char *json, size_t max_len)
{
    cJSON *root = cJSON_CreateObject();

    for (int i = 0; i < PARAM_COUNT; i++) {
        param_def_t *param = &g_params[i];
        const void *value = param->value;

        if (value == NULL) {
            value = param->default_val;
        }

        if (value == NULL) {
            continue;
        }

        switch (param->type) {
        case PARAM_TYPE_STRING:
            cJSON_AddStringToObject(root, param->key, (const char*)value);
            break;

        case PARAM_TYPE_INT:
            cJSON_AddNumberToObject(root, param->key, *(int*)value);
            break;

        case PARAM_TYPE_FLOAT:
            cJSON_AddNumberToObject(root, param->key, *(float*)value);
            break;

        case PARAM_TYPE_BOOL:
            cJSON_AddBoolToObject(root, param->key, *(bool*)value);
            break;
        }
    }

    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    size_t json_len = strlen(json_str);
    if (json_len >= max_len) {
        free(json_str);
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    strcpy(json, json_str);
    free(json_str);
    cJSON_Delete(root);

    return ESP_OK;
}

const char* param_store_get_device_id(void)
{
    return g_device_id;
}

esp_err_t param_store_batch_set(const char *json_str)
{
    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *item;
    cJSON_ArrayForEach(item, root) {
        const char *key = item->string;

        param_def_t *param = get_param_by_key(key);
        if (param == NULL || param->access == PARAM_ACCESS_RO) {
            continue;
        }

        switch (param->type) {
        case PARAM_TYPE_STRING:
            if (cJSON_IsString(item)) {
                set_string_value(param, item->valuestring, strlen(item->valuestring) + 1);
            }
            break;

        case PARAM_TYPE_INT:
            if (cJSON_IsNumber(item)) {
                *(int*)param->value = (int)item->valueint;
            }
            break;

        case PARAM_TYPE_FLOAT:
            if (cJSON_IsNumber(item)) {
                *(float*)param->value = (float)item->valuedouble;
            }
            break;

        case PARAM_TYPE_BOOL:
            if (cJSON_IsBool(item)) {
                *(bool*)param->value = cJSON_IsTrue(item);
            }
            break;
        }

        // Auto-save after setting
        param_store_save(key);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

const char* param_type_to_string(param_type_t type)
{
    switch (type) {
    case PARAM_TYPE_INT: return "int";
    case PARAM_TYPE_FLOAT: return "float";
    case PARAM_TYPE_STRING: return "string";
    case PARAM_TYPE_BOOL: return "bool";
    default: return "unknown";
    }
}

int param_store_get_count(void)
{
    return PARAM_COUNT;
}

const param_def_t* param_store_get_def(int index)
{
    if (index < 0 || index >= PARAM_COUNT) {
        return NULL;
    }
    return &g_params[index];
}
