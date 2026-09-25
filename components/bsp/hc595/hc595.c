/**
 * @file hc595.c
 * @brief 74HC595 Shift Register Driver Implementation
 */

#include "hc595.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "hc595";

/** @brief Maximum cascaded chips */
#define HC595_MAX_CHIPS 8

/** @brief Bits per chip */
#define HC595_BITS_PER_CHIP 8

/** @brief Internal handle structure */
struct hc595_handle_s {
    int8_t ser_gpio;       // Serial data
    int8_t rclk_gpio;      // Latch clock
    int8_t srclk_gpio;     // Shift clock
    int8_t srclr_gpio;     // Clear (or -1)
    uint8_t num_chips;     // Number of cascaded chips
    hc595_polarity_t polarity;
    uint32_t output_value; // Current output state
    bool initialized;
};

/**
 * @brief Hardware delay for GPIO operations
 */
static void hc595_delay(void) {
    // Small delay for signal stability (~1us at 80MHz)
    for (volatile int i = 0; i < 10; i++);
}

/**
 * @brief Write single bit to shift register
 */
static void shift_bit(hc595_handle_t handle, bool bit) {
    gpio_set_level(handle->ser_gpio, bit ? 1 : 0);
    hc595_delay();
    
    gpio_set_level(handle->srclk_gpio, 1);
    hc595_delay();
    gpio_set_level(handle->srclk_gpio, 0);
    hc595_delay();
}

/**
 * @brief Latch data to outputs
 */
static void latch_data(hc595_handle_t handle) {
    gpio_set_level(handle->rclk_gpio, 1);
    hc595_delay();
    gpio_set_level(handle->rclk_gpio, 0);
    hc595_delay();
}

/**
 * @brief Clear shift register
 */
static void clear_shift_register(hc595_handle_t handle) {
    if (handle->srclr_gpio >= 0) {
        gpio_set_level(handle->srclr_gpio, 0);
        hc595_delay();
        gpio_set_level(handle->srclr_gpio, 1);
    }
}

hc595_handle_t hc595_create(const hc595_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    
    if (config->num_chips == 0 || config->num_chips > HC595_MAX_CHIPS) {
        ESP_LOGE(TAG, "Invalid number of chips: %d (max: %d)", 
                 config->num_chips, HC595_MAX_CHIPS);
        return NULL;
    }
    
    if (config->ser_gpio < 0 || config->rclk_gpio < 0 || config->srclk_gpio < 0) {
        ESP_LOGE(TAG, "Invalid GPIO pins: ser=%d, rclk=%d, srclk=%d",
                 config->ser_gpio, config->rclk_gpio, config->srclk_gpio);
        return NULL;
    }
    
    hc595_handle_t handle = (hc595_handle_t)calloc(1, sizeof(struct hc595_handle_s));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return NULL;
    }
    
    handle->ser_gpio = config->ser_gpio;
    handle->rclk_gpio = config->rclk_gpio;
    handle->srclk_gpio = config->srclk_gpio;
    handle->srclr_gpio = config->srclr_gpio;
    handle->num_chips = config->num_chips;
    handle->polarity = config->polarity;
    handle->output_value = 0;
    handle->initialized = false;
    
    ESP_LOGI(TAG, "HC595 created: ser=GPIO%d, rclk=GPIO%d, srclk=GPIO%d, chips=%d",
             config->ser_gpio, config->rclk_gpio, config->srclk_gpio, 
             config->num_chips);
    
    return handle;
}

void hc595_delete(hc595_handle_t handle) {
    if (handle == NULL) return;
    
    if (handle->initialized) {
        // Clear outputs and reset pins
        hc595_clear(handle);
        hc595_update(handle);
        
        gpio_reset_pin(handle->ser_gpio);
        gpio_reset_pin(handle->rclk_gpio);
        gpio_reset_pin(handle->srclk_gpio);
        
        if (handle->srclr_gpio >= 0) {
            gpio_reset_pin(handle->srclr_gpio);
        }
    }
    
    free(handle);
    ESP_LOGI(TAG, "HC595 deleted");
}

