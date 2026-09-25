/**
 * @file ads1115.c
 * @brief ADS1115 16-bit ADC Driver Implementation
 */

#include "ads1115.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_err.h"
#include <stdlib.h>

static const char *TAG = "ads1115";

/** @brief ADS1115 register addresses */
#define ADS1115_REG_CONVERSION   0x00
#define ADS1115_REG_CONFIG       0x01
#define ADS1115_REG_LO_THRESH    0x02
#define ADS1115_REG_HI_THRESH    0x03

/** @brief Config register bits */
#define ADS1115_CONFIG_OS_MASK       0x8000  // Operational status
#define ADS1115_CONFIG_OS_SINGLE     0x8000  // Start single conversion
#define ADS1115_CONFIG_MUX_MASK      0x7000  // Multiplexer
#define ADS1115_CONFIG_PGA_MASK      0x0E00  // PGA
#define ADS1115_CONFIG_MODE_MASK     0x0100  // Conversion mode

/** @brief Internal handle structure */
struct ads1115_handle_s {
    uint8_t i2c_addr;
    ads1115_pga_t pga;
    ads1115_dr_t data_rate;
    ads1115_mode_t mode;
    uint32_t i2c_timeout_ms;
    i2c_master_dev_handle_t i2c_dev;
    ads1115_mux_t last_mux;
    bool initialized;
};

/**
 * @brief PGA full-scale voltage values
 */
static const float ads1115_pga_values[] = {
    [ADS1115_PGA_6_144V] = 6.144f,
    [ADS1115_PGA_4_096V] = 4.096f,
    [ADS1115_PGA_2_048V] = 2.048f,
    [ADS1115_PGA_1_024V] = 1.024f,
    [ADS1115_PGA_0_512V] = 0.512f,
    [ADS1115_PGA_0_256V] = 0.256f,
};

float ads1115_pga_to_voltage(ads1115_pga_t pga) {
    if (pga > ADS1115_PGA_0_256V) {
        pga = ADS1115_PGA_2_048V;
    }
    return ads1115_pga_values[pga];
}

/**
 * @brief I2C write register
 */
static int write_register(ads1115_handle_t handle, uint8_t reg, uint16_t value) {
    if (handle == NULL || handle->i2c_dev == NULL) {
        return -1;
    }
    
    uint8_t data[3];
    data[0] = reg;
    data[1] = (value >> 8) & 0xFF;
    data[2] = value & 0xFF;
    
    return i2c_master_transmit(handle->i2c_dev, data, 3, handle->i2c_timeout_ms / portTICK_PERIOD_MS);
}

/**
 * @brief I2C read register
 */
static int read_register(ads1115_handle_t handle, uint8_t reg, uint16_t *value) {
    if (handle == NULL || handle->i2c_dev == NULL || value == NULL) {
        return -1;
    }
    
    uint8_t cmd = reg;
    int ret = i2c_master_transmit(handle->i2c_dev, &cmd, 1, handle->i2c_timeout_ms / portTICK_PERIOD_MS);
    if (ret != 0) return ret;
    
    uint8_t data[2];
    ret = i2c_master_receive(handle->i2c_dev, data, 2, handle->i2c_timeout_ms / portTICK_PERIOD_MS);
    if (ret != 0) return ret;
    
    *value = ((uint16_t)data[0] << 8) | data[1];
    return 0;
}

ads1115_handle_t ads1115_create(const ads1115_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    
    ads1115_handle_t handle = (ads1115_handle_t)calloc(1, sizeof(struct ads1115_handle_s));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return NULL;
    }
    
    handle->i2c_addr = (uint8_t)config->i2c_addr;
    handle->pga = config->pga > ADS1115_PGA_0_256V ? ADS1115_PGA_2_048V : config->pga;
    handle->data_rate = config->data_rate > ADS1115_DR_860_SPS ? ADS1115_DR_128_SPS : config->data_rate;
    handle->mode = config->mode;
    handle->i2c_timeout_ms = config->i2c_timeout_ms > 0 ? config->i2c_timeout_ms : 1000;
    handle->last_mux = ADS1115_MUX_AIN0_GND;
    handle->initialized = false;
    
    ESP_LOGI(TAG, "ADS1115 created: addr=0x%02X, PGA=±%.3fV, DR=%d SPS",
             handle->i2c_addr,
             ads1115_pga_to_voltage(handle->pga),
             8 << handle->data_rate);
    
    return handle;
}

void ads1115_delete(ads1115_handle_t handle) {
    if (handle == NULL) return;
    
    if (handle->initialized && handle->i2c_dev != NULL) {
        i2c_master_bus_rm_device(handle->i2c_dev);
    }
    
    free(handle);
    ESP_LOGI(TAG, "ADS1115 deleted");
}

int ads1115_init(ads1115_handle_t handle) {
    if (handle == NULL) return -1;
    if (handle->initialized) return 0;
    
    handle->initialized = true;
    ESP_LOGI(TAG, "ADS1115 initialized");
    
    return 0;
}

