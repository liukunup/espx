/**
 * @file ina226.c
 * @brief INA226 power monitor (I2C) driver
 *
 * Device type name: "ina226"
 * High-side current/power/voltage monitor.
 */

#include <string.h>
#include <stdlib.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>

#include "ina226.h"
#include "device_manager.h"
#include "esp_idf_i2c.h"

static const char *TAG = "ina226";

/* INA226 registers */
#define INA226_REG_CONFIG      0x00
#define INA226_REG_SHUNT_VOLT  0x01
#define INA226_REG_BUS_VOLT    0x02
#define INA226_REG_POWER       0x03
#define INA226_REG_CURRENT      0x04
#define INA226_REG_CAL         0x05

typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    uint8_t i2c_addr;
    float r_shunt;          /* mΩ */
    float current_lsb;      /* A per LSB */
    float max_current_ma;   /* A */
    int bus_voltage_range;  /* 16 or 36V */
    int avg_mode;           /* 0-7 */
    int bus_conv_time;      /* 0-7 */
    int shunt_conv_time;     /* 0-7 */
    int interval_ms;
    int64_t last_read_us;

    double bus_voltage_mv;
    double shunt_voltage_uv;
    double current_ma;
    double power_mw;
} ina226_data_t;

static esp_err_t ina226_write_reg(ina226_data_t *data, uint8_t reg, uint16_t val)
{
    uint8_t buf[3] = {reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF)};
    return esp_idf_i2c_write(data->dev, buf, 3);
}

static esp_err_t ina226_read_reg(ina226_data_t *data, uint8_t reg, uint16_t *out_val)
{
    uint8_t buf[2];
    esp_err_t err = esp_idf_i2c_write_read(data->dev, &reg, 1, buf, 2);
    if (err != ESP_OK) return err;
    *out_val = (uint16_t)((buf[0] << 8) | buf[1]);
    return ESP_OK;
}

static esp_err_t ina226_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) return ESP_ERR_INVALID_ARG;

    cJSON *sda = cJSON_GetObjectItem(config, "sda_gpio");
    cJSON *scl = cJSON_GetObjectItem(config, "scl_gpio");
    if (!cJSON_IsNumber(sda) || !cJSON_IsNumber(scl)) return ESP_ERR_INVALID_ARG;

    ina226_data_t *data = calloc(1, sizeof(ina226_data_t));
    if (!data) return ESP_ERR_NO_MEM;

    data->i2c_addr = 0x40;
    data->r_shunt = 10.0f;
    data->max_current_ma = 1000.0f;
    data->bus_voltage_range = 16;
    data->avg_mode = 3;       /* 64 averages */
    data->bus_conv_time = 3;   /* 1.1ms */
    data->shunt_conv_time = 3; /* 1.1ms */
    data->interval_ms = 1000;

    cJSON *addr = cJSON_GetObjectItem(config, "i2c_addr");
    cJSON *rsh  = cJSON_GetObjectItem(config, "r_shunt");
    cJSON *maxcur = cJSON_GetObjectItem(config, "max_current_ma");
    cJSON *bvrange = cJSON_GetObjectItem(config, "bus_voltage_range");
    cJSON *avg   = cJSON_GetObjectItem(config, "avg_mode");
    cJSON *bvct  = cJSON_GetObjectItem(config, "bus_conv_time");
    cJSON *svct  = cJSON_GetObjectItem(config, "shunt_conv_time");
    cJSON *iv    = cJSON_GetObjectItem(config, "interval_ms");
    cJSON *freq  = cJSON_GetObjectItem(config, "scl_freq");

    if (cJSON_IsNumber(addr))    data->i2c_addr = (uint8_t)addr->valueint;
    if (cJSON_IsNumber(rsh))     data->r_shunt = (float)rsh->valueint;
    if (cJSON_IsNumber(maxcur))  data->max_current_ma = (float)maxcur->valueint;
    if (cJSON_IsNumber(bvrange)) data->bus_voltage_range = bvrange->valueint;
    if (cJSON_IsNumber(avg))     data->avg_mode = avg->valueint & 0x07;
    if (cJSON_IsNumber(bvct))    data->bus_conv_time = bvct->valueint & 0x07;
    if (cJSON_IsNumber(svct))    data->shunt_conv_time = svct->valueint & 0x07;
    if (cJSON_IsNumber(iv))      data->interval_ms = iv->valueint;

    uint32_t scl_freq = cJSON_IsNumber(freq) ? freq->valueint : 400000;

    esp_err_t err = esp_idf_i2c_init(sda->valueint, scl->valueint, scl_freq, &data->bus);
    if (err != ESP_OK) { free(data); return err; }
    err = esp_idf_i2c_add_device(data->bus, data->i2c_addr, &data->dev);
    if (err != ESP_OK) { free(data); return err; }
    if (err != ESP_OK) { free(data); return err; }

    /* Calculate Current_LSB = MaxCurrent / 32768 */
    data->current_lsb = (data->max_current_ma / 1000.0f) / 32768.0f;

    /* Calculate Cal = 0.00512 / (Current_LSB × Rshunt_Ω) */
    float r_shunt_ohm = data->r_shunt / 1000.0f;
    uint16_t cal = (uint16_t)(0.00512f / (data->current_lsb * r_shunt_ohm));
    if (cal == 0) cal = 1;  /* Ensure non-zero */

    /* Config register: mode = Shunt+Bus continuous */
    uint16_t cfg = ((data->avg_mode & 0x07) << 9) |
                   ((data->bus_conv_time & 0x07) << 6) |
                   ((data->shunt_conv_time & 0x07) << 3) |
                   0x07;  /* Shunt voltage + Bus voltage continuous mode */

    err = ina226_write_reg(data, INA226_REG_CONFIG, cfg);
    if (err != ESP_OK) { free(data); return err; }

    err = ina226_write_reg(data, INA226_REG_CAL, cal);
    if (err != ESP_OK) { free(data); return err; }

    dev->driver_data = data;
    ESP_LOGI(TAG, "INA226 init: SDA=GPIO%d SCL=GPIO%d addr=0x%02X "
             "Rshunt=%.1fmΩ Cal=0x%04X",
             sda->valueint, scl->valueint, data->i2c_addr, data->r_shunt, cal);
    return ESP_OK;
}

