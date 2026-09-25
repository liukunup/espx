/**
 * @file relay.c
 * @brief Relay Driver Implementation
 */

#include "relay.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include <stdlib.h>

static const char *TAG = "relay";

/** @brief Internal handle structure */
struct relay_handle_s {
    int8_t gpio_num;
    relay_active_level_t active_level;
    bool state;
    bool initialized;
};

/**
 * @brief Set GPIO output level based on active level
 */
static void set_gpio_level(relay_handle_t handle, bool on) {
    if (handle->gpio_num < 0) return;
    
    uint32_t level;
    if (on) {
        level = (handle->active_level == RELAY_ACTIVE_HIGH) ? 1 : 0;
    } else {
        level = (handle->active_level == RELAY_ACTIVE_HIGH) ? 0 : 1;
    }
    
    gpio_set_level(handle->gpio_num, level);
}

relay_handle_t relay_create(const relay_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    
    relay_handle_t handle = (relay_handle_t)calloc(1, sizeof(struct relay_handle_s));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return NULL;
    }
    
    handle->gpio_num = config->gpio_num;
    handle->active_level = config->level;
    handle->state = config->default_state;
    handle->initialized = false;
    
    ESP_LOGI(TAG, "Relay created: GPIO%d, active %s, default %s",
             config->gpio_num,
             (config->level == RELAY_ACTIVE_HIGH) ? "HIGH" : "LOW",
             config->default_state ? "ON" : "OFF");
    
    return handle;
}

void relay_delete(relay_handle_t handle) {
    if (handle == NULL) return;
    
    if (handle->initialized && handle->gpio_num >= 0) {
        // Turn off relay before deletion
        set_gpio_level(handle, false);
        gpio_reset_pin(handle->gpio_num);
    }
    
    free(handle);
    ESP_LOGI(TAG, "Relay deleted");
}

int relay_init(relay_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    if (handle->initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return 0;
    }
    
    if (handle->gpio_num < 0) {
        ESP_LOGI(TAG, "Relay disabled (GPIO not configured)");
        handle->initialized = true;
        return 0;
    }
    
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
        return -2;
    }
    
    // Set initial state
    set_gpio_level(handle, handle->state);
    
    handle->initialized = true;
    ESP_LOGI(TAG, "Relay initialized on GPIO%d", handle->gpio_num);
    
    return 0;
}

int relay_on(relay_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    if (!handle->initialized) {
        return -2;
    }
    
    handle->state = true;
    set_gpio_level(handle, true);
    
    ESP_LOGD(TAG, "Relay ON (GPIO%d)", handle->gpio_num);
    return 0;
}

int relay_off(relay_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    if (!handle->initialized) {
        return -2;
    }
    
    handle->state = false;
    set_gpio_level(handle, false);
    
    ESP_LOGD(TAG, "Relay OFF (GPIO%d)", handle->gpio_num);
    return 0;
}

int relay_toggle(relay_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    if (!handle->initialized) {
        return -2;
    }
    
    handle->state = !handle->state;
    set_gpio_level(handle, handle->state);
    
    ESP_LOGD(TAG, "Relay toggled to %s (GPIO%d)", 
             handle->state ? "ON" : "OFF", handle->gpio_num);
    
    return handle->state ? 1 : 0;
}

int relay_set(relay_handle_t handle, bool on) {
    if (handle == NULL) {
        return -1;
    }
    
    if (!handle->initialized) {
        return -2;
    }
    
    if (on) {
        return relay_on(handle);
    } else {
        return relay_off(handle);
    }
}

int relay_get_state(relay_handle_t handle, bool *state) {
    if (handle == NULL || state == NULL) {
        return -1;
    }
    
    *state = handle->state;
    return 0;
}
