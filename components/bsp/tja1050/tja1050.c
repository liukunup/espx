/**
 * @file tja1050.c
 * @brief TJA1050 CAN Transceiver Driver Implementation
 */

#include "tja1050.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"
#include <stdlib.h>

static const char *TAG = "tja1050";

/** @brief Internal handle structure */
struct tja1050_handle_s {
    int8_t tx_gpio;
    int8_t rx_gpio;
    int8_t stb_gpio;
    int8_t en_gpio;
    tja1050_mode_t mode;
    bool initialized;
};

tja1050_handle_t tja1050_create(const tja1050_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    
    tja1050_handle_t handle = (tja1050_handle_t)calloc(1, sizeof(struct tja1050_handle_s));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return NULL;
    }
    
    handle->tx_gpio = config->tx_gpio;
    handle->rx_gpio = config->rx_gpio;
    handle->stb_gpio = config->stb_gpio;
    handle->en_gpio = config->en_gpio;
    handle->mode = config->default_mode;
    handle->initialized = false;
    
    ESP_LOGI(TAG, "TJA1050 created: TX=GPIO%d, RX=GPIO%d, STB=GPIO%d, EN=GPIO%d",
             config->tx_gpio, config->rx_gpio, config->stb_gpio, config->en_gpio);
    
    return handle;
}

void tja1050_delete(tja1050_handle_t handle) {
    if (handle == NULL) return;
    
    if (handle->initialized) {
        if (handle->tx_gpio >= 0) gpio_reset_pin(handle->tx_gpio);
        if (handle->rx_gpio >= 0) gpio_reset_pin(handle->rx_gpio);
        if (handle->stb_gpio >= 0) gpio_reset_pin(handle->stb_gpio);
        if (handle->en_gpio >= 0) gpio_reset_pin(handle->en_gpio);
    }
    
    free(handle);
    ESP_LOGI(TAG, "TJA1050 deleted");
}

int tja1050_init(tja1050_handle_t handle) {
    if (handle == NULL) return -1;
    if (handle->initialized) return 0;
    
    gpio_config_t io_conf = { 0 };
    
    // Configure TX pin as output
    if (handle->tx_gpio >= 0) {
        io_conf.pin_bit_mask = (1ULL << handle->tx_gpio);
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.intr_type = GPIO_INTR_DISABLE;
        gpio_config(&io_conf);
        gpio_set_level(handle->tx_gpio, 1);  // Recessive level
    }
    
    // Configure RX pin as input
    if (handle->rx_gpio >= 0) {
        io_conf.pin_bit_mask = (1ULL << handle->rx_gpio);
        io_conf.mode = GPIO_MODE_INPUT;
        gpio_config(&io_conf);
    }
    
    // Configure STB pin as output
    if (handle->stb_gpio >= 0) {
        io_conf.pin_bit_mask = (1ULL << handle->stb_gpio);
        io_conf.mode = GPIO_MODE_OUTPUT;
        gpio_config(&io_conf);
    }
    
    // Configure EN pin as output
    if (handle->en_gpio >= 0) {
        io_conf.pin_bit_mask = (1ULL << handle->en_gpio);
        io_conf.mode = GPIO_MODE_OUTPUT;
        gpio_config(&io_conf);
        gpio_set_level(handle->en_gpio, 1);  // Enable
    }
    
    // Apply default mode
    tja1050_set_mode(handle, handle->mode);
    
    handle->initialized = true;
    ESP_LOGI(TAG, "TJA1050 initialized in %s mode", 
             handle->mode == TJA1050_MODE_NORMAL ? "NORMAL" :
             handle->mode == TJA1050_MODE_STANDBY ? "STANDBY" : "SILENT");
    
    return 0;
}

int tja1050_set_mode(tja1050_handle_t handle, tja1050_mode_t mode) {
    if (handle == NULL) return -1;
    
    if (handle->stb_gpio < 0 && handle->en_gpio < 0) {
        ESP_LOGW(TAG, "No control pins configured, cannot set mode");
        return -2;
    }
    
    bool stb_level = false;
    bool en_level = true;
    
    switch (mode) {
        case TJA1050_MODE_NORMAL:
            stb_level = false;  // Normal mode (STB low)
            en_level = true;
            break;
        case TJA1050_MODE_STANDBY:
            stb_level = true;   // Standby mode (STB high)
            en_level = true;
            break;
        case TJA1050_MODE_SILENT:
            stb_level = true;   // Silent mode (STB high)
            en_level = false;   // EN low for listen-only
            break;
    }
    
    if (handle->stb_gpio >= 0) {
        gpio_set_level(handle->stb_gpio, stb_level ? 1 : 0);
    }
    if (handle->en_gpio >= 0) {
        gpio_set_level(handle->en_gpio, en_level ? 1 : 0);
    }
    
    handle->mode = mode;
    ESP_LOGI(TAG, "TJA1050 mode set to %d", mode);
    
    return 0;
}

int tja1050_get_mode(tja1050_handle_t handle, tja1050_mode_t *mode) {
    if (handle == NULL || mode == NULL) return -1;
    *mode = handle->mode;
    return 0;
}

int tja1050_send_bit(tja1050_handle_t handle, bool can_tx_level) {
    if (handle == NULL) return -1;
    if (handle->tx_gpio < 0) return -2;
    
    // TXD level: 0 = dominant, 1 = recessive
    gpio_set_level(handle->tx_gpio, can_tx_level ? 0 : 1);
    return 0;
}

int tja1050_read_bit(tja1050_handle_t handle, bool *can_rx_level) {
    if (handle == NULL || can_rx_level == NULL) return -1;
    if (handle->rx_gpio < 0) return -2;
    
    int level = gpio_get_level(handle->rx_gpio);
    // RXD level: 0 = dominant, 1 = recessive
    *can_rx_level = (level == 0);
    return 0;
}

int tja1050_check_power(tja1050_handle_t handle, bool *powered) {
    if (handle == NULL || powered == NULL) return -1;
    
    // In standby mode, TXD and RXD are both high (recessive)
    // In normal mode with dominant bit, RXD goes low
    *powered = (handle->mode != TJA1050_MODE_STANDBY);
    return 0;
}
