/**
 * @file dht11.c
 * @brief DHT11/DHT22 Temperature and Humidity Sensor Driver Implementation
 */

#include "dht11.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include <stdlib.h>

static const char *TAG = "dht11";

/** @brief DHT timing constants (microseconds) */
#define DHT_START_SIGNAL_US     18000   // Master pull-down start signal
#define DHT_RESPONSE_SIGNAL_US  80      // DHT response low pulse
#define DHT_RESPONSE_WAIT_US   80      // Wait before reading bits
#define DHT_BIT_0_LOW_US       50      // Bit 0: 50us low
#define DHT_BIT_0_HIGH_US      26      // Bit 0: 26us high
#define DHT_BIT_1_LOW_US       50      // Bit 1: 50us low
#define DHT_BIT_1_HIGH_US      70      // Bit 1: 70us high

/** @brief Read retry configuration */
#define DHT_MAX_RETRIES         3
#define DHT_RETRY_DELAY_MS      100

/** @brief Internal handle structure */
struct dht_handle_s {
    int8_t gpio_num;
    dht_type_t type;
    bool internal_pullup;
    bool initialized;
};

/**
 * @brief Wait for specified microseconds
 */
static void dht_delay_us(uint32_t us) {
    uint64_t start = esp_timer_get_time();
    while ((esp_timer_get_time() - start) < us) {
        // Busy wait
    }
}

/**
 * @brief Read a bit from DHT sensor
 */
static int dht_read_bit(dht_handle_t handle) {
    // Wait for start of high pulse
    uint32_t timeout = 100;
    while (gpio_get_level(handle->gpio_num) == 0 && timeout > 0) {
        dht_delay_us(1);
        timeout--;
    }
    
    if (timeout == 0) return -1;
    
    // Measure high pulse width
    timeout = 100;
    uint32_t start = esp_timer_get_time();
    while (gpio_get_level(handle->gpio_num) == 1 && timeout > 0) {
        dht_delay_us(1);
        timeout--;
    }
    
    uint32_t high_us = esp_timer_get_time() - start;
    
    // Bit 1 = high > 40us, Bit 0 = high <= 40us
    return (high_us > 40) ? 1 : 0;
}

/**
 * @brief Read one byte from DHT sensor
 */
static int dht_read_byte(dht_handle_t handle) {
    int value = 0;
    
    for (int i = 0; i < 8; i++) {
        int bit = dht_read_bit(handle);
        if (bit < 0) {
            ESP_LOGD(TAG, "Bit read timeout at bit %d", i);
            return -1;
        }
        value = (value << 1) | bit;
    }
    
    return value;
}

dht_handle_t dht_create(const dht_config_t *config) {
    if (config == NULL || config->gpio_num < 0) {
        ESP_LOGE(TAG, "Invalid config");
        return NULL;
    }
    
    dht_handle_t handle = (dht_handle_t)calloc(1, sizeof(struct dht_handle_s));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return NULL;
    }
    
    handle->gpio_num = config->gpio_num;
    handle->type = config->type;
    handle->internal_pullup = config->internal_pullup;
    handle->initialized = false;
    
    ESP_LOGI(TAG, "DHT created: GPIO%d, type=%s",
             config->gpio_num,
             config->type == DHT_TYPE_DHT11 ? "DHT11" :
             config->type == DHT_TYPE_DHT12 ? "DHT12" :
             config->type == DHT_TYPE_DHT21 ? "DHT21" : "DHT22");
    
    return handle;
}

void dht_delete(dht_handle_t handle) {
    if (handle == NULL) return;
    
    if (handle->initialized) {
        gpio_reset_pin(handle->gpio_num);
    }
    
    free(handle);
    ESP_LOGI(TAG, "DHT deleted");
}

int dht_init(dht_handle_t handle) {
    if (handle == NULL) return -1;
    if (handle->initialized) return 0;
    
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << handle->gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = handle->internal_pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(err));
        return -2;
    }
    
    // Initial pull-up to idle state
    gpio_set_level(handle->gpio_num, 1);
    
    handle->initialized = true;
    ESP_LOGI(TAG, "DHT initialized on GPIO%d", handle->gpio_num);
    
    return 0;
}

