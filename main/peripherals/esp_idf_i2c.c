/**
 * @file esp_idf_i2c.c
 * @brief Shared I2C bus infrastructure for ESPX peripheral drivers
 */

#include "esp_idf_i2c.h"
#include <string.h>
#include <esp_log.h>

static const char *TAG = "i2c_bus";

#define MAX_I2C_PORTS 2

typedef struct {
    bool in_use;
    int sda_gpio;
    int scl_gpio;
    i2c_port_t port;
} i2c_bus_entry_t;

static i2c_bus_entry_t g_i2c_buses[MAX_I2C_PORTS] = {0};

static bool gpio_pair_match(int sda1, int scl1, int sda2, int scl2)
{
    return (sda1 == sda2 && scl1 == scl2);
}

esp_err_t esp_idf_i2c_init(int sda_gpio, int scl_gpio, uint32_t freq_hz, i2c_port_t *out_port)
{
    if (out_port == NULL) return ESP_ERR_INVALID_ARG;

    /* Check if already initialized for this (sda, scl) pair */
    for (int i = 0; i < MAX_I2C_PORTS; i++) {
        if (g_i2c_buses[i].in_use &&
            gpio_pair_match(g_i2c_buses[i].sda_gpio, g_i2c_buses[i].scl_gpio,
                           sda_gpio, scl_gpio)) {
            *out_port = g_i2c_buses[i].port;
            ESP_LOGD(TAG, "Reusing I2C port %d for SDA=%d SCL=%d",
                     (int)g_i2c_buses[i].port, sda_gpio, scl_gpio);
            return ESP_OK;
        }
    }

    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_I2C_PORTS; i++) {
        if (!g_i2c_buses[i].in_use) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        ESP_LOGE(TAG, "No free I2C port slots");
        return ESP_ERR_NO_MEM;
    }

    i2c_port_t port = (i2c_port_t)slot;

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = freq_hz,
    };

    ESP_ERROR_CHECK(i2c_param_config(port, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(port, I2C_MODE_MASTER, 0, 0, 0));

    g_i2c_buses[slot].in_use = true;
    g_i2c_buses[slot].sda_gpio = sda_gpio;
    g_i2c_buses[slot].scl_gpio = scl_gpio;
    g_i2c_buses[slot].port = port;

    *out_port = port;
    ESP_LOGI(TAG, "I2C port %d initialized: SDA=%d SCL=%d %luHz",
             (int)port, sda_gpio, scl_gpio, (unsigned long)freq_hz);
    return ESP_OK;
}

esp_err_t esp_idf_i2c_write(i2c_port_t port, uint8_t addr, const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, addr << 1, true));
    if (len > 0) {
        ESP_ERROR_CHECK(i2c_master_write(cmd, data, len, true));
    }
    ESP_ERROR_CHECK(i2c_master_stop(cmd));
    esp_err_t err = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t esp_idf_i2c_read(i2c_port_t port, uint8_t addr, uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, (addr << 1) | 1, true));
    if (len > 0) {
        ESP_ERROR_CHECK(i2c_master_read(cmd, data, len, I2C_MASTER_LAST_NACK));
    }
    ESP_ERROR_CHECK(i2c_master_stop(cmd));
    esp_err_t err = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}

esp_err_t esp_idf_i2c_write_read(i2c_port_t port, uint8_t addr,
                                  const uint8_t *wdata, size_t wlen,
                                  uint8_t *rdata, size_t rlen)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, addr << 1, true));
    if (wlen > 0) {
        ESP_ERROR_CHECK(i2c_master_write(cmd, wdata, wlen, true));
    }
    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, (addr << 1) | 1, true));
    if (rlen > 0) {
        ESP_ERROR_CHECK(i2c_master_read(cmd, rdata, rlen, I2C_MASTER_LAST_NACK));
    }
    ESP_ERROR_CHECK(i2c_master_stop(cmd));
    esp_err_t err = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    return err;
}
