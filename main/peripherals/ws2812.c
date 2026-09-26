/**
 * @file ws2812.c
 * @brief WS2812 RGB LED strip driver
 *
 * Uses esp-idf led_strip component.
 *
 * Config:
 *   data_gpio:  Data pin (default 48 for ESP32-S3 R16N8)
 *   count:      Number of LEDs (default 1)
 *   brightness: 0-255 (default 255)
 *
 * Write value (object):
 *   {"pixels": [{"r": 255, "g": 0, "b": 0}, ...]}
 *   or
 *   {"all": {"r": 0, "g": 255, "b": 0}}
 *   or
 *   {"index": 0, "r": 255, "g": 0, "b": 0}
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <freertos/FreeRTOS.h>
#include <led_strip.h>
#include <esp_log.h>
#include <cJSON.h>

#include "ws2812.h"
#include "device_manager.h"

static const char *TAG = "ws2812";

typedef struct {
    int data_gpio;
    int count;
    int brightness;
    led_strip_handle_t strip;
    uint8_t *r_buf;
    uint8_t *g_buf;
    uint8_t *b_buf;
} ws2812_data_t;

static esp_err_t apply_pixels(ws2812_data_t *data)
{
    for (int i = 0; i < data->count; i++) {
        led_strip_set_pixel(data->strip, i,
                            data->r_buf[i],
                            data->g_buf[i],
                            data->b_buf[i]);
    }
    return led_strip_refresh(data->strip);
}

static esp_err_t ws2812_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *data_gpio = cJSON_GetObjectItem(config, "data_gpio");
    cJSON *count = cJSON_GetObjectItem(config, "count");
    cJSON *brightness = cJSON_GetObjectItem(config, "brightness");

    ws2812_data_t *data = calloc(1, sizeof(ws2812_data_t));
    if (data == NULL) {
        return ESP_ERR_NO_MEM;
    }

    data->data_gpio = cJSON_IsNumber(data_gpio) ? data_gpio->valueint : 48;
    data->count = cJSON_IsNumber(count) ? count->valueint : 1;
    data->brightness = cJSON_IsNumber(brightness) ? brightness->valueint : 255;

    if (data->count < 1) data->count = 1;
    if (data->count > 300) data->count = 300;  // Safety limit

    data->r_buf = calloc(data->count, sizeof(uint8_t));
    data->g_buf = calloc(data->count, sizeof(uint8_t));
    data->b_buf = calloc(data->count, sizeof(uint8_t));
    if (data->r_buf == NULL || data->g_buf == NULL || data->b_buf == NULL) {
        free(data->r_buf);
        free(data->g_buf);
        free(data->b_buf);
        free(data);
        return ESP_ERR_NO_MEM;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = data->data_gpio,
        .max_leds = data->count,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &data->strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LED strip: %s", esp_err_to_name(err));
        free(data->r_buf);
        free(data->g_buf);
        free(data->b_buf);
        free(data);
        return err;
    }

    // Clear all LEDs
    led_strip_clear(data->strip);

    dev->driver_data = data;
    ESP_LOGI(TAG, "WS2812 initialized: gpio=%d, count=%d", data->data_gpio, data->count);

    return ESP_OK;
}

static esp_err_t ws2812_deinit(device_t *dev)
{
    ws2812_data_t *data = (ws2812_data_t*)dev->driver_data;
    if (data == NULL) return ESP_OK;

    if (data->strip) {
        led_strip_clear(data->strip);
        led_strip_del(data->strip);
    }

    free(data->r_buf);
    free(data->g_buf);
    free(data->b_buf);
    free(data);
    dev->driver_data = NULL;
    return ESP_OK;
}

static esp_err_t ws2812_read(device_t *dev, cJSON *value)
{
    ws2812_data_t *data = (ws2812_data_t*)dev->driver_data;
    if (data == NULL) return ESP_ERR_INVALID_STATE;

    cJSON *pixels = cJSON_AddArrayToObject(value, "pixels");
    for (int i = 0; i < data->count; i++) {
        cJSON *px = cJSON_CreateObject();
        cJSON_AddNumberToObject(px, "r", data->r_buf[i]);
        cJSON_AddNumberToObject(px, "g", data->g_buf[i]);
        cJSON_AddNumberToObject(px, "b", data->b_buf[i]);
        cJSON_AddItemToArray(pixels, px);
    }
    cJSON_AddNumberToObject(value, "count", data->count);
    cJSON_AddNumberToObject(value, "brightness", data->brightness);
    return ESP_OK;
}

static esp_err_t ws2812_write(device_t *dev, const cJSON *value)
{
    ws2812_data_t *data = (ws2812_data_t*)dev->driver_data;
    if (data == NULL) return ESP_ERR_INVALID_STATE;

    if (!cJSON_IsObject(value)) {
        return ESP_ERR_INVALID_ARG;
    }

    // Form 1: {"all": {"r":..,"g":..,"b":..}}
    cJSON *all = cJSON_GetObjectItem(value, "all");
    if (cJSON_IsObject(all)) {
        cJSON *r = cJSON_GetObjectItem(all, "r");
        cJSON *g = cJSON_GetObjectItem(all, "g");
        cJSON *b = cJSON_GetObjectItem(all, "b");
        if (cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b)) {
            for (int i = 0; i < data->count; i++) {
                data->r_buf[i] = (uint8_t)r->valueint;
                data->g_buf[i] = (uint8_t)g->valueint;
                data->b_buf[i] = (uint8_t)b->valueint;
            }
            return apply_pixels(data);
        }
    }

    // Form 2: {"index": 0, "r":..,"g":..,"b":..}
    cJSON *index = cJSON_GetObjectItem(value, "index");
    cJSON *r = cJSON_GetObjectItem(value, "r");
    cJSON *g = cJSON_GetObjectItem(value, "g");
    cJSON *b = cJSON_GetObjectItem(value, "b");
    if (cJSON_IsNumber(index) && cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b)) {
        int idx = index->valueint;
        if (idx < 0 || idx >= data->count) {
            return ESP_ERR_INVALID_ARG;
        }
        data->r_buf[idx] = (uint8_t)r->valueint;
        data->g_buf[idx] = (uint8_t)g->valueint;
        data->b_buf[idx] = (uint8_t)b->valueint;
        return apply_pixels(data);
    }

    // Form 3: {"pixels": [{"r":..,"g":..,"b":..}, ...]}
    cJSON *pixels = cJSON_GetObjectItem(value, "pixels");
    if (cJSON_IsArray(pixels)) {
        int idx = 0;
        cJSON *px;
        cJSON_ArrayForEach(px, pixels) {
            if (idx >= data->count) break;
            cJSON *pr = cJSON_GetObjectItem(px, "r");
            cJSON *pg = cJSON_GetObjectItem(px, "g");
            cJSON *pb = cJSON_GetObjectItem(px, "b");
            if (cJSON_IsNumber(pr) && cJSON_IsNumber(pg) && cJSON_IsNumber(pb)) {
                data->r_buf[idx] = (uint8_t)pr->valueint;
                data->g_buf[idx] = (uint8_t)pg->valueint;
                data->b_buf[idx] = (uint8_t)pb->valueint;
            }
            idx++;
        }
        return apply_pixels(data);
    }

    return ESP_ERR_INVALID_ARG;
}

static esp_err_t ws2812_default_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "data_gpio", 48);
    cJSON_AddNumberToObject(config, "count", 1);
    cJSON_AddNumberToObject(config, "brightness", 255);
    return ESP_OK;
}

static const device_type_t ws2812_driver = {
    .name = "ws2812",
    .description = "WS2812 RGB LED strip",
    .capabilities = DEVICE_CAPABILITY_READ | DEVICE_CAPABILITY_WRITE | DEVICE_CAPABILITY_NOTIFY,
    .init = ws2812_init,
    .deinit = ws2812_deinit,
    .read = ws2812_read,
    .write = ws2812_write,
    .get_default_config = ws2812_default_config,
};

esp_err_t ws2812_driver_register(void)
{
    return device_type_register(&ws2812_driver);
}
