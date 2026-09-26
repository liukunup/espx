/**
 * @file led_driver.c
 * @brief LED Driver for WS2812 RGB LED using led_strip component
 */

#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_driver.h"
#include "led_strip.h"

#ifndef CONFIG_LED_GPIO
#define CONFIG_LED_GPIO 48
#endif

#ifndef CONFIG_LED_STRIP_LENGTH
#define CONFIG_LED_STRIP_LENGTH 1
#endif

static led_strip_handle_t g_led_strip = NULL;
static bool g_initialized = false;

static TaskHandle_t g_blink_task = NULL;
static volatile bool g_blink_running = false;

static void blink_task(void *pvParameters)
{
    int blink_type = (int)pvParameters;
    bool led_state = false;

    while (g_blink_running) {
        led_state = !led_state;

        if (led_state) {
            if (blink_type == 1) {
                led_strip_set_pixel(g_led_strip, 0, 0, 255, 0); // Green
            } else {
                led_strip_set_pixel(g_led_strip, 0, 255, 0, 0); // Red
            }
        } else {
            led_strip_set_pixel(g_led_strip, 0, 0, 0, 0);
        }

        led_strip_refresh(g_led_strip);
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    g_blink_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t led_driver_init(void)
{
    if (g_initialized) {
        return ESP_OK;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = CONFIG_LED_GPIO,
        .max_leds = CONFIG_LED_STRIP_LENGTH,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 64,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &g_led_strip);
    if (err != ESP_OK) {
        return err;
    }

    led_strip_set_pixel(g_led_strip, 0, 0, 0, 0);
    led_strip_refresh(g_led_strip);

    g_initialized = true;
    return ESP_OK;
}

esp_err_t led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (!g_initialized || g_led_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_blink_running) {
        g_blink_running = false;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    led_strip_set_pixel(g_led_strip, 0, r, g, b);
    led_strip_refresh(g_led_strip);

    return ESP_OK;
}

esp_err_t led_set_brightness(uint8_t brightness)
{
    return ESP_OK;
}

esp_err_t led_set_pattern(led_pattern_t pattern, uint32_t period)
{
    if (!g_initialized || g_led_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_blink_running) {
        g_blink_running = false;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    switch (pattern) {
    case LED_PATTERN_NONE:
        led_strip_set_pixel(g_led_strip, 0, 0, 0, 0);
        led_strip_refresh(g_led_strip);
        break;
    case LED_PATTERN_BLINK:
        g_blink_running = true;
        xTaskCreate(blink_task, "blink_task", 2048, (void*)1, 2, &g_blink_task);
        break;
    case LED_PATTERN_BREATHE:
    case LED_PATTERN_PULSE:
        for (int i = 0; i < 2; i++) {
            for (int j = 0; j <= 255; j += 15) {
                led_strip_set_pixel(g_led_strip, 0, 0, j, 0);
                led_strip_refresh(g_led_strip);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            for (int j = 255; j >= 0; j -= 15) {
                led_strip_set_pixel(g_led_strip, 0, 0, j, 0);
                led_strip_refresh(g_led_strip);
                vTaskDelay(pdMS_TO_TICKS(20));
            }
        }
        led_strip_set_pixel(g_led_strip, 0, 0, 0, 0);
        led_strip_refresh(g_led_strip);
        break;
    default:
        break;
    }

    return ESP_OK;
}

esp_err_t led_stop_pattern(void)
{
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_blink_running) {
        g_blink_running = false;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    led_strip_set_pixel(g_led_strip, 0, 0, 0, 0);
    led_strip_refresh(g_led_strip);

    return ESP_OK;
}

esp_err_t led_set_status(const char *status)
{
    if (!g_initialized || g_led_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_blink_running) {
        g_blink_running = false;
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    if (strcmp(status, "connected") == 0 || strcmp(status, "wifi_connected") == 0) {
        led_strip_set_pixel(g_led_strip, 0, 0, 255, 0);
    } else if (strcmp(status, "disconnected") == 0 || strcmp(status, "wifi_disconnected") == 0) {
        led_strip_set_pixel(g_led_strip, 0, 255, 0, 0);
    } else if (strcmp(status, "error") == 0 || strcmp(status, "ota_failed") == 0) {
        g_blink_running = true;
        xTaskCreate(blink_task, "blink_task", 2048, (void*)2, 2, &g_blink_task);
        return ESP_OK;
    } else if (strcmp(status, "ota_progress") == 0 || strcmp(status, "updating") == 0) {
        led_strip_set_pixel(g_led_strip, 0, 0, 0, 255);
    } else if (strcmp(status, "provisioning") == 0) {
        g_blink_running = true;
        xTaskCreate(blink_task, "blink_task", 2048, (void*)2, 2, &g_blink_task);
        return ESP_OK;
    } else {
        led_strip_set_pixel(g_led_strip, 0, 0, 0, 0);
    }

    led_strip_refresh(g_led_strip);
    return ESP_OK;
}

esp_err_t led_off(void)
{
    return led_set_pattern(LED_PATTERN_NONE, 0);
}
