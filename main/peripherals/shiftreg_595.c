/**
 * @file shiftreg_595.c
 * @brief 74HC595 shift register driver
 *
 * Config:
 *   DS:     Serial data input (DS, pin 14)
 *   SCK:    Shift clock (SHCP, pin 11)
 *   RCK:    Storage clock (STCP, pin 12)
 *   OE:     Output enable (OE, pin 13), -1 to disable
 *   count:  Number of cascaded 595 chips (default 1)
 *
 * Write value: array of bytes (one per chip, big-endian: index 0 = last chip)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <driver/gpio.h>
#include <esp_log.h>
#include <cJSON.h>

#include "shiftreg_595.h"
#include "device_manager.h"

static const char *TAG = "shiftreg_595";

typedef struct {
    int data_gpio;
    int clock_gpio;
    int latch_gpio;
    int oe_gpio;
    int count;
    uint8_t *state;
} shiftreg_data_t;

static void shiftreg_pulse(gpio_num_t gpio)
{
    gpio_set_level(gpio, 0);
    gpio_set_level(gpio, 1);
    gpio_set_level(gpio, 0);
}

static void shiftreg_shift_out(shiftreg_data_t *data, const uint8_t *bytes, int count)
{
    // Send MSB first
    for (int chip = count - 1; chip >= 0; chip--) {
        uint8_t byte = bytes[chip];
        for (int bit = 7; bit >= 0; bit--) {
            gpio_set_level(data->data_gpio, (byte >> bit) & 1);
            shiftreg_pulse(data->clock_gpio);
        }
    }
    // Latch to output
    shiftreg_pulse(data->latch_gpio);
}

static esp_err_t shiftreg_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *din = cJSON_GetObjectItem(config, "DS");
    cJSON *sck = cJSON_GetObjectItem(config, "SCK");
    cJSON *rck = cJSON_GetObjectItem(config, "RCK");
    cJSON *oe = cJSON_GetObjectItem(config, "OE");
    cJSON *count = cJSON_GetObjectItem(config, "count");

    if (!cJSON_IsNumber(din) || !cJSON_IsNumber(sck) ||
        !cJSON_IsNumber(rck)) {
        return ESP_ERR_INVALID_ARG;
    }

    shiftreg_data_t *data = calloc(1, sizeof(shiftreg_data_t));
    if (data == NULL) {
        return ESP_ERR_NO_MEM;
    }

    data->data_gpio = din->valueint;
    data->clock_gpio = sck->valueint;
    data->latch_gpio = rck->valueint;
    data->oe_gpio = cJSON_IsNumber(oe) ? oe->valueint : -1;
    data->count = cJSON_IsNumber(count) ? count->valueint : 1;

    if (data->count < 1) data->count = 1;
    if (data->count > 8) data->count = 8;

    data->state = calloc(data->count, sizeof(uint8_t));
    if (data->state == NULL) {
        free(data);
        return ESP_ERR_NO_MEM;
    }

    // Configure GPIOs
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << data->data_gpio) | (1ULL << data->clock_gpio) |
                         (1ULL << data->latch_gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    if (data->oe_gpio >= 0) {
        io.pin_bit_mask = (1ULL << data->oe_gpio);
        gpio_config(&io);
        gpio_set_level(data->oe_gpio, 0);  // Enable outputs
    }

    // Initialize all outputs to 0
    shiftreg_shift_out(data, data->state, data->count);

    dev->driver_data = data;
    ESP_LOGI(TAG, "74HC595 initialized: count=%d, DS=%d, SCK=%d, RCK=%d",
             data->count, data->data_gpio, data->clock_gpio, data->latch_gpio);

    return ESP_OK;
}

static esp_err_t shiftreg_deinit(device_t *dev)
{
    shiftreg_data_t *data = (shiftreg_data_t*)dev->driver_data;
    if (data == NULL) return ESP_OK;

    if (data->oe_gpio >= 0) {
        gpio_set_level(data->oe_gpio, 1);  // Disable outputs
    }

    free(data->state);
    free(data);
    dev->driver_data = NULL;
    return ESP_OK;
}

static esp_err_t shiftreg_read(device_t *dev, cJSON *value)
{
    shiftreg_data_t *data = (shiftreg_data_t*)dev->driver_data;
    if (data == NULL) return ESP_ERR_INVALID_STATE;

    cJSON *array = cJSON_AddArrayToObject(value, "bytes");
    for (int i = 0; i < data->count; i++) {
        cJSON_AddItemToArray(array, cJSON_CreateNumber(data->state[i]));
    }
    return ESP_OK;
}

static esp_err_t shiftreg_write(device_t *dev, const cJSON *value)
{
    shiftreg_data_t *data = (shiftreg_data_t*)dev->driver_data;
    if (data == NULL) return ESP_ERR_INVALID_STATE;

    uint8_t bytes[8] = {0};

    // Accept array of bytes
    if (cJSON_IsArray(value)) {
        int idx = 0;
        cJSON *item;
        cJSON_ArrayForEach(item, value) {
            if (idx >= data->count) break;
            if (cJSON_IsNumber(item)) {
                bytes[idx] = (uint8_t)item->valueint;
            }
            idx++;
        }
    }
    // Accept object with "bytes" array
    else if (cJSON_IsObject(value)) {
        cJSON *array = cJSON_GetObjectItem(value, "bytes");
        if (cJSON_IsArray(array)) {
            int idx = 0;
            cJSON *item;
            cJSON_ArrayForEach(item, array) {
                if (idx >= data->count) break;
                if (cJSON_IsNumber(item)) {
                    bytes[idx] = (uint8_t)item->valueint;
                }
                idx++;
            }
        }
    }
    // Accept single byte (single 595)
    else if (cJSON_IsNumber(value)) {
        bytes[0] = (uint8_t)value->valueint;
    } else {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(data->state, bytes, data->count);
    shiftreg_shift_out(data, data->state, data->count);

    ESP_LOGI(TAG, "74HC595 '%s' updated", dev->id);
    return ESP_OK;
}

static esp_err_t shiftreg_default_config(cJSON *config)
{
    /* Deliberately avoids GPIO17/18: those are UART1's IO_MUX pins and UART1 is
     * the AT command interface by default. A suggested default that collides
     * with a fixed peripheral is a trap. */
    cJSON_AddNumberToObject(config, "din", 16);
    cJSON_AddNumberToObject(config, "clock_gpio", 15);
    cJSON_AddNumberToObject(config, "latch_gpio", 7);
    cJSON_AddNumberToObject(config, "oe_gpio", -1);
    cJSON_AddNumberToObject(config, "count", 1);
    return ESP_OK;
}

static const device_type_t shiftreg_driver = {
    .name = "shiftreg_595",
    .description = "74HC595 cascadable shift register output",
    .description_zh = "74hc595 移位寄存器",
    .save_state = true,
    .capabilities = DEVICE_CAPABILITY_READ | DEVICE_CAPABILITY_WRITE | DEVICE_CAPABILITY_NOTIFY,
    .init = shiftreg_init,
    .deinit = shiftreg_deinit,
    .read = shiftreg_read,
    .write = shiftreg_write,
    .get_default_config = shiftreg_default_config,
};

esp_err_t shiftreg_595_driver_register(void)
{
    return device_type_register(&shiftreg_driver);
}
