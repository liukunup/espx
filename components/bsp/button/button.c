/**
 * @file button.c
 * @brief Button Driver Implementation
 */

#include "button.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "button";

/** @brief Default timing values */
#define DEFAULT_DEBOUNCE_MS     50
#define DEFAULT_LONG_PRESS_MS   1000
#define DEFAULT_DOUBLE_CLICK_MS 300
#define DEFAULT_POLL_MS         10

/** @brief Button states */
typedef enum {
    BUTTON_STATE_IDLE = 0,
    BUTTON_STATE_DEBOUNCING,
    BUTTON_STATE_PRESSED,
    BUTTON_STATE_LONG_PRESS,
} button_state_t;

/** @brief Internal button structure */
struct button_s {
    uint8_t button_id;
    int8_t gpio_num;
    button_active_level_t active_level;
    button_pull_mode_t pull_mode;
    uint32_t debounce_ms;
    uint32_t long_press_ms;
    uint32_t double_click_ms;
    
    button_state_t state;
    uint32_t press_start_time;
    uint32_t last_release_time;
    bool last_raw_state;
    
    button_callback_t callback;
    void *user_data;
};

/** @brief Button handle (opaque pointer to internal structure) */
struct button_handle_s {
    struct button_s btn;
    QueueHandle_t event_queue;
    button_manager_handle_t manager;  // NULL if standalone
};

/** @brief Button manager structure */
struct button_manager_s {
    button_handle_t buttons[BUTTON_MAX_COUNT];
    uint8_t button_count;
    uint32_t poll_interval_ms;
};

/**
 * @brief Get current time in milliseconds
 */
static uint32_t get_tick_ms(void) {
    return esp_log_timestamp();
}

/**
 * @brief Read raw GPIO state
 */
static bool read_gpio(button_handle_t handle) {
    return gpio_get_level(handle->btn.gpio_num) == 1;
}

/**
 * @brief Check if button is currently pressed
 */
static bool is_pressed(button_handle_t handle, bool raw_state) {
    if (handle->btn.active_level == BUTTON_ACTIVE_LOW) {
        return !raw_state;
    } else {
        return raw_state;
    }
}

/**
 * @brief Send event to queue and/or callback
 */
static void send_event(button_handle_t handle, button_event_type_t type, uint32_t duration_ms) {
    button_event_t event = {
        .button_id = handle->btn.button_id,
        .type = type,
        .press_duration_ms = duration_ms,
        .timestamp = get_tick_ms()
    };
    
    // Send to queue
    if (handle->event_queue != NULL) {
        BaseType_t woken = pdFALSE;
        xQueueSendFromISR(handle->event_queue, &event, &woken);
        if (woken) {
            portYIELD_FROM_ISR();
        }
    }
    
    // Call callback
    if (handle->btn.callback != NULL) {
        handle->btn.callback(&event, handle->btn.user_data);
    }
}

button_handle_t button_create(const button_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    
    button_handle_t handle = (button_handle_t)calloc(1, sizeof(struct button_handle_s));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return NULL;
    }
    
    handle->btn.button_id = config->button_id;
    handle->btn.gpio_num = config->gpio_num;
    handle->btn.active_level = config->active_level;
    handle->btn.pull_mode = config->pull_mode;
    handle->btn.debounce_ms = config->debounce_ms > 0 ? config->debounce_ms : DEFAULT_DEBOUNCE_MS;
    handle->btn.long_press_ms = config->long_press_ms > 0 ? config->long_press_ms : DEFAULT_LONG_PRESS_MS;
    handle->btn.double_click_ms = config->double_click_ms > 0 ? config->double_click_ms : DEFAULT_DOUBLE_CLICK_MS;
    
    handle->btn.state = BUTTON_STATE_IDLE;
    handle->btn.press_start_time = 0;
    handle->btn.last_release_time = 0;
    handle->btn.last_raw_state = false;
    handle->btn.callback = NULL;
    handle->btn.user_data = NULL;
    
    handle->event_queue = xQueueCreate(16, sizeof(button_event_t));
    if (handle->event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create event queue");
        free(handle);
        return NULL;
    }
    
    ESP_LOGI(TAG, "Button created: id=%d, GPIO%d, active=%s",
             config->button_id, config->gpio_num,
             (config->active_level == BUTTON_ACTIVE_LOW) ? "LOW" : "HIGH");
    
    return handle;
}

