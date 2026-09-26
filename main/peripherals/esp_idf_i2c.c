/**
 * @file esp_idf_i2c.c
 * @brief Shared I2C bus infrastructure using driver/i2c_master.h (ESP-IDF v6.x)
 */

#include "esp_idf_i2c.h"
#include <string.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

static const char *TAG = "i2c_bus";

#define MAX_I2C_BUSES 2

typedef struct {
    bool in_use;
    int sda_gpio;
    int scl_gpio;
    i2c_master_bus_handle_t bus;
} i2c_bus_entry_t;

static i2c_bus_entry_t g_i2c_buses[MAX_I2C_BUSES] = {0};

static bool gpio_pair_match(int sda1, int scl1, int sda2, int scl2)
{
    return (sda1 == sda2 && scl1 == scl2);
}

esp_err_t esp_idf_i2c_init(int sda_gpio, int scl_gpio, uint32_t freq_hz,
                            i2c_master_bus_handle_t *out_bus)
{
    if (out_bus == NULL) return ESP_ERR_INVALID_ARG;

    /* Check if already initialized for this (sda, scl) pair */
    for (int i = 0; i < MAX_I2C_BUSES; i++) {
        if (g_i2c_buses[i].in_use &&
            gpio_pair_match(g_i2c_buses[i].sda_gpio, g_i2c_buses[i].scl_gpio,
                           sda_gpio, scl_gpio)) {
            *out_bus = g_i2c_buses[i].bus;
            ESP_LOGD(TAG, "Reusing I2C bus %d for SDA=%d SCL=%d", i, sda_gpio, scl_gpio);
            return ESP_OK;
        }
    }

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_I2C_BUSES; i++) {
        if (!g_i2c_buses[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        ESP_LOGE(TAG, "No free I2C bus slots");
        return ESP_ERR_NO_MEM;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_AUTO,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    g_i2c_buses[slot] = (i2c_bus_entry_t){
        .in_use = true,
        .sda_gpio = sda_gpio,
        .scl_gpio = scl_gpio,
        .bus = bus,
    };

    *out_bus = bus;
    ESP_LOGI(TAG, "I2C bus %d initialized: SDA=GPIO%d SCL=GPIO%d %luHz",
             slot, sda_gpio, scl_gpio, (unsigned long)freq_hz);
    return ESP_OK;
}

esp_err_t esp_idf_i2c_add_device(i2c_master_bus_handle_t bus, uint8_t addr,
                                  i2c_master_dev_handle_t *out_dev)
{
    if (out_dev == NULL) return ESP_ERR_INVALID_ARG;

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
        .flags.disable_ack_check = false,
    };

    return i2c_master_bus_add_device(bus, &dev_cfg, out_dev);
}

esp_err_t esp_idf_i2c_write(i2c_master_dev_handle_t dev,
                             const uint8_t *data, size_t len)
{
    return i2c_master_transmit(dev, data, len, pdMS_TO_TICKS(100));
}

esp_err_t esp_idf_i2c_read(i2c_master_dev_handle_t dev,
                            uint8_t *data, size_t len)
{
    return i2c_master_receive(dev, data, len, pdMS_TO_TICKS(100));
}

esp_err_t esp_idf_i2c_write_read(i2c_master_dev_handle_t dev,
                                  const uint8_t *wdata, size_t wlen,
                                  uint8_t *rdata, size_t rlen)
{
    return i2c_master_transmit_receive(dev, wdata, wlen, rdata, rlen, pdMS_TO_TICKS(100));
}
