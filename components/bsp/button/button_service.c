/**
 * @file button_service.c
 * @brief ESP Button Service 封装层实现
 * 
 * 使用 espressif/button 组件提供按钮事件处理
 */

#include "button_service.h"
#include "esp_log.h"
#include "esp_timer.h"
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

/** @brief 按钮实例结构 */
struct button_service_s {
    uint8_t button_id;
    button_handle_t button;         /**< espressif/button 句柄 */
    
    button_service_callback_t callback;
    void *user_data;
    
    QueueHandle_t event_queue;
};

/** @brief 全局时间获取 */
static inline uint32_t get_tick_ms(void) {
    return esp_log_timestamp();
}

/** @brief 转换事件类型 */
static button_service_event_type_t convert_button_event(button_cb_type_t type) {
    switch (type) {
        case BUTTON_CB_PUSH:
            return BUTTON_SERVICE_EVENT_PRESSED;
        case BUTTON_CB_RELEASE:
            return BUTTON_SERVICE_EVENT_RELEASED;
        case BUTTON_CB_TAP:
            return BUTTON_SERVICE_EVENT_CLICKED;
        case BUTTON_CB_LONG_PRESS_START:
        case BUTTON_CB_LONG_PRESS_HOLD:
            return BUTTON_SERVICE_EVENT_LONG_PRESSED;
        default:
            return BUTTON_SERVICE_EVENT_CLICKED;
    }
}

/** @brief espressif/button 内部回调 */
static void button_internal_callback(void *param) {
    button_service_handle_t handle = (button_service_handle_t)param;
    if (handle == NULL || handle->callback == NULL) {
        return;
    }
    
    button_cb_type_t btn_event = iot_button_get_event(handle->button);
    
    button_service_event_t event = {
        .button_id = handle->button_id,
        .type = convert_button_event(btn_event),
        .press_duration_ms = 0,
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
    
    // 调用用户回调
    handle->callback(&event, handle->user_data);
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
    handle->callback = NULL;
    handle->user_data = NULL;
    
    // 创建事件队列
    handle->event_queue = xQueueCreate(16, sizeof(button_service_event_t));
    if (handle->event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        free(handle);
        return NULL;
    }
    
    // 配置 GPIO 按钮参数
    button_config_t btn_cfg = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = config->gpio_num,
            .active_level = config->active_low ? 0 : 1,
        },
    };
    
    // 创建按钮
    handle->button = iot_button_create(&btn_cfg);
    if (handle->button == NULL) {
        ESP_LOGE(TAG, "Failed to create button: GPIO%d", config->gpio_num);
        vQueueDelete(handle->event_queue);
        free(handle);
        return NULL;
    }
    
    // 设置长按时间
    uint32_t long_press_ms = (config->long_press_ms > 0) ? config->long_press_ms : DEFAULT_LONG_PRESS_MS;
    iot_button_set_long_press_time(handle->button, long_press_ms);
    
    ESP_LOGI(TAG, "Button service created: GPIO%d, button_id=%d, long_press=%ums",
             config->gpio_num, config->button_id, long_press_ms);
    
    return handle;
}

esp_err_t button_service_delete(button_service_handle_t handle) {
    if (handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 删除按钮
    if (handle->button != NULL) {
        iot_button_delete(handle->button);
        handle->button = NULL;
    }
    
    // 删除队列
    if (handle->event_queue != NULL) {
        vQueueDelete(handle->event_queue);
        handle->event_queue = NULL;
    }
    
    free(handle);
    ESP_LOGI(TAG, "Button service deleted");
    
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
    
    // 注册 espressif/button 回调
    iot_button_register_cb(handle->button, BUTTON_CB_TAP, button_internal_callback, handle);
    iot_button_register_cb(handle->button, BUTTON_CB_PUSH, button_internal_callback, handle);
    iot_button_register_cb(handle->button, BUTTON_CB_RELEASE, button_internal_callback, handle);
    iot_button_register_cb(handle->button, BUTTON_CB_LONG_PRESS_START, button_internal_callback, handle);
    iot_button_register_cb(handle->button, BUTTON_CB_LONG_PRESS_HOLD, button_internal_callback, handle);
    
    ESP_LOGD(TAG, "Callback registered for button %d", handle->button_id);
    
    return ESP_OK;
}

esp_err_t button_service_get_state(button_service_handle_t handle, bool *pressed) {
    if (handle == NULL || pressed == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (handle->button == NULL) {
        *pressed = false;
        return ESP_ERR_INVALID_ARG;
    }
    
    *pressed = iot_button_get_state(handle->button) == BUTTON_PRESS_DOWN;
    
    return ESP_OK;
}

void button_service_poll(button_service_handle_t handle) {
    // espressif/button 组件使用定时器自动处理，无需轮询
    // 保留此函数以兼容现有代码
    (void)handle;
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