int dht_read(dht_handle_t handle, dht_data_t *data) {
    if (handle == NULL || data == NULL) return -1;
    
    data->valid = false;
    
    // Retry logic
    for (int retry = 0; retry < DHT_MAX_RETRIES; retry++) {
        uint8_t buf[5] = {0};
        
        // Step 1: Master send start signal (pull low for 18ms)
        gpio_set_direction(handle->gpio_num, GPIO_MODE_OUTPUT_OD);
        gpio_set_level(handle->gpio_num, 0);
        dht_delay_us(DHT_START_SIGNAL_US);
        
        // Step 2: Release bus and wait for response
        gpio_set_level(handle->gpio_num, 1);
        gpio_set_direction(handle->gpio_num, GPIO_MODE_INPUT);
        dht_delay_us(40);  // Wait for DHT to pull low
        
        // Step 3: DHT response signal (should be low for 80us)
        if (gpio_get_level(handle->gpio_num) != 0) {
            ESP_LOGD(TAG, "No DHT response, retry %d", retry);
            continue;
        }
        
        // Wait for response to end
        uint32_t timeout = 100;
        while (gpio_get_level(handle->gpio_num) == 0 && timeout > 0) {
            dht_delay_us(1);
            timeout--;
        }
        if (timeout == 0) {
            ESP_LOGD(TAG, "DHT response timeout, retry %d", retry);
            continue;
        }
        
        // Wait before reading data
        dht_delay_us(DHT_RESPONSE_WAIT_US);
        
        // Step 4: Read 40 bits (5 bytes)
        for (int i = 0; i < 5; i++) {
            int byte = dht_read_byte(handle);
            if (byte < 0) {
                ESP_LOGD(TAG, "Byte %d read failed, retry %d", i, retry);
                break;
            }
            buf[i] = (uint8_t)byte;
        }
        
        // Verify checksum
        uint8_t checksum = (buf[0] + buf[1] + buf[2] + buf[3]) & 0xFF;
        if (checksum != buf[4]) {
            ESP_LOGW(TAG, "Checksum mismatch: %02X != %02X (got %02X %02X %02X %02X %02X)",
                     checksum, buf[4], buf[0], buf[1], buf[2], buf[3], buf[4]);
            continue;
        }
        
        // Parse data based on sensor type
        if (handle->type == DHT_TYPE_DHT11 || handle->type == DHT_TYPE_DHT12) {
            // DHT11: humidity = integral, temperature = integral
            data->humidity = (float)buf[0];
            data->temperature = (float)buf[2];
        } else {
            // DHT21/DHT22: high byte is fractional, sign bit in temp high byte
            data->humidity = ((buf[0] << 8) | buf[1]) / 10.0f;
            int16_t temp_raw = (buf[2] << 8) | buf[3];
            data->temperature = temp_raw / 10.0f;
        }
        
        data->timestamp = esp_log_timestamp();
        data->valid = true;
        
        ESP_LOGD(TAG, "DHT read: T=%.1f°C, H=%.1f%%", data->temperature, data->humidity);
        return 0;
    }
    
    ESP_LOGE(TAG, "DHT read failed after %d retries", DHT_MAX_RETRIES);
    return -3;
}

int dht_read_temperature(dht_handle_t handle, float *temperature) {
    if (handle == NULL || temperature == NULL) return -1;
    
    dht_data_t data;
    int ret = dht_read(handle, &data);
    if (ret == 0) {
        *temperature = data.temperature;
    }
    return ret;
}

int dht_read_humidity(dht_handle_t handle, float *humidity) {
    if (handle == NULL || humidity == NULL) return -1;
    
    dht_data_t data;
    int ret = dht_read(handle, &data);
    if (ret == 0) {
        *humidity = data.humidity;
    }
    return ret;
}

int dht_check(dht_handle_t handle, bool *responsive) {
    if (handle == NULL || responsive == NULL) return -1;
    
    dht_data_t data;
    int ret = dht_read(handle, &data);
    *responsive = (ret == 0 && data.valid);
    return ret;
}
