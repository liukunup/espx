/**
 * @file led_driver.c
 * @brief LED Driver implementation using led_indicator
 */

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <led_indicator.h>

#include "led_driver.h"

static const char *TAG = "led_driver";

// LED configuration - using GPIO 48 on ESP32-S3
// Adjust based on actual hardware
#ifndef CONFIG_LED_GPIO
#define CONFIG_LED_GPIO 48
#endif

static led_indicator_handle_t g_led_handle = NULL;

// LED states
typedef enum {
    LED_STATE_OFF = 0,
    LED_STATE_RED,
    LED_STATE_GREEN,
    LED_STATE_BLUE,
    LED_STATE_WHITE,
    LED_STATE_YELLOW,
    LED_STATE_CYAN,
    LED_STATE_MAGENTA,
    LED_STATE_BLINK_RED,
    LED_STATE_BLINK_GREEN,
    LED_STATE_BREATHE,
} led_state_t;

// LED blink durations
static const int BLINK_PERIOD = 1000;  // ms

// LED states configuration for led_indicator
static const led_indicator_state_t led_states[] = {
    [LED_STATE_OFF] = {
        .hold_on_state = 0,
        .hold_off_state = 0,
    },
    [LED_STATE_RED] = {
        .hold_on_state = -1,  // Always on
    },
    [LED_STATE_GREEN] = {
        .hold_on_state = -1,
    },
    [LED_STATE_BLUE] = {
        .hold_on_state = -1,
    },
    [LED_STATE_WHITE] = {
        .hold_on_state = -1,
    },
    [LED_STATE_YELLOW] = {
        .hold_on_state = -1,
    },
    [LED_STATE_CYAN] = {
        .hold_on_state = -1,
    },
    [LED_STATE_MAGENTA] = {
        .hold_on_state = -1,
    },
    [LED_STATE_BLINK_RED] = {
        .hold_on_state = 200,
        .hold_off_state = 200,
        .brightness = 255,
    },
    [LED_STATE_BLINK_GREEN] = {
        .hold_on_state = 200,
        .hold_off_state = 200,
        .brightness = 255,
    },
    [LED_STATE_BREATHE] = {
        .hold_on_state = 2000,
        .hold_off_state = 2000,
        .brightness = -1,
    },
};

static const char *led_state_names[] = {
    [LED_STATE_OFF] = "OFF",
    [LED_STATE_RED] = "RED",
    [LED_STATE_GREEN] = "GREEN",
    [LED_STATE_BLUE] = "BLUE",
    [LED_STATE_WHITE] = "WHITE",
    [LED_STATE_YELLOW] = "YELLOW",
    [LED_STATE_CYAN] = "CYAN",
    [LED_STATE_MAGENTA] = "MAGENTA",
    [LED_STATE_BLINK_RED] = "BLINK_RED",
    [LED_STATE_BLINK_GREEN] = "BLINK_GREEN",
    [LED_STATE_BREATHE] = "BREATHE",
};

static led_state_t g_current_state = LED_STATE_OFF;

// Public API implementation
esp_err_t led_driver_init(void)
{
    ESP_LOGI(TAG, "Initializing LED driver on GPIO %d", CONFIG_LED_GPIO);

    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CONFIG_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&io_conf);

    // Initialize led_indicator
    led_indicator_config_t config = {
        .mode = LED_MODE_GPIO,
        .gpio_num = CONFIG_LED_GPIO,
    };

    g_led_handle = led_indicator_create(&config);
    if (g_led_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create LED indicator");
        return ESP_FAIL;
    }

    // Add states
    for (int i = 0; i < sizeof(led_states) / sizeof(led_states[0]); i++) {
        led_indicator_add_state(g_led_handle, i, &led_states[i]);
    }

    ESP_LOGI(TAG, "LED driver initialized");

    return ESP_OK;
}

esp_err_t led_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    if (g_led_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Determine color state
    led_state_t state = LED_STATE_OFF;

    if (r > 0 && g == 0 && b == 0) {
        state = LED_STATE_RED;
    } else if (r == 0 && g > 0 && b == 0) {
        state = LED_STATE_GREEN;
    } else if (r == 0 && g == 0 && b > 0) {
        state = LED_STATE_BLUE;
    } else if (r > 0 && g > 0 && b == 0) {
        state = LED_STATE_YELLOW;
    } else if (r == 0 && g > 0 && b > 0) {
        state = LED_STATE_CYAN;
    } else if (r > 0 && g == 0 && b > 0) {
        state = LED_STATE_MAGENTA;
    } else if (r > 0 && g > 0 && b > 0) {
        state = LED_STATE_WHITE;
    }

    // Set LED state
    if (state != LED_STATE_OFF) {
        led_indicator_start(g_led_handle, state);
    } else {
        led_indicator_stop(g_led_handle, g_current_state);
    }

    g_current_state = state;

    ESP_LOGD(TAG, "LED color set: R=%d G=%d B=%d -> %s", r, g, b, led_state_names[state]);

    return ESP_OK;
}

esp_err_t led_set_brightness(uint8_t brightness)
{
    // Brightness control would require PWM or RGB LED driver
    // For now, just use full brightness
    ESP_LOGD(TAG, "Brightness set to %d%%", brightness);
    return ESP_OK;
}

esp_err_t led_set_pattern(led_pattern_t pattern, uint32_t period)
{
    if (g_led_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    led_state_t state;

    switch (pattern) {
    case LED_PATTERN_NONE:
        state = LED_STATE_OFF;
        break;
    case LED_PATTERN_BLINK:
        state = LED_STATE_BLINK_GREEN;
        break;
    case LED_PATTERN_BREATHE:
        state = LED_STATE_BREATHE;
        break;
    case LED_PATTERN_PULSE:
        state = LED_STATE_BREATHE;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    led_indicator_start(g_led_handle, state);
    g_current_state = state;

    ESP_LOGI(TAG, "LED pattern set: %d", pattern);

    return ESP_OK;
}

esp_err_t led_stop_pattern(void)
{
    if (g_led_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    led_indicator_stop(g_led_handle, g_current_state);
    g_current_state = LED_STATE_OFF;

    return ESP_OK;
}

esp_err_t led_set_status(const char *status)
{
    if (g_led_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    led_state_t state = LED_STATE_OFF;

    if (strcmp(status, "connected") == 0 || strcmp(status, "wifi_connected") == 0) {
        state = LED_STATE_GREEN;
    } else if (strcmp(status, "disconnected") == 0 || strcmp(status, "wifi_disconnected") == 0) {
        state = LED_STATE_RED;
    } else if (strcmp(status, "error") == 0 || strcmp(status, "ota_failed") == 0) {
        state = LED_STATE_BLINK_RED;
    } else if (strcmp(status, "ota_progress") == 0 || strcmp(status, "updating") == 0) {
        state = LED_STATE_BREATHE;
    } else if (strcmp(status, "provisioning") == 0) {
        state = LED_STATE_BLINK_GREEN;
    }

    led_indicator_start(g_led_handle, state);
    g_current_state = state;

    ESP_LOGI(TAG, "LED status set: %s -> %s", status, led_state_names[state]);

    return ESP_OK;
}

esp_err_t led_off(void)
{
    return led_set_color(0, 0, 0);
}
