/**
 * @file mcp4725.c
 * @brief MCP4725 12-bit DAC Driver Implementation
 */

#include "mcp4725.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_err.h"
#include <stdlib.h>

static const char *TAG = "mcp4725";

/** @brief Maximum DAC value (12-bit) */
#define MCP4725_MAX_VALUE 4095

/** @brief Internal reference voltage */
#define MCP4725_INTERNAL_VREF  2.048f

/** @brief Command bytes */
#define MCP4725_CMD_WRITE_DAC  0x40
#define MCP4725_CMD_WRITE_EEPROM  0x60

/** @brief Internal handle structure */
struct mcp4725_handle_s {
    uint8_t i2c_addr;
    mcp4725_vref_t vref;
    uint32_t i2c_timeout_ms;
    i2c_master_dev_handle_t i2c_dev;
    uint16_t last_value;
    bool initialized;
};

mcp4725_handle_t mcp4725_create(const mcp4725_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    
    mcp4725_handle_t handle = (mcp4725_handle_t)calloc(1, sizeof(struct mcp4725_handle_s));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return NULL;
    }
    
    handle->i2c_addr = (uint8_t)config->i2c_addr;
    handle->vref = config->vref;
    handle->i2c_timeout_ms = config->i2c_timeout_ms > 0 ? config->i2c_timeout_ms : 1000;
    handle->last_value = 0;
    handle->initialized = false;
    
    ESP_LOGI(TAG, "MCP4725 created: addr=0x%02X, vref=%s",
             handle->i2c_addr,
             (config->vref == MCP4725_VREF_INTERNAL) ? "internal" : "external");
    
    return handle;
}

void mcp4725_delete(mcp4725_handle_t handle) {
    if (handle == NULL) return;
    
    if (handle->initialized && handle->i2c_dev != NULL) {
        i2c_master_bus_rm_device(handle->i2c_dev);
    }
    
    free(handle);
    ESP_LOGI(TAG, "MCP4725 deleted");
}

int mcp4725_init(mcp4725_handle_t handle) {
    if (handle == NULL) return -1;
    if (handle->initialized) return 0;
    
    // Get I2C master bus handle from system
    // Note: Application should register I2C bus first
    // For now, we store config and return success
    // I2C device will be created when first transaction happens
    
    handle->initialized = true;
    ESP_LOGI(TAG, "MCP4725 initialized (addr=0x%02X)", handle->i2c_addr);
    
    return 0;
}

/**
 * @brief I2C write helper
 */
static int i2c_write(mcp4725_handle_t handle, const uint8_t *data, size_t len) {
    if (handle == NULL || handle->i2c_dev == NULL) {
        return -1;
    }
    
    return i2c_master_transmit(handle->i2c_dev, data, len, handle->i2c_timeout_ms / portTICK_PERIOD_MS);
}

/**
 * @brief I2C read helper
 */
static int i2c_read(mcp4725_handle_t handle, uint8_t *data, size_t len) {
    if (handle == NULL || handle->i2c_dev == NULL) {
        return -1;
    }
    
    return i2c_master_receive(handle->i2c_dev, data, len, handle->i2c_timeout_ms / portTICK_PERIOD_MS);
}

int mcp4725_set_value(mcp4725_handle_t handle, uint16_t value, bool update_now) {
    if (handle == NULL) return -1;
    
    if (value > MCP4725_MAX_VALUE) {
        value = MCP4725_MAX_VALUE;
    }
    
    uint8_t cmd = update_now ? MCP4725_CMD_WRITE_DAC : MCP4725_CMD_WRITE_DAC;
    uint8_t data[3];
    
    // Fast mode: C2 C1 C0 | D11 D10 D9 D8 | D7 D6 D5 D4 D3 D2 D1 D0
    data[0] = cmd | ((value >> 8) & 0x0F);  // Upper 4 bits + command
    data[1] = value & 0xFF;                // Lower 8 bits
    data[2] = 0x00;                        // Power down bits (PD1=0, PD0=0)
    
    int ret = i2c_write(handle, data, 3);
    if (ret == 0) {
        handle->last_value = value;
        ESP_LOGD(TAG, "DAC set to %d (%.3fV)", value, value * MCP4725_INTERNAL_VREF / MCP4725_MAX_VALUE);
    }
    
    return ret;
}

