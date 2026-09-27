/**
 * @file relay.c
 * @brief Relay output driver
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <driver/gpio.h>
#include <esp_log.h>
#include <cJSON.h>

#include "relay.h"
#include "device_manager.h"

static const char *TAG = "relay";

typedef struct {
    int gpio;
    int active_level;
    bool state;
} relay_data_t;

static esp_err_t relay_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *gpio_node = cJSON_GetObjectItem(config, "gpio");
    cJSON *active = cJSON_GetObjectItem(config, "active_level");

    if (!cJSON_IsNumber(gpio_node)) {
        return ESP_ERR_INVALID_ARG;
    }

    relay_data_t *data = calloc(1, sizeof(relay_data_t));
    if (data == NULL) {
        return ESP_ERR_NO_MEM;
    }

    data->gpio = gpio_node->valueint;
    data->active_level = cJSON_IsNumber(active) ? active->valueint : 1;
    data->state = false;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << data->gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    // Set initial state to OFF
    gpio_set_level(data->gpio, data->active_level ? 0 : 1);

    dev->driver_data = data;
    ESP_LOGI(TAG, "Relay initialized on GPIO %d (active=%d)", data->gpio, data->active_level);

    return ESP_OK;
}

static esp_err_t relay_deinit(device_t *dev)
{
    if (dev->driver_data) {
        free(dev->driver_data);
        dev->driver_data = NULL;
    }
    return ESP_OK;
}

static esp_err_t relay_read(device_t *dev, cJSON *value)
{
    relay_data_t *data = (relay_data_t*)dev->driver_data;
    if (data == NULL) return ESP_ERR_INVALID_STATE;

    cJSON_AddBoolToObject(value, "state", data->state);
    return ESP_OK;
}

static esp_err_t relay_write(device_t *dev, const cJSON *value)
{
    relay_data_t *data = (relay_data_t*)dev->driver_data;
    if (data == NULL) return ESP_ERR_INVALID_STATE;

    bool on;
    if (cJSON_IsBool(value)) {
        on = cJSON_IsTrue(value);
    } else if (cJSON_IsObject(value)) {
        cJSON *state = cJSON_GetObjectItem(value, "state");
        if (!cJSON_IsBool(state)) {
            return ESP_ERR_INVALID_ARG;
        }
        on = cJSON_IsTrue(state);
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    gpio_set_level(data->gpio, on ? data->active_level : !data->active_level);
    data->state = on;

    ESP_LOGI(TAG, "Relay '%s' set to %s", dev->id, on ? "ON" : "OFF");
    return ESP_OK;
}

static esp_err_t relay_default_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "gpio", 5);
    cJSON_AddNumberToObject(config, "active_level", 1);
    return ESP_OK;
}

static const device_type_t relay_driver = {
    .name = "relay",
    .description = "GPIO-controlled relay",
    .description_zh = "继电器",
    .save_state = true,
    .capabilities = DEVICE_CAPABILITY_READ | DEVICE_CAPABILITY_WRITE | DEVICE_CAPABILITY_NOTIFY,
    .init = relay_init,
    .deinit = relay_deinit,
    .read = relay_read,
    .write = relay_write,
    .get_default_config = relay_default_config,
};

esp_err_t relay_driver_register(void)
{
    return device_type_register(&relay_driver);
}
