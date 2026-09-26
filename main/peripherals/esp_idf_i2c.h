/**
 * @file esp_idf_i2c.h
 * @brief Shared I2C bus infrastructure for ESPX peripheral drivers
 *
 * Uses the new ESP-IDF v6.x driver/i2c_master.h API.
 */

#ifndef ESP_IDF_I2C_H
#define ESP_IDF_I2C_H

#include <stdint.h>
#include <stddef.h>
#include <esp_err.h>
#include <driver/i2c_master.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize I2C master bus for the given (sda, scl) GPIO pair.
 *
 * Maintains a global registry of initialized buses. Repeated calls with the same
 * (sda, scl) return the existing bus handle without re-initializing.
 * Supports up to 2 I2C buses simultaneously.
 *
 * @param sda_gpio SDA GPIO number
 * @param scl_gpio SCL GPIO number
 * @param freq_hz Clock frequency in Hz (e.g. 400000)
 * @param out_bus Output pointer for the I2C master bus handle
 * @return ESP_OK on success
 */
esp_err_t esp_idf_i2c_init(int sda_gpio, int scl_gpio, uint32_t freq_hz,
                            i2c_master_bus_handle_t *out_bus);

/**
 * @brief Add an I2C device to a bus and return a device handle.
 *
 * @param bus I2C master bus handle
 * @param addr 7-bit I2C device address
 * @param out_dev Output pointer for the I2C device handle
 * @return ESP_OK on success
 */
esp_err_t esp_idf_i2c_add_device(i2c_master_bus_handle_t bus, uint8_t addr,
                                  i2c_master_dev_handle_t *out_dev);

/**
 * @brief Write data to an I2C device
 *
 * @param dev I2C device handle
 * @param data Data buffer
 * @param len Data length in bytes
 * @return ESP_OK on success
 */
esp_err_t esp_idf_i2c_write(i2c_master_dev_handle_t dev,
                             const uint8_t *data, size_t len);

/**
 * @brief Read data from an I2C device
 *
 * @param dev I2C device handle
 * @param data Output buffer
 * @param len Number of bytes to read
 * @return ESP_OK on success
 */
esp_err_t esp_idf_i2c_read(i2c_master_dev_handle_t dev,
                            uint8_t *data, size_t len);

/**
 * @brief Combined write-then-read (repeated start) on an I2C device
 *
 * @param dev I2C device handle
 * @param wdata Write buffer
 * @param wlen Write length
 * @param rdata Read output buffer
 * @param rlen Number of bytes to read
 * @return ESP_OK on success
 */
esp_err_t esp_idf_i2c_write_read(i2c_master_dev_handle_t dev,
                                  const uint8_t *wdata, size_t wlen,
                                  uint8_t *rdata, size_t rlen);

#ifdef __cplusplus
}
#endif

#endif // ESP_IDF_I2C_H
