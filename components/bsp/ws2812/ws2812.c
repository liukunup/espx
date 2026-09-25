/**
 * @file ws2812.c
 * @brief WS2812 RGB LED Driver Implementation
 */

#include "ws2812.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ws2812";

/** @brief Maximum LEDs supported */
#define WS2812_MAX_LEDS 256

/** @brief Internal handle structure */
struct ws2812_handle_s {
    int8_t gpio_num;
    uint16_t led_count;
    uint8_t brightness;
    ws2812_pixel_t *pixels;
    
    // RMT handles
    rmt_channel_handle_t rmt_channel;
    rmt_encoder_handle_t bytes_encoder;
    rmt_transmit_config_t tx_config;
    
    bool initialized;
};

/**
 * @brief Convert RGB to GRB byte order for WS2812
 */
static void rgb_to_grb(const ws2812_pixel_t *pixel, uint8_t *grb) {
    grb[0] = pixel->g;  // GRB order
    grb[1] = pixel->r;
    grb[2] = pixel->b;
}

/**
 * @brief Apply brightness to pixel
 */
static void apply_brightness(ws2812_pixel_t *pixel, uint8_t brightness) {
    if (brightness < 255) {
        pixel->r = (pixel->r * brightness) / 255;
        pixel->g = (pixel->g * brightness) / 255;
        pixel->b = (pixel->b * brightness) / 255;
    }
}

ws2812_handle_t ws2812_create(const ws2812_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    
    if (config->led_count == 0 || config->led_count > WS2812_MAX_LEDS) {
        ESP_LOGE(TAG, "Invalid LED count: %d (max: %d)", config->led_count, WS2812_MAX_LEDS);
        return NULL;
    }
    
    ws2812_handle_t handle = (ws2812_handle_t)calloc(1, sizeof(struct ws2812_handle_s));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return NULL;
    }
    
    handle->gpio_num = config->gpio_num;
    handle->led_count = config->led_count;
    handle->brightness = (config->brightness > 0) ? config->brightness : 255;
    
    handle->pixels = (ws2812_pixel_t *)calloc(config->led_count, sizeof(ws2812_pixel_t));
    if (handle->pixels == NULL) {
        ESP_LOGE(TAG, "Failed to allocate pixel buffer");
        free(handle);
        return NULL;
    }
    
    handle->initialized = false;
    
    ESP_LOGI(TAG, "WS2812 driver created: GPIO%d, %d LEDs", 
             config->gpio_num, config->led_count);
    
    return handle;
}

void ws2812_delete(ws2812_handle_t handle) {
    if (handle == NULL) return;
    
    if (handle->pixels) {
        free(handle->pixels);
    }
    
    if (handle->rmt_channel) {
        rmt_disable(handle->rmt_channel);
        rmt_del_channel(handle->rmt_channel);
    }
    
    if (handle->bytes_encoder) {
        rmt_del_encoder(handle->bytes_encoder);
    }
    
    free(handle);
    ESP_LOGI(TAG, "WS2812 driver deleted");
}

int ws2812_init(ws2812_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    if (handle->initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return 0;
    }
    
    if (handle->gpio_num < 0) {
        ESP_LOGI(TAG, "WS2812 disabled (GPIO not configured)");
        handle->initialized = true;
        return 0;
    }
    
    // Configure RMT TX channel
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = handle->gpio_num,
        .clk_src = RMT_CLK_SRC_APB,
        .resolution_hz = 80 * 1000 * 1000,  // 80MHz
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    
    esp_err_t err = rmt_new_tx_channel(&tx_chan_config, &handle->rmt_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT channel: %s", esp_err_to_name(err));
        return -2;
    }
    
    // Create bytes encoder for WS2812 timing
    rmt_bytes_encoder_config_t encoder_config = {
        .bit0 = {
            .duration0 = 30,  // T0H ~0.375us at 80MHz
            .level0 = 1,
            .duration1 = 80,  // T0L ~1.0us
            .level1 = 0,
        },
        .bit1 = {
            .duration0 = 60,  // T1H ~0.75us
            .level0 = 1,
            .duration1 = 50,  // T1L ~0.625us
            .level1 = 0,
        },
    };
    
    err = rmt_new_bytes_encoder(&encoder_config, &handle->bytes_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create bytes encoder: %s", esp_err_to_name(err));
        return -3;
    }
    
    handle->tx_config.loop_count = 0;
    
    rmt_enable(handle->rmt_channel);
    
    handle->initialized = true;
    ESP_LOGI(TAG, "WS2812 initialized on GPIO%d", handle->gpio_num);
    
    return 0;
}