int ads1115_configure(ads1115_handle_t handle, ads1115_mux_t mux, ads1115_pga_t pga,
                      ads1115_mode_t mode, ads1115_dr_t data_rate, bool start_conversion) {
    if (handle == NULL) return -1;
    
    if (pga > ADS1115_PGA_0_256V) pga = ADS1115_PGA_2_048V;
    if (data_rate > ADS1115_DR_860_SPS) data_rate = ADS1115_DR_128_SPS;
    
    uint16_t config = 0;
    
    // Operational status / single-shot start
    if (mode == ADS1115_MODE_SINGLE_SHOT || start_conversion) {
        config |= ADS1115_CONFIG_OS_SINGLE;
    }
    
    // Multiplexer
    config |= ((uint16_t)mux << 12) & ADS1115_CONFIG_MUX_MASK;
    
    // PGA
    config |= ((uint16_t)pga << 9) & ADS1115_CONFIG_PGA_MASK;
    
    // Mode
    if (mode == ADS1115_MODE_SINGLE_SHOT) {
        config |= ADS1115_CONFIG_MODE_MASK;  // Single-shot mode
    }
    
    // Data rate
    config |= ((uint16_t)data_rate << 5);
    
    // Enable comparator (for continuous mode alert)
    config &= ~0x0003;  // Disable comparator
    
    int ret = write_register(handle, ADS1115_REG_CONFIG, config);
    if (ret == 0) {
        handle->last_mux = mux;
        handle->pga = pga;
        handle->data_rate = data_rate;
        handle->mode = mode;
        ESP_LOGD(TAG, "ADS1115 configured: mux=%d, pga=%d, mode=%d, dr=%d",
                 mux, pga, mode, data_rate);
    }
    
    return ret;
}

int ads1115_read(ads1115_handle_t handle, ads1115_mux_t mux, ads1115_data_t *data) {
    if (handle == NULL || data == NULL) return -1;
    
    // Configure and start conversion
    int ret = ads1115_configure(handle, mux, handle->pga, ADS1115_MODE_SINGLE_SHOT,
                                handle->data_rate, true);
    if (ret != 0) return ret;
    
    // Wait for conversion to complete
    // Max wait time based on data rate
    uint32_t delay_us = 1000000 / (8 << handle->data_rate);
    delay_us += 1000;  // Add margin
    
    for (int i = 0; i < 100; i++) {
        bool ready;
        ads1115_is_ready(handle, &ready);
        if (ready) break;
        
        // Simple delay
        for (volatile int j = 0; j < 1000; j++);
    }
    
    // Read conversion result
    uint16_t raw;
    ret = read_register(handle, ADS1115_REG_CONVERSION, &raw);
    if (ret != 0) {
        data->valid = false;
        return ret;
    }
    
    // Convert to signed 16-bit
    int16_t signed_raw = (int16_t)raw;
    
    // Calculate voltage
    float vref = ads1115_pga_to_voltage(handle->pga);
    float voltage = (signed_raw * vref) / 32768.0f;
    
    data->raw = signed_raw;
    data->voltage = voltage;
    data->mux = mux;
    data->timestamp = esp_log_timestamp();
    data->valid = true;
    
    ESP_LOGD(TAG, "ADS1115: raw=%d, voltage=%.4fV (mux=%d)", signed_raw, voltage, mux);
    
    return 0;
}

int ads1115_read_channel(ads1115_handle_t handle, uint8_t channel, float *voltage) {
    if (handle == NULL || voltage == NULL) return -1;
    if (channel > 3) return -2;
    
    ads1115_mux_t mux = (ads1115_mux_t)(ADS1115_MUX_AIN0_GND + channel);
    
    ads1115_data_t data;
    int ret = ads1115_read(handle, mux, &data);
    if (ret == 0) {
        *voltage = data.voltage;
    }
    
    return ret;
}

int ads1115_read_differential(ads1115_handle_t handle, ads1115_mux_t mux, float *voltage) {
    if (handle == NULL || voltage == NULL) return -1;
    
    // Only valid differential configs
    if (mux > ADS1115_MUX_AIN2_AIN3) return -2;
    
    ads1115_data_t data;
    int ret = ads1115_read(handle, mux, &data);
    if (ret == 0) {
        *voltage = data.voltage;
    }
    
    return ret;
}

int ads1115_is_ready(ads1115_handle_t handle, bool *ready) {
    if (handle == NULL || ready == NULL) return -1;
    
    uint16_t config;
    int ret = read_register(handle, ADS1115_REG_CONFIG, &config);
    if (ret != 0) {
        *ready = false;
        return ret;
    }
    
    // OS bit is high when conversion is complete (in single-shot mode)
    *ready = (config & ADS1115_CONFIG_OS_MASK) != 0;
    
    return 0;
}

int ads1115_start_conversion(ads1115_handle_t handle, ads1115_mux_t mux) {
    if (handle == NULL) return -1;
    
    return ads1115_configure(handle, mux, handle->pga, ADS1115_MODE_SINGLE_SHOT,
                            handle->data_rate, true);
}