void button_delete(button_handle_t handle) {
    if (handle == NULL) return;
    
    if (handle->event_queue != NULL) {
        vQueueDelete(handle->event_queue);
    }
    
    if (handle->btn.gpio_num >= 0) {
        gpio_reset_pin(handle->btn.gpio_num);
    }
    
    free(handle);
    ESP_LOGI(TAG, "Button deleted");
}

int button_init(button_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    if (handle->btn.gpio_num < 0) {
        ESP_LOGI(TAG, "Button disabled (GPIO not configured)");
        return 0;
    }
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << handle->btn.gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = (handle->btn.pull_mode == BUTTON_PULL_UP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (handle->btn.pull_mode == BUTTON_PULL_DOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(err));
        return -2;
    }
    
    handle->btn.last_raw_state = read_gpio(handle);
    
    ESP_LOGI(TAG, "Button initialized: GPIO%d", handle->btn.gpio_num);
    
    return 0;
}

void button_poll(button_handle_t handle) {
    if (handle == NULL || handle->btn.gpio_num < 0) {
        return;
    }
    
    uint32_t now = get_tick_ms();
    bool raw_state = read_gpio(handle);
    bool pressed = is_pressed(handle, raw_state);
    
    switch (handle->btn.state) {
        case BUTTON_STATE_IDLE:
            if (pressed) {
                // Button pressed, start debounce
                handle->btn.press_start_time = now;
                handle->btn.state = BUTTON_STATE_DEBOUNCING;
            }
            break;
            
        case BUTTON_STATE_DEBOUNCING:
            if (pressed) {
                if (now - handle->btn.press_start_time >= handle->btn.debounce_ms) {
                    // Debounce passed, button is pressed
                    handle->btn.state = BUTTON_STATE_PRESSED;
                    send_event(handle, BUTTON_EVENT_PRESSED, 0);
                }
            } else {
                // Released during debounce, go back to idle
                handle->btn.state = BUTTON_STATE_IDLE;
            }
            break;
            
        case BUTTON_STATE_PRESSED:
            if (!pressed) {
                // Button released
                uint32_t duration = now - handle->btn.press_start_time;
                
                // Check for double click
                if (duration < handle->btn.double_click_ms && 
                    now - handle->btn.last_release_time < handle->btn.double_click_ms) {
                    send_event(handle, BUTTON_EVENT_DOUBLE_CLICK, duration);
                } else {
                    send_event(handle, BUTTON_EVENT_CLICKED, duration);
                }
                
                handle->btn.last_release_time = now;
                send_event(handle, BUTTON_EVENT_RELEASED, duration);
                handle->btn.state = BUTTON_STATE_IDLE;
            } else if (now - handle->btn.press_start_time >= handle->btn.long_press_ms) {
                // Long press threshold reached
                handle->btn.state = BUTTON_STATE_LONG_PRESS;
                send_event(handle, BUTTON_EVENT_LONG_PRESSED, now - handle->btn.press_start_time);
            }
            break;
            
        case BUTTON_STATE_LONG_PRESS:
            if (!pressed) {
                // Button finally released after long press
                uint32_t duration = now - handle->btn.press_start_time;
                
                send_event(handle, BUTTON_EVENT_LONG_RELEASED, duration);
                send_event(handle, BUTTON_EVENT_RELEASED, duration);
                handle->btn.last_release_time = now;
                handle->btn.state = BUTTON_STATE_IDLE;
            }
            break;
    }
    
    handle->btn.last_raw_state = raw_state;
}

int button_check_event(button_handle_t handle, button_event_t *event) {
    if (handle == NULL || event == NULL) {
        return -1;
    }
    
    if (handle->event_queue != NULL && xQueueReceive(handle->event_queue, event, 0) == pdTRUE) {
        return 0;
    }
    
    return -1;
}

