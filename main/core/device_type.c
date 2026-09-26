/**
 * @file device_type.c
 * @brief Device driver type registry implementation
 */

#include <string.h>
#include <esp_log.h>
#include "device_type.h"

static const char *TAG = "device_type";

#define MAX_DEVICE_TYPES 16
static const device_type_t *g_types[MAX_DEVICE_TYPES];
static size_t g_type_count = 0;

esp_err_t device_type_registry_init(void)
{
    g_type_count = 0;
    memset(g_types, 0, sizeof(g_types));
    ESP_LOGI(TAG, "Device type registry initialized");
    return ESP_OK;
}

esp_err_t device_type_register(const device_type_t *type)
{
    if (type == NULL || type->name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (g_type_count >= MAX_DEVICE_TYPES) {
        ESP_LOGE(TAG, "Maximum device types reached");
        return ESP_ERR_NO_MEM;
    }

    // Check for duplicate
    for (size_t i = 0; i < g_type_count; i++) {
        if (strcmp(g_types[i]->name, type->name) == 0) {
            ESP_LOGW(TAG, "Device type '%s' already registered", type->name);
            return ESP_OK;
        }
    }

    g_types[g_type_count++] = type;
    ESP_LOGI(TAG, "Registered device type: %s", type->name);

    return ESP_OK;
}

const device_type_t* device_type_get(const char *name)
{
    if (name == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < g_type_count; i++) {
        if (strcmp(g_types[i]->name, name) == 0) {
            return g_types[i];
        }
    }

    return NULL;
}

const device_type_t* device_type_get_all(size_t *count)
{
    if (count != NULL) {
        *count = g_type_count;
    }
    return g_types[0];
}
