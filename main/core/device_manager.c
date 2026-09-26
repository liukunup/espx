/**
 * @file device_manager.c
 * @brief Runtime device instance manager implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>
#include <cJSON.h>

#include "device_manager.h"
#include "device_type.h"
#include "event_bus.h"
#include "node_config.h"

static const char *TAG = "device_manager";

#define MAX_DEVICES 32
#define NVS_NAMESPACE "espx_devices"
#define NVS_KEY_CONFIG "config"

static struct device *g_devices[MAX_DEVICES];
static size_t g_device_count = 0;
static TaskHandle_t g_tick_task = NULL;
static bool g_running = false;

/**
 * @brief Periodic tick task - polls devices with periodic capability
 */
static void tick_task(void *arg)
{
    while (g_running) {
        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms tick

        for (size_t i = 0; i < g_device_count; i++) {
            struct device *dev = g_devices[i];
            if (dev == NULL || !dev->enabled || !dev->initialized) {
                continue;
            }

            if (dev->type->tick != NULL) {
                dev->type->tick(dev);
            }
        }
    }

    g_tick_task = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief Find device index by id
 */
static int find_device_index(const char *id)
{
    if (id == NULL) {
        return -1;
    }
    for (size_t i = 0; i < g_device_count; i++) {
        if (g_devices[i] != NULL && strcmp(g_devices[i]->id, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Deinit and free a device
 */
static void free_device(struct device *dev)
{
    if (dev == NULL) return;

    if (dev->initialized && dev->type != NULL && dev->type->deinit != NULL) {
        dev->type->deinit(dev);
    }

    if (dev->config) cJSON_Delete((cJSON*)dev->config);
    if (dev->state) cJSON_Delete((cJSON*)dev->state);

    free(dev->driver_data);
    free(dev);
}

/**
 * @brief Initialize a device instance from config
 */
static esp_err_t init_device(struct device *dev)
{
    if (dev == NULL || dev->type == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (dev->type->init == NULL) {
        ESP_LOGE(TAG, "Type '%s' has no init function", dev->type->name);
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = dev->type->init(dev, (const cJSON*)dev->config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init device '%s': %s",
                 dev->id, esp_err_to_name(err));
        return err;
    }

    dev->initialized = true;
    ESP_LOGI(TAG, "Device '%s' (%s) initialized", dev->id, dev->type->name);

    return ESP_OK;
}

esp_err_t device_manager_init(void)
{
    g_device_count = 0;
    memset(g_devices, 0, sizeof(g_devices));

    g_running = true;
    xTaskCreate(tick_task, "dev_tick", 4096, NULL, 1, &g_tick_task);

    ESP_LOGI(TAG, "Device manager initialized");
    return ESP_OK;
}

esp_err_t device_manager_load(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No saved device config");
        return ESP_OK;
    }

    size_t len = 0;
    err = nvs_get_str(nvs, NVS_KEY_CONFIG, NULL, &len);
    if (err != ESP_OK || len == 0) {
        nvs_close(nvs);
        ESP_LOGI(TAG, "No devices to load");
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

    cJSON *root = cJSON_Parse(json_str);
    free(json_str);
    if (root == NULL) {
        return ESP_FAIL;
    }

    cJSON *array = cJSON_GetObjectItem(root, "devices");
    if (cJSON_IsArray(array)) {
        cJSON *item;
        cJSON_ArrayForEach(item, array) {
            cJSON *id = cJSON_GetObjectItem(item, "id");
            cJSON *type = cJSON_GetObjectItem(item, "type");
            cJSON *config = cJSON_GetObjectItem(item, "config");
            cJSON *enabled = cJSON_GetObjectItem(item, "enabled");

            if (!cJSON_IsString(id) || !cJSON_IsString(type)) continue;

            esp_err_t add_err = device_add(id->valuestring, type->valuestring, config);
            if (add_err == ESP_OK && enabled && !cJSON_IsTrue(enabled)) {
                device_set_enabled(id->valuestring, false);
            }
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t device_manager_save(void)
{
    cJSON *array = cJSON_CreateArray();

    for (size_t i = 0; i < g_device_count; i++) {
        struct device *dev = g_devices[i];
        if (dev == NULL) continue;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", dev->id);
        cJSON_AddStringToObject(item, "type", dev->type->name);
        cJSON_AddBoolToObject(item, "enabled", dev->enabled);

        // Deep copy config
        if (dev->config != NULL) {
            cJSON *cfg_copy = cJSON_Duplicate((cJSON*)dev->config, true);
            cJSON_AddItemToObject(item, "config", cfg_copy);
        }

        cJSON_AddItemToArray(array, item);
    }

    char *json_str = cJSON_PrintUnformatted(array);
    cJSON_Delete(array);

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

esp_err_t device_manager_reload(void)
{
    // Deinit all devices
    for (size_t i = 0; i < g_device_count; i++) {
        struct device *dev = g_devices[i];
        if (dev != NULL && dev->initialized && dev->type->deinit) {
            dev->type->deinit(dev);
            dev->initialized = false;
        }
    }

    // Re-init enabled devices
    for (size_t i = 0; i < g_device_count; i++) {
        struct device *dev = g_devices[i];
        if (dev != NULL && dev->enabled) {
            init_device(dev);
        }
    }

    return ESP_OK;
}

esp_err_t device_add(const char *id, const char *type_name, const cJSON *config)
{
    if (id == NULL || type_name == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (find_device_index(id) >= 0) {
        ESP_LOGE(TAG, "Device '%s' already exists", id);
        return ESP_ERR_INVALID_STATE;
    }

    const device_type_t *type = device_type_get(type_name);
    if (type == NULL) {
        ESP_LOGE(TAG, "Unknown device type: %s", type_name);
        return ESP_ERR_NOT_FOUND;
    }

    // Validate config if validator exists
    if (type->validate_config && config != NULL) {
        esp_err_t err = type->validate_config(config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Config validation failed for '%s'", id);
            return err;
        }
    }

    if (g_device_count >= MAX_DEVICES) {
        ESP_LOGE(TAG, "Maximum devices reached");
        return ESP_ERR_NO_MEM;
    }

    struct device *dev = calloc(1, sizeof(struct device));
    if (dev == NULL) {
        return ESP_ERR_NO_MEM;
    }

    strncpy(dev->id, id, sizeof(dev->id) - 1);
    dev->type = type;
    dev->enabled = true;
    dev->config = config ? cJSON_Duplicate(config, true) : NULL;

    g_devices[g_device_count++] = dev;

    // Initialize device
    init_device(dev);

    event_bus_publish(EVENT_DEVICE_ADDED, id, (void*)NULL);

    return ESP_OK;
}

esp_err_t device_remove(const char *id)
{
    int idx = find_device_index(id);
    if (idx < 0) {
        return ESP_ERR_NOT_FOUND;
    }

    free_device(g_devices[idx]);

    // Shift array
    for (size_t i = idx; i < g_device_count - 1; i++) {
        g_devices[i] = g_devices[i + 1];
    }
    g_devices[--g_device_count] = NULL;

    event_bus_publish(EVENT_DEVICE_REMOVED, id, (void*)NULL);

    return ESP_OK;
}

esp_err_t device_set_enabled(const char *id, bool enabled)
{
    struct device *dev = device_get(id);
    if (dev == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    if (dev->enabled == enabled) {
        return ESP_OK;
    }

    dev->enabled = enabled;

    if (enabled && !dev->initialized) {
        init_device(dev);
    } else if (!enabled && dev->initialized) {
        if (dev->type->deinit) {
            dev->type->deinit(dev);
        }
        dev->initialized = false;
    }

    event_bus_publish(EVENT_DEVICE_CHANGED, id, (void*)NULL);

    return ESP_OK;
}

esp_err_t device_update_config(const char *id, const cJSON *config)
{
    struct device *dev = device_get(id);
    if (dev == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    if (dev->type->validate_config && config != NULL) {
        esp_err_t err = dev->type->validate_config(config);
        if (err != ESP_OK) {
            return err;
        }
    }

    if (dev->config) cJSON_Delete((cJSON*)dev->config);
    dev->config = config ? cJSON_Duplicate(config, true) : NULL;

    // Re-initialize device with new config
    if (dev->initialized && dev->type->deinit) {
        dev->type->deinit(dev);
        dev->initialized = false;
    }
    if (dev->enabled) {
        init_device(dev);
    }

    event_bus_publish(EVENT_DEVICE_CHANGED, id, (void*)NULL);

    return ESP_OK;
}

struct device* device_get(const char *id)
{
    int idx = find_device_index(id);
    if (idx < 0) return NULL;
    return g_devices[idx];
}

const struct device* device_get_all(size_t *count)
{
    if (count != NULL) {
        *count = g_device_count;
    }
    return g_devices[0];
}

size_t device_get_count(void)
{
    return g_device_count;
}

esp_err_t device_read(const char *id, cJSON *value)
{
    struct device *dev = device_get(id);
    if (dev == NULL) return ESP_ERR_NOT_FOUND;

    if (!dev->enabled || !dev->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (dev->type->read == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    return dev->type->read(dev, value);
}

esp_err_t device_read_all(cJSON *result)
{
    cJSON *sensors = cJSON_AddObjectToObject(result, "sensors");
    cJSON *attrs = cJSON_AddObjectToObject(result, "attrs");

    for (size_t i = 0; i < g_device_count; i++) {
        struct device *dev = g_devices[i];
        if (dev == NULL || !dev->enabled || !dev->initialized) continue;

        cJSON *value = cJSON_CreateObject();
        if (dev->type->read != NULL) {
            esp_err_t err = dev->type->read(dev, value);
            if (err != ESP_OK) {
                cJSON_Delete(value);
                continue;
            }
        } else {
            continue;
        }

        // Categorize based on capability
        if (dev->type->capabilities & DEVICE_CAPABILITY_PERIODIC) {
            cJSON_AddItemToObject(sensors, dev->id, value);
        } else {
            cJSON_AddItemToObject(attrs, dev->id, value);
        }
    }

    return ESP_OK;
}

esp_err_t device_write(const char *id, const cJSON *value)
{
    struct device *dev = device_get(id);
    if (dev == NULL) return ESP_ERR_NOT_FOUND;

    if (!dev->enabled || !dev->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (dev->type->write == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = dev->type->write(dev, value);

    if (err == ESP_OK) {
        // Cache the value in state
        if (dev->state) cJSON_Delete((cJSON*)dev->state);
        dev->state = cJSON_Duplicate(value, true);
        event_bus_publish(EVENT_DEVICE_VALUE_CHANGED, id, (void*)value);
    }

    return err;
}

esp_err_t device_get_json_array(cJSON *array)
{
    for (size_t i = 0; i < g_device_count; i++) {
        struct device *dev = g_devices[i];
        if (dev == NULL) continue;

        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", dev->id);
        cJSON_AddStringToObject(item, "type", dev->type->name);
        cJSON_AddStringToObject(item, "description", dev->type->description);
        cJSON_AddBoolToObject(item, "enabled", dev->enabled);
        cJSON_AddNumberToObject(item, "capabilities", dev->type->capabilities);
        cJSON_AddBoolToObject(item, "initialized", dev->initialized);

        if (dev->config != NULL) {
            cJSON *cfg_copy = cJSON_Duplicate((cJSON*)dev->config, true);
            cJSON_AddItemToObject(item, "config", cfg_copy);
        }

        if (dev->state != NULL) {
            cJSON *st_copy = cJSON_Duplicate((cJSON*)dev->state, true);
            cJSON_AddItemToObject(item, "state", st_copy);
        }

        cJSON_AddItemToArray(array, item);
    }

    return ESP_OK;
}

esp_err_t device_get_json(const char *id, cJSON *obj)
{
    struct device *dev = device_get(id);
    if (dev == NULL) return ESP_ERR_NOT_FOUND;

    cJSON_AddStringToObject(obj, "id", dev->id);
    cJSON_AddStringToObject(obj, "type", dev->type->name);
    cJSON_AddStringToObject(obj, "description", dev->type->description);
    cJSON_AddBoolToObject(obj, "enabled", dev->enabled);
    cJSON_AddBoolToObject(obj, "initialized", dev->initialized);

    if (dev->config != NULL) {
        cJSON *cfg = cJSON_Duplicate((cJSON*)dev->config, true);
        cJSON_AddItemToObject(obj, "config", cfg);
    }

    if (dev->state != NULL) {
        cJSON *st = cJSON_Duplicate((cJSON*)dev->state, true);
        cJSON_AddItemToObject(obj, "state", st);
    }

    return ESP_OK;
}
