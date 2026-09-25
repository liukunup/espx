/**
 * @file led_indicator.h
 * @brief LED Indicator component with status patterns
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED status patterns
 */
typedef enum {
    LED_OFF = 0,           // 熄灭
    LED_ON,                 // 常亮
    LED_BLINK_SLOW,         // 慢闪 (1Hz)
    LED_BLINK_FAST,         // 快闪 (4Hz)
    LED_BLINK_ONCE,         // 单闪
    LED_BREATHE,            // 呼吸灯
} led_pattern_t;

/**
 * @brief LED colors (for RGB LED)
 */
typedef enum {
    LED_COLOR_RED = 0,
    LED_COLOR_GREEN,
    LED_COLOR_BLUE,
    LED_COLOR_YELLOW,
    LED_COLOR_CYAN,
    LED_COLOR_MAGENTA,
    LED_COLOR_WHITE,
} led_color_t;

/**
 * @brief LED status types
 */
typedef enum {
    // System status
    LED_STATUS_BOOT = 0,        // 启动中 - 蓝色闪烁
    LED_STATUS_NORMAL,           // 正常运行 - 绿色呼吸
    LED_STATUS_ERROR,            // 系统错误 - 红色快闪
    
    // Network status
    LED_STATUS_WIFI_CONNECTING,  // WiFi连接中 - 蓝色慢闪
    LED_STATUS_WIFI_CONNECTED,    // WiFi已连接 - 绿色常亮
    LED_STATUS_WIFI_DISCONNECTED, // WiFi断开 - 蓝色闪烁
    
    // Provisioning status
    LED_STATUS_PROV_START,       // 配网开始 - 黄色慢闪
    LED_STATUS_PROV_SUCCESS,     // 配网成功 - 绿色单闪
    LED_STATUS_PROV_FAILED,      // 配网失败 - 黄色快闪
    
    // MQTT status
    LED_STATUS_MQTT_CONNECTING,  // MQTT连接中 - 青色慢闪
    LED_STATUS_MQTT_CONNECTED,    // MQTT已连接 - 绿色单闪
    LED_STATUS_MQTT_DISCONNECTED, // MQTT断开 - 黄色快闪
    
    // OTA status
    LED_STATUS_OTA_UPDATING,    // OTA升级中 - 紫色呼吸
    LED_STATUS_OTA_SUCCESS,      // OTA成功 - 绿色常亮
    LED_STATUS_OTA_FAILED,       // OTA失败 - 红色快闪
} led_status_t;

/**
 * @brief Initialize LED indicator
 * @param gpio_num GPIO pin number for LED (0-48, or -1 to disable)
 */
void led_indicator_init(int8_t gpio_num);

/**
 * @brief Set LED status (pattern)
 * @param status Status type
 */
void led_indicator_set_status(led_status_t status);

/**
 * @brief Set LED pattern directly
 * @param pattern LED pattern
 * @param color LED color (for RGB LED)
 */
void led_indicator_set_pattern(led_pattern_t pattern, led_color_t color);

/**
 * @brief LED task handler (call periodically)
 */
void led_indicator_task(void);

/**
 * @brief Start color cycling mode (Blue LED -> Red -> Green -> Blue(WS2812))
 */
void led_indicator_cycle_colors(void);

/**
 * @brief Color cycle tick (call in task loop, every 100ms)
 * Changes color every 10 ticks (1 second)
 */
void led_indicator_cycle_tick(void);

/**
 * @brief Set blue LED on/off (GPIO mode)
 * @param on true to turn on, false to turn off
 */
void blue_led_set(bool on);

#ifdef __cplusplus
}
#endif
