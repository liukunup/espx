/**
 * @file button_service.c
 * @brief ESP Button Service 封装层实现
 * 
 * 使用 GPIO 边沿中断 + esp_timer 实现消抖和长按检测
 */

#include "button_service.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "button_service";

/** @brief 默认定时值 */
#define DEFAULT_LONG_PRESS_MS   1000
#define DEFAULT_SHORT_PRESS_MS  50
#define DEFAULT_DEBOUNCE_MS     30

/** @brief 按钮内部状态 */
typedef enum {
    BTN_STATE_IDLE = 0,
    BTN_STATE_DEBOUNCING,
    BTN_STATE_PRESSED,
    BTN_STATE_LONG_PRESS,
} btn_internal_state_t;

/** @brief 按钮实例结构 */
struct button_service_s {
    uint8_t button_id;
    gpio_num_t gpio_num;
    uint32_t long_press_ms;
    uint32_t short_press_ms;
    uint32_t debounce_ms;
    bool active_low;
    
    btn_internal_state_t state;
    uint32_t press_start_time;
    uint32_t last_event_time;
    bool last_raw_level;
    
    button_service_callback_t callback;
    void *user_data;
    
    QueueHandle_t event_queue;
};

/** @brief 全局时间获取 */
static inline uint32_t get_tick_ms(void) {
    return esp_log_timestamp();
}

/** @brief 读取 GPIO 原始电平 */
static inline bool read_gpio_raw(button_service_handle_t handle) {
    return gpio_get_level(handle->gpio_num) == 1;
}

/** @brief 判断按钮是否按下 */
static inline bool is_button_pressed(button_service_handle_t handle, bool raw_level) {
    if (handle->active_low) {
        return !raw_level;  // 低电平触发
    } else {
        return raw_level;   // 高电平触发
    }
}

/** @brief 发送事件到队列和回调 */
static void send_event(button_service_handle_t handle, button_service_event_type_t type, uint32_t duration_ms) {
    button_service_event_t event = {
        .button_id = handle->button_id,
        .type = type,
        .press_duration_ms = duration_ms,
        .timestamp = get_tick_ms()
    };
    
    // 发送到队列
    if (handle->event_queue != NULL) {
        BaseType_t woken = pdFALSE;
        xQueueSendFromISR(handle->event_queue, &event, &woken);
        if (woken) {
            portYIELD_FROM_ISR();
        }
    }
    
    // 调用回调
    if (handle->callback != NULL) {
        handle->callback(&event, handle->user_data);
    }
}

button_service_handle_t button_service_create(const button_service_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    
    if (config->gpio_num < 0 || config->gpio_num >= GPIO_NUM_MAX) {
        ESP_LOGE(TAG, "Invalid GPIO number: %d", config->gpio_num);
        return NULL;
    }
    
    button_service_handle_t handle = calloc(1, sizeof(struct button_service_s));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return NULL;
    }
    
    handle->button_id = config->button_id;
    handle->gpio_num = config->gpio_num;
    handle->long_press_ms = (config->long_press_ms > 0) ? config->long_press_ms : DEFAULT_LONG_PRESS_MS;
    handle->short_press_ms = (config->short_press_ms > 0) ? config->short_press_ms : DEFAULT_SHORT_PRESS_MS;
    handle->debounce_ms = DEFAULT_DEBOUNCE_MS;
    handle->active_low = config->active_low;
    
    handle->state = BTN_STATE_IDLE;
    handle->press_start_time = 0;
    handle->last_event_time = 0;
    handle->last_raw_level = false;
    handle->callback = NULL;
    handle->user_data = NULL;
    
    // 创建事件队列
    handle->event_queue = xQueueCreate(16, sizeof(button_service_event_t));
    if (handle->event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        free(handle);
        return NULL;
    }
    
    ESP_LOGI(TAG, "Button service created: GPIO%d, button_id=%d, long_press=%ums",
             config->gpio_num, config->button_id, handle->long_press_ms);
    
    return handle;
}

esp_err_t button_service_delete(button_service_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 注销 GPIO
    gpio_reset_pin(handle->gpio_num);
    
    // 删除队列
    if (handle->event_queue != NULL) {
        vQueueDelete(handle->event_queue);
    }
    
    free(handle);
    ESP_LOGI(TAG, "Button service deleted");
    
    return ESP_OK;
}

esp_err_t button_service_init_gpio(button_service_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << handle->gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = handle->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = handle->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,  // 边沿触发
    };
    
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(err));
        return err;
    }
    
    handle->last_raw_level = read_gpio_raw(handle);
    
    ESP_LOGI(TAG, "Button GPIO initialized: GPIO%d, active_%s", 
             handle->gpio_num, handle->active_low ? "low" : "high");
    
    return ESP_OK;
}

esp_err_t button_service_register_callback(button_service_handle_t handle,
                                           button_service_callback_t callback,
                                           void *user_data) {
    if (handle == NULL || callback == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    handle->callback = callback;
    handle->user_data = user_data;
    
    ESP_LOGD(TAG, "Callback registered for button %d", handle->button_id);
    
    return ESP_OK;
}

esp_err_t button_service_get_state(button_service_handle_t handle, bool *pressed) {
    if (handle == NULL || pressed == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    bool raw = read_gpio_raw(handle);
    *pressed = is_button_pressed(handle, raw);
    
    return ESP_OK;
}

void button_service_poll(button_service_handle_t handle) {
    if (handle == NULL) {
        return;
    }
    
    uint32_t now = get_tick_ms();
    bool raw_level = read_gpio_raw(handle);
    bool pressed = is_button_pressed(handle, raw_level);
    
    switch (handle->state) {
        case BTN_STATE_IDLE:
            if (pressed) {
                // 开始消抖
                handle->press_start_time = now;
                handle->state = BTN_STATE_DEBOUNCING;
            }
            break;
            
        case BTN_STATE_DEBOUNCING:
            if (pressed) {
                if (now - handle->press_start_time >= handle->debounce_ms) {
                    // 消抖通过，确认按下
                    handle->state = BTN_STATE_PRESSED;
                    send_event(handle, BUTTON_SERVICE_EVENT_PRESSED, 0);
                }
            } else {
                // 释放，忽略
                handle->state = BTN_STATE_IDLE;
            }
            break;
            
        case BTN_STATE_PRESSED:
            if (!pressed) {
                // 释放
                uint32_t duration = now - handle->press_start_time;
                handle->last_event_time = now;
                send_event(handle, BUTTON_SERVICE_EVENT_RELEASED, duration);
                send_event(handle, BUTTON_SERVICE_EVENT_CLICKED, duration);
                handle->state = BTN_STATE_IDLE;
            } else if (now - handle->press_start_time >= handle->long_press_ms) {
                // 达到长按阈值
                handle->state = BTN_STATE_LONG_PRESS;
                send_event(handle, BUTTON_SERVICE_EVENT_LONG_PRESSED, now - handle->press_start_time);
            }
            break;
            
        case BTN_STATE_LONG_PRESS:
            if (!pressed) {
                // 长按释放
                uint32_t duration = now - handle->press_start_time;
                handle->last_event_time = now;
                send_event(handle, BUTTON_SERVICE_EVENT_RELEASED, duration);
                handle->state = BTN_STATE_IDLE;
            }
            break;
    }
    
    handle->last_raw_level = raw_level;
}

esp_err_t button_service_wait_event(button_service_handle_t handle,
                                    button_service_event_t *event,
                                    uint32_t timeout_ms) {
    if (handle == NULL || event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (handle->event_queue != NULL &&
        xQueueReceive(handle->event_queue, event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}