int hc595_init(hc595_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    if (handle->initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return 0;
    }
    
    // Configure GPIO pins
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << handle->ser_gpio) | 
                        (1ULL << handle->rclk_gpio) | 
                        (1ULL << handle->srclk_gpio),
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
    
    // Configure SRCLR if used
    if (handle->srclr_gpio >= 0) {
        gpio_config_t clr_conf = {
            .pin_bit_mask = (1ULL << handle->srclr_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        
        err = gpio_config(&clr_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SRCLR GPIO config failed: %s", esp_err_to_name(err));
            return -3;
        }
        
        // SRCLR is active low, so set high to enable
        gpio_set_level(handle->srclr_gpio, 1);
    }
    
    // Initialize all pins to low
    gpio_set_level(handle->ser_gpio, 0);
    gpio_set_level(handle->rclk_gpio, 0);
    gpio_set_level(handle->srclk_gpio, 0);
    
    handle->initialized = true;
    ESP_LOGI(TAG, "HC595 initialized");
    
    return 0;
}

int hc595_set_bit(hc595_handle_t handle, uint8_t bit, bool state) {
    if (handle == NULL) {
        return -1;
    }
    
    uint8_t max_bits = handle->num_chips * HC595_BITS_PER_CHIP;
    if (bit >= max_bits) {
        ESP_LOGW(TAG, "Bit %d out of range (max: %d)", bit, max_bits - 1);
        return -2;
    }
    
    if (state) {
        handle->output_value |= (1UL << bit);
    } else {
        handle->output_value &= ~(1UL << bit);
    }
    
    return 0;
}

int hc595_get_bit(hc595_handle_t handle, uint8_t bit, bool *state) {
    if (handle == NULL || state == NULL) {
        return -1;
    }
    
    uint8_t max_bits = handle->num_chips * HC595_BITS_PER_CHIP;
    if (bit >= max_bits) {
        ESP_LOGW(TAG, "Bit %d out of range (max: %d)", bit, max_bits - 1);
        return -2;
    }
    
    *state = (handle->output_value & (1UL << bit)) != 0;
    return 0;
}

int hc595_set_value(hc595_handle_t handle, uint32_t value) {
    if (handle == NULL) {
        return -1;
    }
    
    uint8_t max_bits = handle->num_chips * HC595_BITS_PER_CHIP;
    uint32_t mask = (1UL << max_bits) - 1;
    
    handle->output_value = value & mask;
    return 0;
}

int hc595_get_value(hc595_handle_t handle, uint32_t *value) {
    if (handle == NULL || value == NULL) {
        return -1;
    }
    
    *value = handle->output_value;
    return 0;
}

int hc595_clear(hc595_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    handle->output_value = 0;
    return 0;
}

int hc595_set_all(hc595_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    uint8_t max_bits = handle->num_chips * HC595_BITS_PER_CHIP;
    handle->output_value = (1UL << max_bits) - 1;
    
    return 0;
}

int hc595_update(hc595_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    if (!handle->initialized) {
        return -2;
    }
    
    // Clear shift register
    clear_shift_register(handle);
    
    // Get value to shift (considering polarity)
    uint32_t value = handle->output_value;
    if (handle->polarity == HC595_POLARITY_INVERTED) {
        uint8_t max_bits = handle->num_chips * HC595_BITS_PER_CHIP;
        value = (~value) & ((1UL << max_bits) - 1);
    }
    
    // Shift out all bits (MSB first for cascaded chips)
    uint8_t total_bits = handle->num_chips * HC595_BITS_PER_CHIP;
    for (int8_t i = total_bits - 1; i >= 0; i--) {
        bool bit = (value & (1UL << i)) != 0;
        shift_bit(handle, bit);
    }
    
    // Latch data to outputs
    latch_data(handle);
    
    ESP_LOGD(TAG, "HC595 updated: 0x%08X", handle->output_value);
    return 0;
}

int hc595_write(hc595_handle_t handle, uint32_t value) {
    if (handle == NULL) {
        return -1;
    }
    
    int ret = hc595_set_value(handle, value);
    if (ret != 0) {
        return ret;
    }
    
    return hc595_update(handle);
}

int hc595_write_bit(hc595_handle_t handle, uint8_t bit, bool state) {
    if (handle == NULL) {
        return -1;
    }
    
    int ret = hc595_set_bit(handle, bit, state);
    if (ret != 0) {
        return ret;
    }
    
    return hc595_update(handle);
}
