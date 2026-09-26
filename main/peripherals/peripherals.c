/**
 * @file peripherals.c
 * @brief Register all peripheral drivers
 */

#include "peripherals.h"
#include "dht11.h"
#include "button.h"
#include "relay.h"
#include "shiftreg_595.h"
#include "ws2812.h"

esp_err_t peripherals_register_all(void)
{
    ESP_ERROR_CHECK(dht11_driver_register());
    ESP_ERROR_CHECK(button_driver_register());
    ESP_ERROR_CHECK(relay_driver_register());
    ESP_ERROR_CHECK(shiftreg_595_driver_register());
    ESP_ERROR_CHECK(ws2812_driver_register());
    return ESP_OK;
}
