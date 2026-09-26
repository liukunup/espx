/**
 * @file button.c
 * @brief Button input driver
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

typedef struct {
    int gpio;
    int active_level;
    bool pullup;
    bool current_state;
    int64_t last_change;
    int64_t last_publish;
    bool last_pressed;  // for press events
} button_data_t;

static bool button_isr_service_installed = false;

static void IRAM_ATTR button_isr_handler(void *arg)
{
    // ISR - just note the change; actual debounce in tick
    device_t *dev = (device_t*)arg;
    button_data_t *data = (button_data_t*)dev->driver_data;
    if (data == NULL) return;
    data->last_change = esp_timer_get_time();
}

static esp_err_t button_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *gpio_node = cJSON_GetObjectItem(config, "gpio");
    cJSON *active = cJSON_GetObjectItem(config, "active_level");
    cJSON *pullup = cJSON_GetObjectItem(config, "pullup");

    if (!cJSON_IsNumber(gpio_node)) {
        return ESP_ERR_INVALID_ARG;
    }

    button_data_t *data = calloc(1, sizeof(button_data_t));
    if (data == NULL) {
        return ESP_ERR_NO_MEM;
    }

    data->gpio = gpio_node->valueint;
    data->active_level = cJSON_IsNumber(active) ? active->valueint : 0;
    data->pullup = !cJSON_IsFalse(pullup);
    data->current_state = false;
    data->last_change = esp_timer_get_time();
    data->last_publish = 0;
    data->last_pressed = false;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << data->gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = data->pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = data->pullup ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io);

    if (!button_isr_service_installed) {
        gpio_install_isr_service(0);
        button_isr_service_installed = true;
    }
    gpio_isr_handler_add(data->gpio, button_isr_handler, dev);

    dev->driver_data = data;
    ESP_LOGI(TAG, "Button initialized on GPIO %d (active=%d)", data->gpio, data->active_level);

    return ESP_OK;
}

static esp_err_t button_deinit(device_t *dev)
{
    button_data_t *data = (button_data_t*)dev->driver_data;
    if (data == NULL) return ESP_OK;

    gpio_isr_handler_remove(data->gpio);
    free(data);
    dev->driver_data = NULL;
    return ESP_OK;
}

static esp_err_t button_read(device_t *dev, cJSON *value)
{
    button_data_t *data = (button_data_t*)dev->driver_data;
    if (data == NULL) return ESP_ERR_INVALID_STATE;

    bool pressed = (gpio_get_level(data->gpio) == data->active_level);
    cJSON_AddBoolToObject(value, "pressed", pressed);

    return ESP_OK;
}

static esp_err_t button_tick(device_t *dev)
{
    button_data_t *data = (button_data_t*)dev->driver_data;
    if (data == NULL) return ESP_OK;

    int64_t now = esp_timer_get_time();
    int64_t debounce_ms = 50 * 1000;

    // Debounce
    if (now - data->last_change < debounce_ms) {
        return ESP_OK;
    }

    bool pressed = (gpio_get_level(data->gpio) == data->active_level);

    if (pressed != data->current_state) {
        data->current_state = pressed;
        cJSON *value = cJSON_CreateObject();
        cJSON_AddBoolToObject(value, "pressed", pressed);
        event_bus_publish(EVENT_DEVICE_VALUE_CHANGED, dev->id, value);
        cJSON_Delete(value);
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
