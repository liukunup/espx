/**
 * @file ws2812.c
 * @brief WS2812 RGB LED strip driver
 *
 * Uses esp-idf led_strip component.
 *
 * Config:
 *   din:  Data pin (default 48 for ESP32-S3 R16N8)
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
#include "device_type.h"

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
    /* Scale by brightness (0-255) so the configured value actually applies. */
    const uint32_t b = (uint32_t)data->brightness;

    for (int i = 0; i < data->count; i++) {
        uint32_t r = (uint32_t)data->r_buf[i] * b / 255u;
        uint32_t g = (uint32_t)data->g_buf[i] * b / 255u;
        uint32_t bl = (uint32_t)data->b_buf[i] * b / 255u;
        led_strip_set_pixel(data->strip, i, r, g, bl);
    }
    return led_strip_refresh(data->strip);
}

/**
 * @brief Reject impossible GPIO numbers before touching the peripheral
 */
static esp_err_t ws2812_validate_config(const cJSON *config)
{
    if (!cJSON_IsObject(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    // din is required
    cJSON *din = cJSON_GetObjectItem(config, "din");
    if (!cJSON_IsNumber(din)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (din->valueint < 0 || din->valueint > 48) {
        return ESP_ERR_INVALID_ARG;
    }

    // count is optional (defaults to 1)
    cJSON *count = cJSON_GetObjectItem(config, "count");
    if (cJSON_IsNumber(count) && (count->valueint < 1 || count->valueint > 300)) {
        return ESP_ERR_INVALID_ARG;
    }

    // r, g, b, brightness are optional (handled in init/write)
    return ESP_OK;
}

static esp_err_t ws2812_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *data_gpio = cJSON_GetObjectItem(config, "din");
    cJSON *count = cJSON_GetObjectItem(config, "count");
    cJSON *brightness = cJSON_GetObjectItem(config, "brightness");
    cJSON *r_cfg = cJSON_GetObjectItem(config, "r");
    cJSON *g_cfg = cJSON_GetObjectItem(config, "g");
    cJSON *b_cfg = cJSON_GetObjectItem(config, "b");

    ws2812_data_t *data = calloc(1, sizeof(ws2812_data_t));
    if (data == NULL) {
        return ESP_ERR_NO_MEM;
    }

    data->data_gpio = cJSON_IsNumber(data_gpio) ? data_gpio->valueint : 48;
    data->count = cJSON_IsNumber(count) ? count->valueint : 1;
    data->brightness = cJSON_IsNumber(brightness) ? brightness->valueint : 255;
    uint8_t init_r = cJSON_IsNumber(r_cfg) ? r_cfg->valueint : 0;
    uint8_t init_g = cJSON_IsNumber(g_cfg) ? g_cfg->valueint : 0;
    uint8_t init_b = cJSON_IsNumber(b_cfg) ? b_cfg->valueint : 0;

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
    // Initialize buffers with saved color
    for (int i = 0; i < data->count; i++) {
        data->r_buf[i] = init_r;
        data->g_buf[i] = init_g;
        data->b_buf[i] = init_b;
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

    uint8_t rv = 0, gv = 0, bv = 0;
    int new_brightness = -1;
    bool color_changed = false;

    // Simple form: {"r": 255, "g": 0, "b": 128, "brightness": 128}
    // Sets all pixels to the same color with optional brightness
    cJSON *r = cJSON_GetObjectItem(value, "r");
    cJSON *g = cJSON_GetObjectItem(value, "g");
    cJSON *b = cJSON_GetObjectItem(value, "b");
    cJSON *brightness = cJSON_GetObjectItem(value, "brightness");
    if (cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b)) {
        rv = (uint8_t)r->valueint;
        gv = (uint8_t)g->valueint;
        bv = (uint8_t)b->valueint;
        color_changed = true;
        for (int i = 0; i < data->count; i++) {
            data->r_buf[i] = rv;
            data->g_buf[i] = gv;
            data->b_buf[i] = bv;
        }
        if (cJSON_IsNumber(brightness)) {
            new_brightness = brightness->valueint;
            data->brightness = new_brightness;
        }
    }

    // Form 2: {"all": {"r":..,"g":..,"b":..}}
    cJSON *all = cJSON_GetObjectItem(value, "all");
    if (cJSON_IsObject(all) && !color_changed) {
        cJSON *r2 = cJSON_GetObjectItem(all, "r");
        cJSON *g2 = cJSON_GetObjectItem(all, "g");
        cJSON *b2 = cJSON_GetObjectItem(all, "b");
        if (cJSON_IsNumber(r2) && cJSON_IsNumber(g2) && cJSON_IsNumber(b2)) {
            rv = (uint8_t)r2->valueint;
            gv = (uint8_t)g2->valueint;
            bv = (uint8_t)b2->valueint;
            color_changed = true;
            for (int i = 0; i < data->count; i++) {
                data->r_buf[i] = rv;
                data->g_buf[i] = gv;
                data->b_buf[i] = bv;
            }
        }
    }

    // Save color and brightness to config
    if (color_changed || new_brightness >= 0) {
        cJSON *cfg = (cJSON*)dev->config;
        if (cfg != NULL) {
            cJSON_DeleteItemFromObject(cfg, "r");
            cJSON_DeleteItemFromObject(cfg, "g");
            cJSON_DeleteItemFromObject(cfg, "b");
            cJSON_AddNumberToObject(cfg, "r", rv);
            cJSON_AddNumberToObject(cfg, "g", gv);
            cJSON_AddNumberToObject(cfg, "b", bv);
            if (new_brightness >= 0) {
                cJSON_DeleteItemFromObject(cfg, "brightness");
                cJSON_AddNumberToObject(cfg, "brightness", new_brightness);
            }
            device_manager_save();
        }
    }

    // Apply pixels
    esp_err_t err = apply_pixels(data);
    if (err != ESP_OK) return err;

    // Handle index-based writes separately (don't persist these)
    // This runs after the global color is set above
    cJSON *index = cJSON_GetObjectItem(value, "index");
    if (cJSON_IsNumber(index)) {
        int idx = index->valueint;
        if (idx < 0 || idx >= data->count) {
            return ESP_ERR_INVALID_ARG;
        }
        // Get color from top-level or use current buffer values
        cJSON *r3 = cJSON_GetObjectItem(value, "r");
        cJSON *g3 = cJSON_GetObjectItem(value, "g");
        cJSON *b3 = cJSON_GetObjectItem(value, "b");
        if (cJSON_IsNumber(r3) && cJSON_IsNumber(g3) && cJSON_IsNumber(b3)) {
            data->r_buf[idx] = (uint8_t)r3->valueint;
            data->g_buf[idx] = (uint8_t)g3->valueint;
            data->b_buf[idx] = (uint8_t)b3->valueint;
            return apply_pixels(data);
        }
        // If no color specified, just apply current buffer to this index
        return apply_pixels(data);
    }

    // Form 4: {"pixels": [{"r":..,"g":..,"b":..}, ...]}
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
    cJSON_AddNumberToObject(config, "din", 48);
    cJSON_AddNumberToObject(config, "count", 1);
    cJSON_AddNumberToObject(config, "r", 0);
    cJSON_AddNumberToObject(config, "g", 0);
    cJSON_AddNumberToObject(config, "b", 0);
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
    .validate_config = ws2812_validate_config,
};

esp_err_t ws2812_driver_register(void)
{
    return device_type_register(&ws2812_driver);
}