static esp_err_t ina226_deinit(device_t *dev)
{
    free(dev->driver_data);
    dev->driver_data = NULL;
    return ESP_OK;
}

static esp_err_t ina226_read(device_t *dev, cJSON *value)
{
    ina226_data_t *data = dev->driver_data;
    if (!data) return ESP_ERR_INVALID_STATE;

    cJSON_AddNumberToObject(value, "bus_voltage_mv", data->bus_voltage_mv);
    cJSON_AddNumberToObject(value, "shunt_voltage_uv", data->shunt_voltage_uv);
    cJSON_AddNumberToObject(value, "current_ma", data->current_ma);
    cJSON_AddNumberToObject(value, "power_mw", data->power_mw);
    return ESP_OK;
}

static esp_err_t ina226_tick(device_t *dev)
{
    ina226_data_t *data = dev->driver_data;
    if (!data) return ESP_OK;

    int64_t now_us = esp_timer_get_time();
    int64_t interval_us = (int64_t)data->interval_ms * 1000;
    if (data->last_read_us > 0 && (now_us - data->last_read_us) < interval_us) {
        return ESP_OK;
    }

    uint16_t shunt_raw, bus_raw;
    esp_err_t err;

    err = ina226_read_reg(data, INA226_REG_SHUNT_VOLT, &shunt_raw);
    if (err != ESP_OK) return err;

    err = ina226_read_reg(data, INA226_REG_BUS_VOLT, &bus_raw);
    if (err != ESP_OK) return err;

    /* Shunt voltage: signed, LSB = 2.5μV */
    int16_t shunt_signed = (int16_t)shunt_raw;
    data->shunt_voltage_uv = shunt_signed * 2.5;

    /* Bus voltage: bits [2:0] are status, real value = raw >> 3 × 1.25mV */
    data->bus_voltage_mv = ((bus_raw >> 3) & 0x07FF) * 1.25;

    /* Current = Current register × Current_LSB */
    uint16_t current_raw;
    err = ina226_read_reg(data, INA226_REG_CURRENT, &current_raw);
    if (err == ESP_OK) {
        int16_t cur = (int16_t)current_raw;
        data->current_ma = cur * data->current_lsb * 1000.0;
    }

    /* Power = Power register × (Current_LSB × 20) */
    uint16_t power_raw;
    err = ina226_read_reg(data, INA226_REG_POWER, &power_raw);
    if (err == ESP_OK) {
        data->power_mw = power_raw * data->current_lsb * 20.0 * 1000.0;
    }

    data->last_read_us = now_us;
    ESP_LOGD(TAG, "INA226: V=%.2fmV I=%.2fmA P=%.2fmW",
             data->bus_voltage_mv, data->current_ma, data->power_mw);
    return ESP_OK;
}

static esp_err_t ina226_default_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "sda_gpio", 10);
    cJSON_AddNumberToObject(config, "scl_gpio", 11);
    cJSON_AddNumberToObject(config, "i2c_addr", 0x40);
    cJSON_AddNumberToObject(config, "r_shunt", 10.0);
    cJSON_AddNumberToObject(config, "max_current_ma", 1000);
    cJSON_AddNumberToObject(config, "bus_voltage_range", 16);
    cJSON_AddNumberToObject(config, "avg_mode", 3);
    cJSON_AddNumberToObject(config, "bus_conv_time", 3);
    cJSON_AddNumberToObject(config, "shunt_conv_time", 3);
    cJSON_AddNumberToObject(config, "interval_ms", 1000);
    cJSON_AddNumberToObject(config, "scl_freq", 400000);
    return ESP_OK;
}

static const device_type_t ina226_driver = {
    .name = "ina226",
    .description = "INA226 power monitor (I2C)",
    .capabilities = DEVICE_CAPABILITY_READ | DEVICE_CAPABILITY_PERIODIC | DEVICE_CAPABILITY_NOTIFY,
    .init = ina226_init,
    .deinit = ina226_deinit,
    .read = ina226_read,
    .tick = ina226_tick,
    .get_default_config = ina226_default_config,
};

esp_err_t ina226_driver_register(void)
{
    return device_type_register(&ina226_driver);
}