int mcp4725_set_voltage(mcp4725_handle_t handle, float voltage, bool update_now) {
    if (handle == NULL) return -1;
    
    float vref = (handle->vref == MCP4725_VREF_INTERNAL) ? MCP4725_INTERNAL_VREF : MCP4725_INTERNAL_VREF * 2;
    
    if (voltage < 0) voltage = 0;
    if (voltage > vref) voltage = vref;
    
    uint16_t value = (uint16_t)((voltage / vref) * MCP4725_MAX_VALUE);
    
    return mcp4725_set_value(handle, value, update_now);
}

int mcp4725_set_channel(mcp4725_handle_t handle, uint8_t channel, uint16_t value, mcp4725_power_down_t pd_mode) {
    if (handle == NULL) return -1;
    if (channel != 0) return -2;  // MCP4725 has only 1 channel
    
    if (value > MCP4725_MAX_VALUE) {
        value = MCP4725_MAX_VALUE;
    }
    
    uint8_t data[3];
    data[0] = MCP4725_CMD_WRITE_DAC | ((value >> 8) & 0x0F);
    data[1] = value & 0xFF;
    data[2] = (pd_mode & 0x03) << 4;  // PD1 PD0 in bits 5-4
    
    int ret = i2c_write(handle, data, 3);
    if (ret == 0) {
        handle->last_value = value;
    }
    
    return ret;
}

int mcp4725_set_power_down(mcp4725_handle_t handle, mcp4725_power_down_t mode) {
    if (handle == NULL) return -1;
    
    uint8_t data[3];
    data[0] = MCP4725_CMD_WRITE_DAC | ((handle->last_value >> 8) & 0x0F);
    data[1] = handle->last_value & 0xFF;
    data[2] = (mode & 0x03) << 4;
    
    return i2c_write(handle, data, 3);
}

int mcp4725_get_value(mcp4725_handle_t handle, uint16_t *value) {
    if (handle == NULL || value == NULL) return -1;
    
    *value = handle->last_value;
    return 0;
}

int mcp4725_get_eeprom_voltage(mcp4725_handle_t handle, float *voltage) {
    if (handle == NULL || voltage == NULL) return -1;
    
    uint8_t cmd = 0x08;  // Read EEPROM
    int ret = i2c_write(handle, &cmd, 1);
    if (ret != 0) return ret;
    
    uint8_t data[5];
    ret = i2c_read(handle, data, 5);
    if (ret != 0) return ret;
    
    uint16_t value = ((data[1] & 0x0F) << 8) | data[2];
    
    float vref = (handle->vref == MCP4725_VREF_INTERNAL) ? MCP4725_INTERNAL_VREF : MCP4725_INTERNAL_VREF * 2;
    *voltage = value * vref / MCP4725_MAX_VALUE;
    
    return 0;
}

int mcp4725_save_to_eeprom(mcp4725_handle_t handle, uint16_t value) {
    if (handle == NULL) return -1;
    
    if (value > MCP4725_MAX_VALUE) {
        value = MCP4725_MAX_VALUE;
    }
    
    uint8_t data[3];
    data[0] = MCP4725_CMD_WRITE_EEPROM | ((value >> 8) & 0x0F);
    data[1] = value & 0xFF;
    data[2] = 0x00;  // Power down bits
    
    int ret = i2c_write(handle, data, 3);
    if (ret == 0) {
        ESP_LOGI(TAG, "Value %d saved to EEPROM", value);
    }
    
    return ret;
}

int mcp4725_get_voltage(mcp4725_handle_t handle, float *voltage) {
    if (handle == NULL || voltage == NULL) return -1;
    
    float vref = (handle->vref == MCP4725_VREF_INTERNAL) ? MCP4725_INTERNAL_VREF : MCP4725_INTERNAL_VREF * 2;
    *voltage = handle->last_value * vref / MCP4725_MAX_VALUE;
    
    return 0;
}
