/**
 * @file ina226.c
 * @brief INA226 High-Side Current/Voltage Monitor Driver Implementation
 */

#include "ina226.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_err.h"
#include <stdlib.h>
#include <math.h>

static const char *TAG = "ina226";

/** @brief INA226 register addresses */
#define INA226_REG_CONFIG       0x00
#define INA226_REG_SHUNT_VOLT   0x01
#define INA226_REG_BUS_VOLT     0x02
#define INA226_REG_POWER        0x03
#define INA226_REG_CURRENT      0x04
#define INA226_REG_CALIBRATION  0x05
#define INA226_REG_MASK_ENABLE  0x06
#define INA226_REG_ALERT_LIMIT  0x07
#define INA226_REG_MFG_UID      0xFE
#define INA226_REG_DIE_ID       0xFF

/** @brief LSB values */
#define INA226_SHUNT_VOLTAGE_LSB  2.5e-6   // 2.5μV per LSB
#define INA226_BUS_VOLTAGE_LSB    1.25e-3   // 1.25mV per LSB
#define INA226_POWER_LSB          25.0e-3   // 25mW per LSB (depends on calibration)

/** @brief Internal handle structure */
struct ina226_handle_s {
    uint8_t i2c_addr;
    float r_shunt;
    float max_current;
    float current_lsb;
    uint16_t calibration;
    ina226_avg_t avg_mode;
    ina226_vbus_ct_t vbus_ct;
    ina226_vsh_ct_t vsh_ct;
    ina226_mode_t mode;
    uint32_t i2c_timeout_ms;
    i2c_master_dev_handle_t i2c_dev;
    bool initialized;
};

/**
 * @brief I2C write register
 */
