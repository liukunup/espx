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
#include "tja1050.h"
#include "mcp4725.h"
#include "ads1115.h"
#include "ina226.h"
#include "buzzer.h"

esp_err_t peripherals_register_all(void)
{
    ESP_ERROR_CHECK(dht11_driver_register());
    ESP_ERROR_CHECK(button_driver_register());
    ESP_ERROR_CHECK(relay_driver_register());
    ESP_ERROR_CHECK(shiftreg_595_driver_register());
    ESP_ERROR_CHECK(ws2812_driver_register());
    ESP_ERROR_CHECK(tja1050_driver_register());
    ESP_ERROR_CHECK(mcp4725_driver_register());
    ESP_ERROR_CHECK(ads1115_driver_register());
    ESP_ERROR_CHECK(ina226_driver_register());
    ESP_ERROR_CHECK(buzzer_driver_register());
    return ESP_OK;
}
