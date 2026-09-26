/**
 * @file ads1115.c
 * @brief ADS1115 16-bit ADC (I2C) driver
 *
 * Device type name: "ads1115"
 * 4-channel single-ended or 2-channel differential ADC.
 */

#include <string.h>
#include <stdlib.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>

#include "ads1115.h"
#include "device_manager.h"
#include "esp_idf_i2c.h"

static const char *TAG = "ads1115";

/* ADS1115 registers */
#define ADS1115_REG_CONVERSION  0x00
#define ADS1115_REG_CONFIG      0x01

/* PGA full-scale voltages in mV */
static const int ADS1115_PGA_FS[8] = {6144, 4096, 2048, 1024, 512, 256, 256, 256};

/* Data rates in SPS */
static const int ADS1115_RATE[8] = {8, 16, 32, 64, 128, 250, 475, 860};

/* MUX codes for single-ended channels (vs GND) */
static const uint8_t ADS1115_MUX_SINGLE[4] = {0x04, 0x05, 0x06, 0x07};

typedef struct {
    i2c_port_t i2c_port;
    uint8_t i2c_addr;
    int channel;          /* 0-3 single-ended vs GND */
    int gain;             /* PGA gain code 0-7 */
    int rate;             /* SPS code 0-7 */
    int interval_ms;
    int64_t last_read_us;
    int16_t last_raw;
    double last_mv;
} ads1115_data_t;

static esp_err_t ads1115_trigger_read(ads1115_data_t *data)
{
    uint8_t mux = ADS1115_MUX_SINGLE[data->channel & 0x03];
    uint8_t pga = (uint8_t)(data->gain & 0x07);
    uint8_t dr  = (uint8_t)(data->rate & 0x07);

    uint16_t config = (1 << 15) |       /* OS: start conversion */
                      ((uint16_t)mux << 12) |
                      ((uint16_t)pga << 9)  |
                      (1 << 8)            |  /* MODE: single-shot */
                      ((uint16_t)dr << 5);

    uint8_t cmd[3] = {
        ADS1115_REG_CONFIG,
        (uint8_t)(config >> 8),
        (uint8_t)(config & 0xFF),
    };
    return esp_idf_i2c_write(data->i2c_port, data->i2c_addr, cmd, 3);
}

static esp_err_t ads1115_read_raw(ads1115_data_t *data, int16_t *out_raw)
{
    uint8_t reg = ADS1115_REG_CONVERSION;
    esp_err_t err = esp_idf_i2c_write(data->i2c_port, data->i2c_addr, &reg, 1);
    if (err != ESP_OK) return err;

    uint8_t buf[2];
    err = esp_idf_i2c_read(data->i2c_port, data->i2c_addr, buf, 2);
    if (err != ESP_OK) return err;

    *out_raw = (int16_t)((buf[0] << 8) | buf[1]);
    return ESP_OK;
}

static esp_err_t ads1115_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) return ESP_ERR_INVALID_ARG;

    cJSON *sda = cJSON_GetObjectItem(config, "sda_gpio");
    cJSON *scl = cJSON_GetObjectItem(config, "scl_gpio");
    if (!cJSON_IsNumber(sda) || !cJSON_IsNumber(scl)) return ESP_ERR_INVALID_ARG;

    ads1115_data_t *data = calloc(1, sizeof(ads1115_data_t));
    if (!data) return ESP_ERR_NO_MEM;

    data->i2c_addr = 0x48;
    data->channel = 0;
    data->gain = 1;    /* ±4.096V range */
    data->rate = 4;    /* 128 SPS */
    data->interval_ms = 1000;
    data->last_read_us = 0;

    cJSON *addr = cJSON_GetObjectItem(config, "i2c_addr");
    cJSON *ch   = cJSON_GetObjectItem(config, "channel");
    cJSON *gn   = cJSON_GetObjectItem(config, "gain");
    cJSON *rt   = cJSON_GetObjectItem(config, "rate");
    cJSON *iv   = cJSON_GetObjectItem(config, "interval_ms");
    cJSON *freq = cJSON_GetObjectItem(config, "scl_freq");

    if (cJSON_IsNumber(addr)) data->i2c_addr = (uint8_t)addr->valueint;
    if (cJSON_IsNumber(ch))   data->channel = ch->valueint;
    if (cJSON_IsNumber(gn))   data->gain = gn->valueint;
    if (cJSON_IsNumber(rt))   data->rate = rt->valueint;
    if (cJSON_IsNumber(iv))   data->interval_ms = iv->valueint;

    uint32_t scl_freq = cJSON_IsNumber(freq) ? freq->valueint : 400000;

    esp_err_t err = esp_idf_i2c_init(sda->valueint, scl->valueint, scl_freq, &data->i2c_port);
    if (err != ESP_OK) { free(data); return err; }

    dev->driver_data = data;
    ESP_LOGI(TAG, "ADS1115 init: SDA=GPIO%d SCL=GPIO%d addr=0x%02X ch=%d gain=%d rate=%dSPS",
             sda->valueint, scl->valueint, data->i2c_addr, data->channel,
             data->gain, ADS1115_RATE[data->rate & 0x07]);
    return ESP_OK;
}

