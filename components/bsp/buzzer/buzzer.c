/**
 * @file buzzer.c
 * @brief Buzzer Driver Implementation
 */

#include "buzzer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <math.h>

static const char *TAG = "buzzer";

/** @brief LEDC timer configuration */
#define BUZZER_LEDC_TIMER      LEDC_TIMER_0
#define BUZZER_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define BUZZER_LEDC_DUTY_RES   LEDC_TIMER_10_BIT  // 1024 levels
#define BUZZER_LEDC_FREQ_BASE  1000               // Base frequency for calculations

/** @brief Internal handle structure */
struct buzzer_handle_s {
    int8_t gpio_num;
    buzzer_type_t type;
    uint32_t current_freq;
    bool is_on;
    bool initialized;
    ledc_channel_config_t ledc_channel;
};

/**
 * @brief Set PWM frequency
 */
static int set_pwm_frequency(buzzer_handle_t handle, uint32_t freq) {
    if (handle->gpio_num < 0 || !handle->initialized) {
        return -1;
    }
    
    // Stop current PWM
    ledc_stop(BUZZER_LEDC_MODE, LEDC_CHANNEL_0, 0);
    
    // Calculate duty for 50% volume
    uint32_t duty = (1 << BUZZER_LEDC_DUTY_RES) / 2;
    
    // Setup channel with new frequency
    handle->ledc_channel.channel = LEDC_CHANNEL_0;
    handle->ledc_channel.freq_hz = freq;
    
    esp_err_t err = ledc_channel_config(&handle->ledc_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(err));
        return -2;
    }
    
    err = ledc_set_duty(BUZZER_LEDC_MODE, LEDC_CHANNEL_0, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC set duty failed: %s", esp_err_to_name(err));
        return -3;
    }
    
    err = ledc_update_duty(BUZZER_LEDC_MODE, LEDC_CHANNEL_0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC update duty failed: %s", esp_err_to_name(err));
        return -4;
    }
    
    handle->current_freq = freq;
    ESP_LOGD(TAG, "Buzzer frequency set to %lu Hz", freq);
    
    return 0;
}

buzzer_handle_t buzzer_create(const buzzer_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    
    buzzer_handle_t handle = (buzzer_handle_t)calloc(1, sizeof(struct buzzer_handle_s));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return NULL;
    }
    
    handle->gpio_num = config->gpio_num;
    handle->type = config->type;
    handle->current_freq = config->default_freq > 0 ? config->default_freq : 1000;
    handle->is_on = false;
    handle->initialized = false;
    
    // Configure LEDC channel
    handle->ledc_channel.gpio_num = config->gpio_num;
    handle->ledc_channel.speed_mode = BUZZER_LEDC_MODE;
    handle->ledc_channel.channel = LEDC_CHANNEL_0;
    handle->ledc_channel.intr_type = LEDC_INTR_DISABLE;
    handle->ledc_channel.duty = 0;
    handle->ledc_channel.hpoint = 0;
    
    ESP_LOGI(TAG, "Buzzer created: GPIO%d, type=%s, freq=%lu Hz",
             config->gpio_num,
             (config->type == BUZZER_TYPE_ACTIVE) ? "active" : "passive",
             handle->current_freq);
    
    return handle;
}

void buzzer_delete(buzzer_handle_t handle) {
    if (handle == NULL) return;
    
    if (handle->initialized) {
        buzzer_off(handle);
        gpio_reset_pin(handle->gpio_num);
    }
    
    free(handle);
    ESP_LOGI(TAG, "Buzzer deleted");
}

int buzzer_init(buzzer_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    if (handle->initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return 0;
    }
    
    if (handle->gpio_num < 0) {
        ESP_LOGI(TAG, "Buzzer disabled (GPIO not configured)");
        handle->initialized = true;
        return 0;
    }
    
    if (handle->type == BUZZER_TYPE_PASSIVE) {
        // Setup LEDC timer for PWM
        ledc_timer_config_t ledc_timer = {
            .speed_mode = BUZZER_LEDC_MODE,
            .timer_num = BUZZER_LEDC_TIMER,
            .duty_resolution = BUZZER_LEDC_DUTY_RES,
            .freq_hz = handle->current_freq,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        
        esp_err_t err = ledc_timer_config(&ledc_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(err));
            return -2;
        }
        
        // Setup channel
        handle->ledc_channel.speed_mode = BUZZER_LEDC_MODE;
        handle->ledc_channel.timer_sel = BUZZER_LEDC_TIMER;
        
        err = ledc_channel_config(&handle->ledc_channel);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(err));
            return -3;
        }
    } else {
        // Active buzzer - just GPIO output
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << handle->gpio_num),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        
        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(err));
            return -4;
        }
        
        gpio_set_level(handle->gpio_num, 0);
    }
    
    handle->initialized = true;
    ESP_LOGI(TAG, "Buzzer initialized on GPIO%d", handle->gpio_num);
    
    return 0;
}

