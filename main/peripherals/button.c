/**
 * @file button.c
 * @brief GPIO button input driver
 *
 * State is polled from the device-manager tick (100 ms). No ISR is used: an
 * ISR would have to be IRAM-safe, and `esp_timer_get_time()` is not, so a
 * polling debounce is both simpler and safer.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <cJSON.h>

#include "button.h"
#include "device_manager.h"
#include "event_bus.h"

static const char *TAG = "button";

#define BUTTON_DEBOUNCE_MS 50

typedef struct {
    int gpio;
    int active_level;
    bool pullup;
    bool stable_pressed;    /**< debounced state */
    bool last_raw;          /**< last raw sample */
    int stable_ms;          /**< how long the raw sample has been unchanged */
} button_data_t;

static esp_err_t button_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) {
        ESP_LOGE(TAG, "Missing config for '%s'", dev->id);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *gpio_node = cJSON_GetObjectItem(config, "gpio");
    cJSON *active = cJSON_GetObjectItem(config, "active_level");
    cJSON *pullup = cJSON_GetObjectItem(config, "pullup");

    if (!cJSON_IsNumber(gpio_node)) {
        ESP_LOGE(TAG, "Missing 'gpio' for '%s'", dev->id);
        return ESP_ERR_INVALID_ARG;
    }

    button_data_t *data = calloc(1, sizeof(button_data_t));
    if (data == NULL) {
        return ESP_ERR_NO_MEM;
    }

    data->gpio = gpio_node->valueint;
    data->active_level = cJSON_IsNumber(active) ? active->valueint : 0;
    data->pullup = cJSON_IsBool(pullup) ? cJSON_IsTrue(pullup) : true;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << data->gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = data->pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = data->pullup ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        free(data);
        return err;
    }

    data->last_raw = (gpio_get_level(data->gpio) == data->active_level);
    data->stable_pressed = data->last_raw;

    dev->driver_data = data;
    ESP_LOGI(TAG, "Button '%s' on GPIO%d (active=%d, pullup=%d)",
             dev->id, data->gpio, data->active_level, data->pullup);

    return ESP_OK;
}

static esp_err_t button_deinit(device_t *dev)
{
    if (dev->driver_data) {
        free(dev->driver_data);
        dev->driver_data = NULL;
    }
    return ESP_OK;
}

static esp_err_t button_read(device_t *dev, cJSON *value)
{
    button_data_t *data = (button_data_t *)dev->driver_data;
    if (data == NULL) return ESP_ERR_INVALID_STATE;

    cJSON_AddBoolToObject(value, "pressed", data->stable_pressed);
    return ESP_OK;
}

static esp_err_t button_tick(device_t *dev)
{
    button_data_t *data = (button_data_t *)dev->driver_data;
    if (data == NULL) return ESP_OK;

    bool raw = (gpio_get_level(data->gpio) == data->active_level);

    if (raw != data->last_raw) {
        data->last_raw = raw;
        data->stable_ms = 0;
        return ESP_OK;                 /* still bouncing */
    }

    data->stable_ms += 100;            /* tick period */

    if (raw != data->stable_pressed && data->stable_ms >= BUTTON_DEBOUNCE_MS) {
        data->stable_pressed = raw;
        data->stable_ms = 0;

        cJSON *value = cJSON_CreateObject();
        if (value) {
            cJSON_AddBoolToObject(value, "pressed", raw);
            event_bus_publish(EVENT_DEVICE_VALUE_CHANGED, dev->id, value);
            cJSON_Delete(value);
        }

        ESP_LOGI(TAG, "Button '%s' %s", dev->id, raw ? "pressed" : "released");
    }

    return ESP_OK;
}

static esp_err_t button_default_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "gpio", 0);
    cJSON_AddNumberToObject(config, "active_level", 0);
    cJSON_AddBoolToObject(config, "pullup", true);
    return ESP_OK;
}

static const device_type_t button_driver = {
    .name = "button",
    .description = "GPIO input button",
    .capabilities = DEVICE_CAPABILITY_READ | DEVICE_CAPABILITY_NOTIFY,
    .init = button_init,
    .deinit = button_deinit,
    .read = button_read,
    .get_default_config = button_default_config,
    .tick = button_tick,
};

esp_err_t button_driver_register(void)
{
    return device_type_register(&button_driver);
}
