/**
 * @file dht11.c
 * @brief DHT11 driver implementation
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
#include <rom/ets_sys.h>

#include "dht11.h"
#include "device_manager.h"
#include "event_bus.h"

static const char *TAG = "dht11";

typedef struct {
    int gpio;
    uint32_t interval_ms;
    int64_t last_read_time;
    float temperature;
    float humidity;
} dht11_data_t;

// DHT11 timing constants (microseconds)
#define DHT11_START_LOW    18000
#define DHT11_RESPONSE     40
#define DHT11_BIT_HIGH_MIN 50
#define DHT11_BIT_HIGH_MAX 70

static int dht11_wait_level(int gpio, int level, int timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(gpio) != level) {
        if ((int)(esp_timer_get_time() - start) > timeout_us) {
            return -1;
        }
    }
    return (int)(esp_timer_get_time() - start);
}

static esp_err_t dht11_read_raw(int gpio, uint8_t data[5])
{
    uint8_t buf[5] = {0};

    // Send start signal
    gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(gpio, 0);
    ets_delay_us(DHT11_START_LOW);
    gpio_set_level(gpio, 1);
    ets_delay_us(30);
    gpio_set_direction(gpio, GPIO_MODE_INPUT);

    // Wait for sensor response
    if (dht11_wait_level(gpio, 0, 100) < 0) return ESP_FAIL;
    if (dht11_wait_level(gpio, 1, 100) < 0) return ESP_FAIL;
    if (dht11_wait_level(gpio, 0, 100) < 0) return ESP_FAIL;

    // Read 40 bits (5 bytes)
    for (int i = 0; i < 40; i++) {
        if (dht11_wait_level(gpio, 1, 100) < 0) return ESP_FAIL;
        int duration = dht11_wait_level(gpio, 0, 100);
        if (duration < 0) return ESP_FAIL;

        // High duration > 50us = 1, < 50us = 0
        buf[i / 8] <<= 1;
        if (duration > DHT11_BIT_HIGH_MIN) {
            buf[i / 8] |= 1;
        }
    }

    // Verify checksum
    if (buf[4] != ((buf[0] + buf[1] + buf[2] + buf[3]) & 0xFF)) {
        return ESP_FAIL;
    }

    memcpy(data, buf, 5);
    return ESP_OK;
}

static esp_err_t dht11_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *gpio_node = cJSON_GetObjectItem(config, "gpio");
    cJSON *interval = cJSON_GetObjectItem(config, "interval_ms");

    if (!cJSON_IsNumber(gpio_node)) {
        ESP_LOGE(TAG, "Missing gpio in config");
        return ESP_ERR_INVALID_ARG;
    }

    dht11_data_t *data = calloc(1, sizeof(dht11_data_t));
    if (data == NULL) {
        return ESP_ERR_NO_MEM;
    }

    data->gpio = gpio_node->valueint;
    data->interval_ms = cJSON_IsNumber(interval) ? (uint32_t)interval->valueint : 5000;
    data->last_read_time = 0;
    data->temperature = 0;
    data->humidity = 0;

    // Configure GPIO
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << data->gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    dev->driver_data = data;
    ESP_LOGI(TAG, "DHT11 initialized on GPIO %d", data->gpio);

    return ESP_OK;
}

static esp_err_t dht11_deinit(device_t *dev)
{
    if (dev->driver_data) {
        free(dev->driver_data);
        dev->driver_data = NULL;
    }
    return ESP_OK;
}

static esp_err_t dht11_read(device_t *dev, cJSON *value)
{
    dht11_data_t *data = (dht11_data_t*)dev->driver_data;
    if (data == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t raw[5];
    esp_err_t err = dht11_read_raw(data->gpio, raw);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "DHT11 read failed");
        return err;
    }

    data->humidity = raw[0] + raw[1] * 0.1f;
    data->temperature = raw[2] + raw[3] * 0.1f;
    data->last_read_time = esp_timer_get_time();

    cJSON_AddNumberToObject(value, "temperature", data->temperature);
    cJSON_AddNumberToObject(value, "humidity", data->humidity);

    return ESP_OK;
}

static esp_err_t dht11_tick(device_t *dev)
{
    dht11_data_t *data = (dht11_data_t*)dev->driver_data;
    if (data == NULL) return ESP_OK;

    int64_t now = esp_timer_get_time();
    if (now - data->last_read_time < (int64_t)data->interval_ms * 1000) {
        return ESP_OK;
    }

    cJSON *value = cJSON_CreateObject();
    if (dht11_read(dev, value) == ESP_OK) {
        event_bus_publish(EVENT_DEVICE_VALUE_CHANGED, dev->id, value);
    }
    cJSON_Delete(value);

    return ESP_OK;
}

static esp_err_t dht11_default_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "gpio", 4);
    cJSON_AddNumberToObject(config, "interval_ms", 5000);
    return ESP_OK;
}

static const device_type_t dht11_driver = {
    .name = "dht11",
    .description = "DHT11 temperature & humidity sensor",
    .description_zh = "DHT11 温湿度传感器",
    .save_state = false,
    .capabilities = DEVICE_CAPABILITY_READ | DEVICE_CAPABILITY_PERIODIC,
    .init = dht11_init,
    .deinit = dht11_deinit,
    .read = dht11_read,
    .get_default_config = dht11_default_config,
    .tick = dht11_tick,
};

esp_err_t dht11_driver_register(void)
{
    return device_type_register(&dht11_driver);
}