int buzzer_on(buzzer_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    if (!handle->initialized) {
        return -2;
    }
    
    if (handle->type == BUZZER_TYPE_ACTIVE) {
        gpio_set_level(handle->gpio_num, 1);
    } else {
        // Passive buzzer - start PWM
        uint32_t duty = (1 << BUZZER_LEDC_DUTY_RES) / 2;
        ledc_set_duty(BUZZER_LEDC_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(BUZZER_LEDC_MODE, LEDC_CHANNEL_0);
    }
    
    handle->is_on = true;
    return 0;
}

int buzzer_off(buzzer_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    if (!handle->initialized) {
        return -2;
    }
    
    if (handle->type == BUZZER_TYPE_ACTIVE) {
        gpio_set_level(handle->gpio_num, 0);
    } else {
        // Passive buzzer - stop PWM
        ledc_set_duty(BUZZER_LEDC_MODE, LEDC_CHANNEL_0, 0);
        ledc_update_duty(BUZZER_LEDC_MODE, LEDC_CHANNEL_0);
    }
    
    handle->is_on = false;
    return 0;
}

int buzzer_set_freq(buzzer_handle_t handle, uint32_t freq) {
    if (handle == NULL) {
        return -1;
    }
    
    if (freq < 100 || freq > 10000) {
        ESP_LOGW(TAG, "Frequency out of range: %lu (valid: 100-10000)", freq);
        return -2;
    }
    
    if (handle->type != BUZZER_TYPE_PASSIVE) {
        ESP_LOGW(TAG, "Frequency setting only for passive buzzer");
        return -3;
    }
    
    handle->current_freq = freq;
    
    if (handle->is_on) {
        return set_pwm_frequency(handle, freq);
    }
    
    return 0;
}

int buzzer_tone(buzzer_handle_t handle, uint32_t freq, uint32_t duration_ms) {
    if (handle == NULL) {
        return -1;
    }
    
    if (handle->type != BUZZER_TYPE_PASSIVE) {
        ESP_LOGW(TAG, "Tone only for passive buzzer");
        return -2;
    }
    
    // Set frequency and turn on
    int ret = buzzer_set_freq(handle, freq);
    if (ret != 0) {
        return ret;
    }
    
    ret = buzzer_on(handle);
    if (ret != 0) {
        return ret;
    }
    
    // If duration specified, turn off after delay
    if (duration_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(duration_ms));
        buzzer_off(handle);
    }
    
    return 0;
}

int buzzer_beep(buzzer_handle_t handle, uint32_t on_ms, uint32_t off_ms, uint8_t count) {
    if (handle == NULL) {
        return -1;
    }
    
    if (handle->type != BUZZER_TYPE_PASSIVE) {
        ESP_LOGW(TAG, "Beep only for passive buzzer");
        return -2;
    }
    
    for (uint8_t i = 0; i < count; i++) {
        buzzer_on(handle);
        vTaskDelay(pdMS_TO_TICKS(on_ms));
        buzzer_off(handle);
        
        if (i < count - 1 && off_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(off_ms));
        }
    }
    
    return 0;
}

int buzzer_play_note(buzzer_handle_t handle, uint8_t note, uint32_t duration_ms) {
    if (handle == NULL) {
        return -1;
    }
    
    uint32_t freq = buzzer_note_to_freq(note);
    return buzzer_tone(handle, freq, duration_ms);
}

int buzzer_get_state(buzzer_handle_t handle, bool *on) {
    if (handle == NULL || on == NULL) {
        return -1;
    }
    
    *on = handle->is_on;
    return 0;
}

uint32_t buzzer_note_to_freq(uint8_t note) {
    // A4 (note 69) = 440Hz
    // f = 440 * 2^((n-69)/12)
    double freq = 440.0 * pow(2.0, (note - 69) / 12.0);
    return (uint32_t)(freq + 0.5);
}
