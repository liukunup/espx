/**
 * @file dht11.h
 * @brief DHT11/DHT22 Temperature and Humidity Sensor Driver
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief DHT sensor type
 */
typedef enum {
    DHT_TYPE_DHT11 = 0,   // DHT11 (20-90% RH, 0-50°C, ±2°C)
    DHT_TYPE_DHT12,       // DHT12 (same as DHT11)
    DHT_TYPE_DHT21,       // DHT21/AM2301
    DHT_TYPE_DHT22,       // DHT22/AM2302 (0-100% RH, -40-80°C, ±0.5°C)
} dht_type_t;

/**
 * @brief DHT reading result
 */
typedef struct {
    float temperature;    // Temperature in Celsius
    float humidity;       // Relative humidity in %
    uint32_t timestamp;   // Reading timestamp
    bool valid;           // Data validity
} dht_data_t;

/**
 * @brief DHT configuration
 */
typedef struct {
    int8_t gpio_num;      // GPIO pin number
    dht_type_t type;       // Sensor type
    bool internal_pullup;  // Use internal pull-up resistor
} dht_config_t;

/**
 * @brief DHT handle
 */
typedef struct dht_handle_s *dht_handle_t;

/**
 * @brief Create DHT driver instance
 * @param config Configuration
 * @return Handle or NULL on error
 */
dht_handle_t dht_create(const dht_config_t *config);

/**
 * @brief Delete DHT driver instance
 * @param handle Driver handle
 */
void dht_delete(dht_handle_t handle);

/**
 * @brief Initialize DHT sensor
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int dht_init(dht_handle_t handle);

/**
 * @brief Read temperature and humidity
 * @param handle Driver handle
 * @param data Output data
 * @return 0 on success, negative on error
 */
int dht_read(dht_handle_t handle, dht_data_t *data);

/**
 * @brief Read temperature only (faster)
 * @param handle Driver handle
 * @param temperature Output temperature
 * @return 0 on success, negative on error
 */
int dht_read_temperature(dht_handle_t handle, float *temperature);

/**
 * @brief Read humidity only (faster)
 * @param handle Driver handle
 * @param humidity Output humidity
 * @return 0 on success, negative on error
 */
int dht_read_humidity(dht_handle_t handle, float *humidity);

/**
 * @brief Check if sensor is responding
 * @param handle Driver handle
 * @param responsive Output responsive status
 * @return 0 on success, negative on error
 */
int dht_check(dht_handle_t handle, bool *responsive);

#ifdef __cplusplus
}
#endif