int button_wait_event(button_handle_t handle, button_event_t *event, uint32_t timeout_ms) {
    if (handle == NULL || event == NULL) {
        return -1;
    }
    
    if (handle->event_queue != NULL && 
        xQueueReceive(handle->event_queue, event, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return 0;
    }
    
    return ESP_ERR_TIMEOUT;
}

int button_register_callback(button_handle_t handle, button_callback_t callback, void *user_data) {
    if (handle == NULL) {
        return -1;
    }
    
    handle->btn.callback = callback;
    handle->btn.user_data = user_data;
    
    return 0;
}

int button_get_state(button_handle_t handle, bool *pressed) {
    if (handle == NULL || pressed == NULL) {
        return -1;
    }
    
    if (handle->btn.gpio_num < 0) {
        *pressed = false;
        return 0;
    }
    
    bool raw = read_gpio(handle);
    *pressed = is_pressed(handle, raw);
    
    return 0;
}

uint32_t button_event_queue_size(button_handle_t handle) {
    if (handle == NULL || handle->event_queue == NULL) {
        return 0;
    }
    
    return uxQueueMessagesWaiting(handle->event_queue);
}

void button_clear_queue(button_handle_t handle) {
    if (handle == NULL || handle->event_queue == NULL) {
        return;
    }
    
    button_event_t event;
    while (xQueueReceive(handle->event_queue, &event, 0) == pdTRUE) {
        // Drain queue
    }
}

// ============= Button Manager =============

button_manager_handle_t button_manager_create(const button_manager_config_t *config) {
    button_manager_handle_t handle = (button_manager_handle_t)calloc(1, sizeof(struct button_manager_s));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate manager");
        return NULL;
    }
    
    handle->button_count = 0;
    handle->poll_interval_ms = (config && config->poll_interval_ms > 0) ? 
                                config->poll_interval_ms : DEFAULT_POLL_MS;
    
    ESP_LOGI(TAG, "Button manager created");
    
    return handle;
}

void button_manager_delete(button_manager_handle_t handle) {
    if (handle == NULL) return;
    
    // Delete all buttons
    for (int i = 0; i < handle->button_count; i++) {
        if (handle->buttons[i] != NULL) {
            button_delete(handle->buttons[i]);
            handle->buttons[i] = NULL;
        }
    }
    
    free(handle);
    ESP_LOGI(TAG, "Button manager deleted");
}

button_handle_t button_manager_add_button(button_manager_handle_t handle, const button_config_t *config) {
    if (handle == NULL || config == NULL) {
        return NULL;
    }
    
    if (handle->button_count >= BUTTON_MAX_COUNT) {
        ESP_LOGE(TAG, "Max buttons reached (%d)", BUTTON_MAX_COUNT);
        return NULL;
    }
    
    button_handle_t btn = button_create(config);
    if (btn == NULL) {
        return NULL;
    }
    
    btn->manager = handle;
    handle->buttons[handle->button_count++] = btn;
    
    return btn;
}

int button_manager_remove_button(button_manager_handle_t handle, button_handle_t button) {
    if (handle == NULL || button == NULL) {
        return -1;
    }
    
    for (int i = 0; i < handle->button_count; i++) {
        if (handle->buttons[i] == button) {
            // Remove from array
            for (int j = i; j < handle->button_count - 1; j++) {
                handle->buttons[j] = handle->buttons[j + 1];
            }
            handle->button_count--;
            
            button_delete(button);
            return 0;
        }
    }
    
    return -1;  // Not found
}

void button_manager_poll(button_manager_handle_t handle) {
    if (handle == NULL) return;
    
    for (int i = 0; i < handle->button_count; i++) {
        if (handle->buttons[i] != NULL) {
            button_poll(handle->buttons[i]);
        }
    }
}

int button_manager_wait_event(button_manager_handle_t handle, button_event_t *event, uint32_t timeout_ms) {
    if (handle == NULL || event == NULL) {
        return -1;
    }
    
    uint32_t start = get_tick_ms();
    
    while (get_tick_ms() - start < timeout_ms) {
        button_manager_poll(handle);
        
        for (int i = 0; i < handle->button_count; i++) {
            if (handle->buttons[i] != NULL) {
                if (button_check_event(handle->buttons[i], event) == 0) {
                    return 0;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(handle->poll_interval_ms));
    }
    
    return ESP_ERR_TIMEOUT;
}
