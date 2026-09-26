/**
 * @file mcp4725.c
 * @brief MCP4725 12-bit DAC (I2C) driver
 *
 * Device type name: "mcp4725"
 * Write: 0-4095 -> 0-Vref
 */

#include <string.h>
#include <stdlib.h>
#include <esp_log.h>
#include <cJSON.h>

#include "mcp4725.h"
#include "device_manager.h"
#include "esp_idf_i2c.h"

static const char *TAG = "mcp4725";

typedef struct {
    i2c_master_bus_handle_t bus;
    i2c_master_dev_handle_t dev;
    uint16_t value;      /* Cached DAC value (0-4095) */
    int vref_mv;         /* Reference voltage in mV */
} mcp4725_data_t;

static esp_err_t mcp4725_write_value(mcp4725_data_t *data, uint16_t value, bool to_eeprom)
{
    if (value > 4095) return ESP_ERR_INVALID_ARG;

    uint8_t cmd = to_eeprom ? 0x60 : 0x40;
    uint8_t buf[3] = {
        cmd,
        (uint8_t)((value >> 4) & 0xFF),
        (uint8_t)((value & 0x0F) << 4),
    };

    return esp_idf_i2c_write(data->dev, buf, sizeof(buf));
}

static esp_err_t mcp4725_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) return ESP_ERR_INVALID_ARG;

    cJSON *sda = cJSON_GetObjectItem(config, "sda_gpio");
    cJSON *scl = cJSON_GetObjectItem(config, "scl_gpio");
    if (!cJSON_IsNumber(sda) || !cJSON_IsNumber(scl)) return ESP_ERR_INVALID_ARG;

    mcp4725_data_t *data = calloc(1, sizeof(mcp4725_data_t));
    if (!data) return ESP_ERR_NO_MEM;

    uint8_t addr = 0x60;
    data->vref_mv = 3300;
    data->value = 0;

    cJSON *addr_n = cJSON_GetObjectItem(config, "i2c_addr");
    cJSON *vref  = cJSON_GetObjectItem(config, "vref_mv");
    cJSON *freq  = cJSON_GetObjectItem(config, "scl_freq");

    if (cJSON_IsNumber(addr_n)) addr = (uint8_t)addr_n->valueint;
    if (cJSON_IsNumber(vref)) data->vref_mv = vref->valueint;

    uint32_t scl_freq = cJSON_IsNumber(freq) ? freq->valueint : 400000;

    esp_err_t err = esp_idf_i2c_init(sda->valueint, scl->valueint, scl_freq, &data->bus);
    if (err != ESP_OK) { free(data); return err; }

    err = esp_idf_i2c_add_device(data->bus, addr, &data->dev);
    if (err != ESP_OK) { free(data); return err; }

    dev->driver_data = data;
    ESP_LOGI(TAG, "MCP4725 init: SDA=GPIO%d SCL=GPIO%d addr=0x%02X vref=%dmV",
             sda->valueint, scl->valueint, addr, data->vref_mv);
    return ESP_OK;
}

static esp_err_t mcp4725_deinit(device_t *dev)
{
    free(dev->driver_data);
    dev->driver_data = NULL;
    return ESP_OK;
}

static esp_err_t mcp4725_read(device_t *dev, cJSON *value)
{
    mcp4725_data_t *data = dev->driver_data;
    if (!data) return ESP_ERR_INVALID_STATE;

    cJSON_AddNumberToObject(value, "value", data->value);
    double voltage_mv = (double)data->value / 4095.0 * data->vref_mv;
    cJSON_AddNumberToObject(value, "voltage_mv", voltage_mv);
    cJSON_AddNumberToObject(value, "vref_mv", data->vref_mv);
    return ESP_OK;
}

static esp_err_t mcp4725_write(device_t *dev, const cJSON *value)
{
    mcp4725_data_t *data = dev->driver_data;
    if (!data) return ESP_ERR_INVALID_STATE;

    uint16_t dac_value;
    bool to_eeprom = false;

    if (cJSON_IsObject(value)) {
        cJSON *eeprom = cJSON_GetObjectItem(value, "eeprom");
        if (cJSON_IsTrue(eeprom)) to_eeprom = true;
        cJSON *v = cJSON_GetObjectItem(value, "value");
        if (!cJSON_IsNumber(v)) return ESP_ERR_INVALID_ARG;
        dac_value = (uint16_t)v->valueint;
    } else if (cJSON_IsNumber(value)) {
        dac_value = (uint16_t)value->valueint;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    if (dac_value > 4095) return ESP_ERR_INVALID_ARG;

    esp_err_t err = mcp4725_write_value(data, dac_value, to_eeprom);
    if (err == ESP_OK) {
        data->value = dac_value;
        ESP_LOGD(TAG, "MCP4725 set: %u (%.2fmV)", dac_value,
                (double)dac_value / 4095.0 * data->vref_mv);
    }
    return err;
}

static esp_err_t mcp4725_default_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "sda_gpio", 10);
    cJSON_AddNumberToObject(config, "scl_gpio", 11);
    cJSON_AddNumberToObject(config, "i2c_addr", 0x60);
    cJSON_AddNumberToObject(config, "vref_mv", 3300);
    cJSON_AddNumberToObject(config, "scl_freq", 400000);
    return ESP_OK;
}

static const device_type_t mcp4725_driver = {
    .name = "mcp4725",
    .description = "MCP4725 12-bit DAC (I2C)",
    .capabilities = DEVICE_CAPABILITY_WRITE | DEVICE_CAPABILITY_READ,
    .init = mcp4725_init,
    .deinit = mcp4725_deinit,
    .read = mcp4725_read,
    .write = mcp4725_write,
    .get_default_config = mcp4725_default_config,
};

esp_err_t mcp4725_driver_register(void)
{
    return device_type_register(&mcp4725_driver);
}
