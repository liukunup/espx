/**
 * @file button_service.h
 * @brief ESP Button Service 封装层 - 使用 GPIO ISR + esp_timer
 * 
 * 提供统一的按钮事件处理接口，支持:
 * - 短按/长按检测
 * - 消抖处理
 * - 回调通知
 */

#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Button service 事件类型
 */
typedef enum {
    BUTTON_SERVICE_EVENT_PRESSED = 0,
    BUTTON_SERVICE_EVENT_RELEASED,
    BUTTON_SERVICE_EVENT_CLICKED,
    BUTTON_SERVICE_EVENT_LONG_PRESSED,
} button_service_event_type_t;

/**
 * @brief Button service 事件结构
 */
typedef struct {
    uint8_t button_id;
    button_service_event_type_t type;
    uint32_t press_duration_ms;
    uint32_t timestamp;
} button_service_event_t;

/**
 * @brief Button service 回调函数类型
 */
typedef void (*button_service_callback_t)(const button_service_event_t *event, void *user_data);

/**
 * @brief Button service 句柄 ( opaque )
 */
typedef struct button_service_s *button_service_handle_t;

/**
 * @brief Button service 配置
 */
typedef struct {
    uint8_t button_id;              /**< 按钮 ID */
    gpio_num_t gpio_num;            /**< GPIO 编号 */
    uint32_t long_press_ms;         /**< 长按阈值 (ms), 默认 1000 */
    uint32_t short_press_ms;        /**< 短按阈值 (ms), 默认 50 */
    bool active_low;                /**< 低电平触发 (上拉模式) */
} button_service_config_t;

/**
 * @brief 创建按钮服务实例
 * @param config 按钮配置
 * @return 句柄或 NULL
 */
button_service_handle_t button_service_create(const button_service_config_t *config);

/**
 * @brief 删除按钮服务实例
 * @param handle 按钮句柄
 * @return ESP_OK 成功
 */
esp_err_t button_service_delete(button_service_handle_t handle);

/**
 * @brief 注册按钮事件回调
 * @param handle 按钮句柄
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return ESP_OK 成功
 */
esp_err_t button_service_register_callback(button_service_handle_t handle,
                                           button_service_callback_t callback,
                                           void *user_data);

/**
 * @brief 获取按钮当前状态
 * @param handle 按钮句柄
 * @param pressed 输出: 是否按下
 * @return ESP_OK 成功
 */
esp_err_t button_service_get_state(button_service_handle_t handle, bool *pressed);

/**
 * @brief 轮询按钮状态 (用于主循环轮询模式)
 * @param handle 按钮句柄
 */
void button_service_poll(button_service_handle_t handle);

/**
 * @brief 等待按钮事件 (阻塞)
 * @param handle 按钮句柄
 * @param event 输出事件
 * @param timeout_ms 超时 ms
 * @return ESP_OK 成功, ESP_ERR_TIMEOUT 超时
 */
esp_err_t button_service_wait_event(button_service_handle_t handle,
                                    button_service_event_t *event,
                                    uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif
