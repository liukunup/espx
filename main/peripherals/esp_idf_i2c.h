/**
 * @file esp_idf_i2c.h
 * @brief Shared I2C bus infrastructure for ESPX peripheral drivers
 */

#ifndef ESP_IDF_I2C_H
#define ESP_IDF_I2C_H

#include <stdint.h>
#include <stddef.h>
#include <esp_err.h>
#include <driver/i2c.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize I2C bus if not already initialized for this (sda, scl) pair.
 *
 * Maintains a global registry of initialized buses. Repeated calls with the same
 * (sda, scl) return the existing port without re-initializing.
 * Supports up to 2 I2C ports simultaneously.
 *
 * @param sda_gpio SDA GPIO number
 * @param scl_gpio SCL GPIO number
 * @param freq_hz Clock frequency in Hz (e.g. 400000)
 * @param out_port Output pointer for the I2C port number
 * @return ESP_OK on success
 */
esp_err_t esp_idf_i2c_init(int sda_gpio, int scl_gpio, uint32_t freq_hz, i2c_port_t *out_port);

/**
 * @brief Write data to I2C device
 *
 * @param port I2C port number
 * @param addr Device I2C address (7-bit)
 * @param data Data buffer
 * @param len Data length in bytes
 * @return ESP_OK on success
 */
esp_err_t esp_idf_i2c_write(i2c_port_t port, uint8_t addr, const uint8_t *data, size_t len);

/**
 * @brief Read data from I2C device
 *
 * @param port I2C port number
 * @param addr Device I2C address (7-bit)
 * @param data Output buffer
 * @param len Number of bytes to read
 * @return ESP_OK on success
 */
esp_err_t esp_idf_i2c_read(i2c_port_t port, uint8_t addr, uint8_t *data, size_t len);

/**
 * @brief Write and then read I2C device (combined format / repeated start)
 *
 * @param port I2C port number
 * @param addr Device I2C address (7-bit)
 * @param wdata Write buffer
 * @param wlen Write length
 * @param rdata Read output buffer
 * @param rlen Number of bytes to read
 * @return ESP_OK on success
 */
esp_err_t esp_idf_i2c_write_read(i2c_port_t port, uint8_t addr,
                                  const uint8_t *wdata, size_t wlen,
                                  uint8_t *rdata, size_t rlen);

#ifdef __cplusplus
}
#endif

#endif // ESP_IDF_I2C_H
