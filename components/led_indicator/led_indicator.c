/**
 * @file led_indicator.c
 * @brief LED Indicator with GPIO21 shared between blue LED and WS2812
 */

#include "led_indicator.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_types.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "led_indicator";

static int8_t s_led_gpio = 21;
static led_pattern_t s_pattern = LED_OFF;
static led_color_t s_color = LED_COLOR_GREEN;
static uint32_t s_tick_count = 0;
static bool s_initialized = false;
static bool s_ws2812_mode = false;

// RMT handles
static rmt_channel_handle_t s_rmt_channel = NULL;
static rmt_encoder_handle_t s_bytes_encoder = NULL;
static rmt_transmit_config_t tx_config = {
    .loop_count = 0,
};

// Color table (RGB values, will convert to GRB for WS2812)
// Format: 0x00RRGGBB
static const uint32_t color_table[] = {
    [LED_COLOR_RED] = 0x00FF0000,     // R=255, G=0, B=0
    [LED_COLOR_GREEN] = 0x0000FF00,    // R=0, G=255, B=0
    [LED_COLOR_BLUE] = 0x000000FF,     // R=0, G=0, B=255
    [LED_COLOR_YELLOW] = 0x00FFFF00,   // R=255, G=255, B=0
    [LED_COLOR_CYAN] = 0x0000FFFF,    // R=0, G=255, B=255
    [LED_COLOR_MAGENTA] = 0x00FF00FF,  // R=255, G=0, B=255
    [LED_COLOR_WHITE] = 0x00FFFFFF,   // R=255, G=255, B=255
};

/**
 * @brief Switch to GPIO mode for blue LED (active low)
 */
static void switch_to_gpio_mode(void) {
    if (!s_ws2812_mode || s_rmt_channel == NULL) return;
    
    rmt_disable(s_rmt_channel);
    
    gpio_reset_pin(s_led_gpio);
    gpio_set_direction(s_led_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(s_led_gpio, 1);  // High = LED off (active low)
    
    s_ws2812_mode = false;
}

/**
 * @brief Switch to RMT mode for WS2812
 */
static void switch_to_rmt_mode(void) {
    if (s_ws2812_mode || s_rmt_channel == NULL) return;
    
    gpio_reset_pin(s_led_gpio);
    rmt_enable(s_rmt_channel);
    
    s_ws2812_mode = true;
}

/**
 * @brief Set blue LED state (active low)
 */
static void set_blue_led(bool on) {
    switch_to_gpio_mode();
    gpio_set_level(s_led_gpio, on ? 0 : 1);  // Active low
}

/**
 * @brief Send color to WS2812 (RGB to GRB conversion)
 */
static void ws2812_set_color(uint32_t rgb_color) {
    if (s_rmt_channel == NULL || !s_initialized) return;
    
    switch_to_rmt_mode();
    
    // Extract RGB from 0x00RRGGBB
    uint8_t r = (rgb_color >> 16) & 0xFF;
    uint8_t g = (rgb_color >> 8) & 0xFF;
    uint8_t b = rgb_color & 0xFF;
    
    // Convert to GRB for WS2812
    uint8_t grb[3] = {g, r, b};
    
    rmt_transmit(s_rmt_channel, s_bytes_encoder, grb, 3, &tx_config);
}

/**
 * @brief Get GRB color value
 */
static uint32_t get_color_value(led_color_t color) {
    if (color >= LED_COLOR_RED && color <= LED_COLOR_WHITE) {
        return color_table[color];
    }
    return color_table[LED_COLOR_GREEN];
}

/**
 * @brief Calculate brightness based on pattern
 */
static uint8_t get_brightness(void) {
    switch (s_pattern) {
        case LED_OFF:
            return 0;
        case LED_ON:
            return 255;
        case LED_BLINK_SLOW:
            return (s_tick_count % 1000) < 500 ? 255 : 0;
        case LED_BLINK_FAST:
            return (s_tick_count % 250) < 125 ? 255 : 0;
        case LED_BLINK_ONCE:
            if (s_tick_count > 100 && s_tick_count < 400) return 255;
            return 0;
        case LED_BREATHE: {
            uint32_t t = s_tick_count % 2000;
            if (t < 1000) {
                return (uint8_t)((t * 255) / 1000);
            } else {
                return (uint8_t)(((2000 - t) * 255) / 1000);
            }
        }
        default:
            return 0;
    }
}

void led_indicator_init(int8_t gpio_num) {
    s_led_gpio = gpio_num;
    s_ws2812_mode = false;
    
    if (gpio_num >= 0) {
        // Configure RMT TX channel
        rmt_tx_channel_config_t tx_chan_config = {
            .gpio_num = gpio_num,
            .clk_src = RMT_CLK_SRC_APB,
            .resolution_hz = 80 * 1000 * 1000,
            .mem_block_symbols = 64,
            .trans_queue_depth = 4,
        };
        
        ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &s_rmt_channel));
        
        // Create bytes encoder for WS2812
        rmt_bytes_encoder_config_t encoder_config = {
            .bit0 = {
                .duration0 = 30,  // T0H ~0.375us
                .level0 = 1,
                .duration1 = 80, // T0L ~1.0us
                .level1 = 0,
            },
            .bit1 = {
                .duration0 = 60,  // T1H ~0.75us
                .level0 = 1,
                .duration1 = 50, // T1L ~0.625us
                .level1 = 0,
            },
        };
        ESP_ERROR_CHECK(rmt_new_bytes_encoder(&encoder_config, &s_bytes_encoder));
        
        // Start in GPIO mode (LED off)
        switch_to_gpio_mode();
        
        ESP_LOGI(TAG, "LED indicator initialized on GPIO%d (blue LED + WS2812)", gpio_num);
    } else {
        ESP_LOGI(TAG, "LED indicator disabled");
    }
    
    s_initialized = true;
    s_pattern = LED_OFF;
    s_tick_count = 0;
}