static int write_register(ina226_handle_t handle, uint8_t reg, uint16_t value) {
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
static int read_register(ina226_handle_t handle, uint8_t reg, uint16_t *value) {
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

/**
 * @brief Calculate calibration value
 */
static uint16_t calculate_calibration(float r_shunt, float max_current) {
    // INA226 calibration formula:
    // Current_LSB = Max_Expected_Current / 32768
    // Calibration = trunc(0.00512 / (Current_LSB × R_Shunt))
    
    float current_lsb = max_current / 32768.0f;
    float calibration = 0.00512f / (current_lsb * r_shunt);
    
    return (uint16_t)calibration;
}

/**
 * @brief Calculate current from LSB
 */
static float calculate_current(int16_t raw_current, float current_lsb) {
    return raw_current * current_lsb;
}

/**
 * @brief Calculate power from LSB
 */
static float calculate_power(uint16_t raw_power, float current_lsb) {
    // Power_LSB = 25 × Current_LSB
    float power_lsb = 25.0f * current_lsb;
    return raw_power * power_lsb;
}

ina226_handle_t ina226_create(const ina226_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    
    if (config->r_shunt_ohm <= 0) {
        ESP_LOGE(TAG, "Invalid shunt resistance: %.6f", config->r_shunt_ohm);
        return NULL;
    }
    
    ina226_handle_t handle = (ina226_handle_t)calloc(1, sizeof(struct ina226_handle_s));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return NULL;
    }
    
    handle->i2c_addr = (uint8_t)config->i2c_addr;
    handle->r_shunt = config->r_shunt_ohm;
    handle->max_current = config->max_current_amp;
    handle->current_lsb = config->max_current_amp / 32768.0f;
    handle->calibration = calculate_calibration(handle->r_shunt, handle->max_current);
    handle->avg_mode = config->avg_mode;
    handle->vbus_ct = config->vbus_ct;
    handle->vsh_ct = config->vsh_ct;
    handle->mode = config->mode;
    handle->i2c_timeout_ms = config->i2c_timeout_ms > 0 ? config->i2c_timeout_ms : 1000;
    handle->initialized = false;
    
    ESP_LOGI(TAG, "INA226 created: addr=0x%02X, shunt=%.4fΩ, maxI=%.3fA, cal=%d",
             handle->i2c_addr, handle->r_shunt, handle->max_current, handle->calibration);
    
    return handle;
}

void ina226_delete(ina226_handle_t handle) {
    if (handle == NULL) return;
    
    if (handle->initialized && handle->i2c_dev != NULL) {
        i2c_master_bus_rm_device(handle->i2c_dev);
    }
    
    free(handle);
    ESP_LOGI(TAG, "INA226 deleted");
}

int ina226_init(ina226_handle_t handle) {
    if (handle == NULL) return -1;
    if (handle->initialized) return 0;
    
    handle->initialized = true;
    ESP_LOGI(TAG, "INA226 initialized");
    
    return 0;
}

int ina226_configure(ina226_handle_t handle, ina226_avg_t avg, ina226_vbus_ct_t vbus_ct,
                     ina226_vsh_ct_t vsh_ct, ina226_mode_t mode) {
    if (handle == NULL) return -1;
    
    uint16_t config = 0;
    
    // Reset
    config |= (1 << 15);
    
    // Mode
    config |= (mode & 0x07);
    
    // Shunt voltage conversion time
    config |= ((vsh_ct & 0x07) << 3);
    
    // Bus voltage conversion time
    config |= ((vbus_ct & 0x07) << 6);
    
    // Averaging mode
    config |= ((avg & 0x07) << 9);
    
    int ret = write_register(handle, INA226_REG_CONFIG, config);
    if (ret == 0) {
        handle->avg_mode = avg;
        handle->vbus_ct = vbus_ct;
        handle->vsh_ct = vsh_ct;
        handle->mode = mode;
    }
    
    return ret;
}

int ina226_set_calibration(ina226_handle_t handle, float r_shunt, float max_current) {
    if (handle == NULL) return -1;
    if (r_shunt <= 0 || max_current <= 0) return -2;
    
    handle->r_shunt = r_shunt;
    handle->max_current = max_current;
    handle->current_lsb = max_current / 32768.0f;
    handle->calibration = calculate_calibration(r_shunt, max_current);
    
    ESP_LOGI(TAG, "INA226 calibration set: R=%.4fΩ, Imax=%.3fA, CAL=%d",
             r_shunt, max_current, handle->calibration);
    
    return write_register(handle, INA226_REG_CALIBRATION, handle->calibration);
}

int ina226_read(ina226_handle_t handle, ina226_data_t *data) {
    if (handle == NULL || data == NULL) return -1;
    
    // Read all registers
    uint16_t shunt_raw, bus_raw, power_raw, current_raw;
    
    int ret = read_register(handle, INA226_REG_SHUNT_VOLT, &shunt_raw);
    if (ret != 0) { data->valid = false; return ret; }
    
    ret = read_register(handle, INA226_REG_BUS_VOLT, &bus_raw);
    if (ret != 0) { data->valid = false; return ret; }
    
    ret = read_register(handle, INA226_REG_POWER, &power_raw);
    if (ret != 0) { data->valid = false; return ret; }
    
    ret = read_register(handle, INA226_REG_CURRENT, &current_raw);
    if (ret != 0) { data->valid = false; return ret; }
    
    // Convert shunt voltage (signed, 2.5μV per LSB)
    int16_t shunt_signed = (int16_t)shunt_raw;
    data->shunt_voltage = shunt_signed * INA226_SHUNT_VOLTAGE_LSB * 1000.0f;  // mV
    
    // Convert bus voltage (unsigned, 1.25mV per LSB, bits [14:3])
    data->bus_voltage = ((bus_raw >> 3) * INA226_BUS_VOLTAGE_LSB);
    
    // Convert current (signed, depends on calibration)
    int16_t current_signed = (int16_t)current_raw;
    data->current_amp = calculate_current(current_signed, handle->current_lsb);
    
    // Convert power (unsigned, depends on calibration)
    data->power_watt = calculate_power(power_raw, handle->current_lsb);
    
    data->timestamp = esp_log_timestamp();
    data->valid = true;
    
    ESP_LOGD(TAG, "INA226: V=%.3fV, Is=%.3fmV, I=%.4fA, P=%.4fW",
             data->bus_voltage, data->shunt_voltage, data->current_amp, data->power_watt);
    
    return 0;
}

int ina226_read_bus_voltage(ina226_handle_t handle, float *voltage) {
    if (handle == NULL || voltage == NULL) return -1;
    
    uint16_t raw;
    int ret = read_register(handle, INA226_REG_BUS_VOLT, &raw);
    if (ret != 0) return ret;
    
    *voltage = ((raw >> 3) * INA226_BUS_VOLTAGE_LSB);
    return 0;
}

int ina226_read_shunt_voltage(ina226_handle_t handle, float *voltage) {
    if (handle == NULL || voltage == NULL) return -1;
    
    uint16_t raw;
    int ret = read_register(handle, INA226_REG_SHUNT_VOLT, &raw);
    if (ret != 0) return ret;
    
    int16_t signed_raw = (int16_t)raw;
    *voltage = signed_raw * INA226_SHUNT_VOLTAGE_LSB * 1000.0f;  // mV
    return 0;
}

int ina226_read_current(ina226_handle_t handle, float *current) {
    if (handle == NULL || current == NULL) return -1;
    
    uint16_t raw;
    int ret = read_register(handle, INA226_REG_CURRENT, &raw);
    if (ret != 0) return ret;
    
    int16_t signed_raw = (int16_t)raw;
    *current = calculate_current(signed_raw, handle->current_lsb);
    return 0;
}

int ina226_read_power(ina226_handle_t handle, float *power) {
    if (handle == NULL || power == NULL) return -1;
    
    uint16_t raw;
    int ret = read_register(handle, INA226_REG_POWER, &raw);
    if (ret != 0) return ret;
    
    *power = calculate_power(raw, handle->current_lsb);
    return 0;
}

int ina226_check_present(ina226_handle_t handle, bool *present) {
    if (handle == NULL || present == NULL) return -1;
    
    uint16_t mfg_id, die_id;
    int ret1 = read_register(handle, INA226_REG_MFG_UID, &mfg_id);
    int ret2 = read_register(handle, INA226_REG_DIE_ID, &die_id);
    
    if (ret1 == 0 && ret2 == 0) {
        // INA226 should return MFG=0x5449, DIE=0x2260
        *present = (mfg_id == 0x5449 && die_id == 0x2260);
        ESP_LOGI(TAG, "INA226 present: MFG=0x%04X, DIE=0x%04X", mfg_id, die_id);
    } else {
        *present = false;
    }
    
    return 0;
}