static esp_err_t ads1115_deinit(device_t *dev)
{
    free(dev->driver_data);
    dev->driver_data = NULL;
    return ESP_OK;
}

static esp_err_t ads1115_read(device_t *dev, cJSON *value)
{
    ads1115_data_t *data = dev->driver_data;
    if (!data) return ESP_ERR_INVALID_STATE;

    cJSON_AddNumberToObject(value, "raw", data->last_raw);
    cJSON_AddNumberToObject(value, "mv", data->last_mv);
    cJSON_AddNumberToObject(value, "channel", data->channel);
    cJSON_AddNumberToObject(value, "gain", data->gain);
    cJSON_AddNumberToObject(value, "rate", ADS1115_RATE[data->rate & 0x07]);
    return ESP_OK;
}

static esp_err_t ads1115_tick(device_t *dev)
{
    ads1115_data_t *data = dev->driver_data;
    if (!data) return ESP_OK;

    int64_t now_us = esp_timer_get_time();
    int64_t interval_us = (int64_t)data->interval_ms * 1000;
    if (data->last_read_us > 0 && (now_us - data->last_read_us) < interval_us) {
        return ESP_OK;
    }

    esp_err_t err = ads1115_trigger_read(data);
    if (err != ESP_OK) return err;

    /* Conversion time: 1 / SPS + margin */
    uint32_t conv_time_ms = (1000 / ADS1115_RATE[data->rate & 0x07]) + 2;
    vTaskDelay(pdMS_TO_TICKS(conv_time_ms));

    int16_t raw;
    err = ads1115_read_raw(data, &raw);
    if (err == ESP_OK) {
        data->last_raw = raw;
        double fs_mv = ADS1115_PGA_FS[data->gain & 0x07];
        data->last_mv = (raw / 32768.0) * fs_mv;
        data->last_read_us = esp_timer_get_time();
        ESP_LOGD(TAG, "ADS1115 ch%d: raw=%d mv=%.2f", data->channel, raw, data->last_mv);
    }
    return err;
}

static esp_err_t ads1115_default_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "sda_gpio", 10);
    cJSON_AddNumberToObject(config, "scl_gpio", 11);
    cJSON_AddNumberToObject(config, "i2c_addr", 0x48);
    cJSON_AddNumberToObject(config, "channel", 0);
    cJSON_AddNumberToObject(config, "gain", 1);
    cJSON_AddNumberToObject(config, "rate", 4);
    cJSON_AddNumberToObject(config, "interval_ms", 1000);
    cJSON_AddNumberToObject(config, "scl_freq", 400000);
    return ESP_OK;
}

static esp_err_t ads1115_validate_config(const cJSON *config)
{
    cJSON *ch = cJSON_GetObjectItem(config, "channel");
    cJSON *gn = cJSON_GetObjectItem(config, "gain");
    cJSON *rt = cJSON_GetObjectItem(config, "rate");
    if (cJSON_IsNumber(ch) && (ch->valueint < 0 || ch->valueint > 3)) return ESP_ERR_INVALID_ARG;
    if (cJSON_IsNumber(gn) && (gn->valueint < 0 || gn->valueint > 7)) return ESP_ERR_INVALID_ARG;
    if (cJSON_IsNumber(rt) && (rt->valueint < 0 || rt->valueint > 7)) return ESP_ERR_INVALID_ARG;
    return ESP_OK;
}

static const device_type_t ads1115_driver = {
    .name = "ads1115",
    .description = "ADS1115 16-bit ADC (I2C)",
    .capabilities = DEVICE_CAPABILITY_READ | DEVICE_CAPABILITY_PERIODIC | DEVICE_CAPABILITY_NOTIFY,
    .init = ads1115_init,
    .deinit = ads1115_deinit,
    .read = ads1115_read,
    .tick = ads1115_tick,
    .get_default_config = ads1115_default_config,
    .validate_config = ads1115_validate_config,
};

esp_err_t ads1115_driver_register(void)
{
    return device_type_register(&ads1115_driver);
}