int ws2812_set_pixel(ws2812_handle_t handle, uint16_t index, const ws2812_color_t *color) {
    if (handle == NULL || color == NULL) {
        return -1;
    }
    
    if (index >= handle->led_count) {
        ESP_LOGW(TAG, "Pixel index %d out of range (max: %d)", index, handle->led_count - 1);
        return -2;
    }
    
    handle->pixels[index].r = color->r;
    handle->pixels[index].g = color->g;
    handle->pixels[index].b = color->b;
    
    return 0;
}

int ws2812_set_all(ws2812_handle_t handle, const ws2812_color_t *color) {
    if (handle == NULL || color == NULL) {
        return -1;
    }
    
    for (uint16_t i = 0; i < handle->led_count; i++) {
        handle->pixels[i].r = color->r;
        handle->pixels[i].g = color->g;
        handle->pixels[i].b = color->b;
    }
    
    return 0;
}

int ws2812_set_pixel_rgb(ws2812_handle_t handle, uint16_t index, uint8_t r, uint8_t g, uint8_t b) {
    ws2812_color_t color = { .r = r, .g = g, .b = b };
    return ws2812_set_pixel(handle, index, &color);
}

int ws2812_set_all_rgb(ws2812_handle_t handle, uint8_t r, uint8_t g, uint8_t b) {
    ws2812_color_t color = { .r = r, .g = g, .b = b };
    return ws2812_set_all(handle, &color);
}

int ws2812_clear(ws2812_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    memset(handle->pixels, 0, handle->led_count * sizeof(ws2812_pixel_t));
    return 0;
}

int ws2812_set_brightness(ws2812_handle_t handle, uint8_t brightness) {
    if (handle == NULL) {
        return -1;
    }
    
    handle->brightness = brightness;
    return 0;
}

int ws2812_show(ws2812_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    if (!handle->initialized || handle->gpio_num < 0) {
        return 0;  // Disabled, nothing to do
    }
    
    // Convert RGB to GRB and apply brightness
    uint8_t *grb_data = (uint8_t *)malloc(handle->led_count * 3);
    if (grb_data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate GRB buffer");
        return -2;
    }
    
    for (uint16_t i = 0; i < handle->led_count; i++) {
        ws2812_pixel_t pixel = handle->pixels[i];
        apply_brightness(&pixel, handle->brightness);
        rgb_to_grb(&pixel, &grb_data[i * 3]);
    }
    
    esp_err_t err = rmt_transmit(handle->rmt_channel, 
                                  handle->bytes_encoder,
                                  grb_data, 
                                  handle->led_count * 3, 
                                  &handle->tx_config);
    
    free(grb_data);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RMT transmit failed: %s", esp_err_to_name(err));
        return -3;
    }
    
    return 0;
}

int ws2812_set_hue(ws2812_handle_t handle, uint16_t hue) {
    if (handle == NULL) {
        return -1;
    }
    
    ws2812_color_t color;
    ws2812_hsv_to_rgb(hue, 100, 100, &color);
    
    return ws2812_set_all(handle, &color);
}

void ws2812_hsv_to_rgb(uint16_t h, uint8_t s, uint8_t v, ws2812_color_t *color) {
    if (color == NULL) return;
    
    uint8_t r, g, b;
    
    if (s == 0) {
        r = g = b = (v * 255) / 100;
    } else {
        uint16_t h_div = h / 60;
        uint16_t f = h % 60;
        
        uint8_t p = (uint8_t)((v * (100 - s)) / 100);
        uint8_t q = (uint8_t)((v * (100 - (s * f / 60))) / 100);
        uint8_t t = (uint8_t)((v * (100 - (s * (60 - f) / 60))) / 100);
        
        switch (h_div) {
            case 0: r = v * 255 / 100; g = t; b = p; break;
            case 1: r = q; g = v * 255 / 100; b = p; break;
            case 2: r = p; g = v * 255 / 100; b = t; break;
            case 3: r = p; g = q; b = v * 255 / 100; break;
            case 4: r = t; g = p; b = v * 255 / 100; break;
            default: r = v * 255 / 100; g = p; b = q; break;
        }
    }
    
    color->r = r;
    color->g = g;
    color->b = b;
}