void led_indicator_set_status(led_status_t status) {
    if (!s_initialized) return;
    
    switch (status) {
        case LED_STATUS_BOOT:
            s_pattern = LED_BLINK_SLOW;
            s_color = LED_COLOR_BLUE;
            break;
        case LED_STATUS_NORMAL:
            s_pattern = LED_BREATHE;
            s_color = LED_COLOR_GREEN;
            break;
        case LED_STATUS_ERROR:
            s_pattern = LED_BLINK_FAST;
            s_color = LED_COLOR_RED;
            break;
        case LED_STATUS_WIFI_CONNECTING:
            s_pattern = LED_BLINK_SLOW;
            s_color = LED_COLOR_BLUE;
            break;
        case LED_STATUS_WIFI_CONNECTED:
            s_pattern = LED_ON;
            s_color = LED_COLOR_GREEN;
            break;
        case LED_STATUS_WIFI_DISCONNECTED:
            s_pattern = LED_BLINK_SLOW;
            s_color = LED_COLOR_BLUE;
            break;
        case LED_STATUS_PROV_START:
            s_pattern = LED_BLINK_SLOW;
            s_color = LED_COLOR_YELLOW;
            break;
        case LED_STATUS_PROV_SUCCESS:
            s_pattern = LED_BLINK_ONCE;
            s_color = LED_COLOR_GREEN;
            break;
        case LED_STATUS_PROV_FAILED:
            s_pattern = LED_BLINK_FAST;
            s_color = LED_COLOR_YELLOW;
            break;
        case LED_STATUS_MQTT_CONNECTING:
            s_pattern = LED_BLINK_SLOW;
            s_color = LED_COLOR_CYAN;
            break;
        case LED_STATUS_MQTT_CONNECTED:
            s_pattern = LED_BLINK_ONCE;
            s_color = LED_COLOR_GREEN;
            break;
        case LED_STATUS_MQTT_DISCONNECTED:
            s_pattern = LED_BLINK_FAST;
            s_color = LED_COLOR_YELLOW;
            break;
        case LED_STATUS_OTA_UPDATING:
            s_pattern = LED_BREATHE;
            s_color = LED_COLOR_MAGENTA;
            break;
        case LED_STATUS_OTA_SUCCESS:
            s_pattern = LED_ON;
            s_color = LED_COLOR_GREEN;
            break;
        case LED_STATUS_OTA_FAILED:
            s_pattern = LED_BLINK_FAST;
            s_color = LED_COLOR_RED;
            break;
        default:
            s_pattern = LED_OFF;
            break;
    }
}

void led_indicator_set_pattern(led_pattern_t pattern, led_color_t color) {
    if (!s_initialized) return;
    s_pattern = pattern;
    s_color = color;
}

void led_indicator_task(void) {
    if (!s_initialized) return;
    
    s_tick_count++;
    
    uint8_t brightness = get_brightness();
    
    if (brightness == 0) {
        // LED off - use GPIO mode
        set_blue_led(false);
        return;
    }
    
    if (s_color == LED_COLOR_BLUE) {
        // Blue color - use GPIO mode (blue LED active low)
        set_blue_led(true);
    } else {
        // Other colors - use WS2812
        uint32_t base_color = get_color_value(s_color);
        
        uint8_t g = (base_color >> 16) & 0xFF;
        uint8_t r = (base_color >> 8) & 0xFF;
        uint8_t b = base_color & 0xFF;
        
        g = (g * brightness) / 255;
        r = (r * brightness) / 255;
        b = (b * brightness) / 255;
        
        uint32_t final_grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
        ws2812_set_color(final_grb);
    }
}
