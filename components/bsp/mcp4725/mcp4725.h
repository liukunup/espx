/**
 * @file mcp4725.h
 * @brief MCP4725 12-bit DAC Driver (I2C)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief MCP4725 I2C address options
 * (A0 pin state determines LSB)
 */
typedef enum {
    MCP4725_ADDR_A00 = 0x60,  // A0 = GND
    MCP4725_ADDR_A01 = 0x61,  // A0 = VCC
} mcp4725_addr_t;

/**
 * @brief MCP4725 reference voltage
 */
typedef enum {
    MCP4725_VREF_INTERNAL = 0,  // 2.048V internal reference
    MCP4725_VREF_EXTERNAL,      // VDD as reference
} mcp4725_vref_t;

/**
 * @brief MCP4725 power down mode
 */
typedef enum {
    MCP4725_PD_NORMAL = 0,      // Normal operation
    MCP4725_PD_1K,              // 1kΩ to ground
    MCP4725_PD_100K,            // 100kΩ to ground
    MCP4725_PD_500K,            // 500kΩ to ground
} mcp4725_power_down_t;

/**
 * @brief MCP4725 configuration
 */
typedef struct {
    mcp4725_addr_t i2c_addr;       // I2C address
    mcp4725_vref_t vref;           // Reference voltage
    uint32_t i2c_timeout_ms;       // I2C timeout
} mcp4725_config_t;

/**
 * @brief MCP4725 handle
 */
typedef struct mcp4725_handle_s *mcp4725_handle_t;

/**
 * @brief Create MCP4725 driver instance
 * @param config Configuration
 * @return Handle or NULL on error
 */
mcp4725_handle_t mcp4725_create(const mcp4725_config_t *config);

/**
 * @brief Delete MCP4725 driver instance
 * @param handle Driver handle
 */
void mcp4725_delete(mcp4725_handle_t handle);

/**
 * @brief Initialize MCP4725
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int mcp4725_init(mcp4725_handle_t handle);

/**
 * @brief Set output voltage (0-4095 = 0-Vref)
 * @param handle Driver handle
 * @param value 12-bit value (0-4095)
 * @param update_now true to update immediately
 * @return 0 on success, negative on error
 */
int mcp4725_set_value(mcp4725_handle_t handle, uint16_t value, bool update_now);

/**
 * @brief Set voltage in volts (0 to Vref)
 * @param handle Driver handle
 * @param voltage Voltage in volts
 * @param update_now true to update immediately
 * @return 0 on success, negative on error
 */
int mcp4725_set_voltage(mcp4725_handle_t handle, float voltage, bool update_now);

/**
 * @brief Set voltage with channel selection (MCP4728 compatible)
 * @param handle Driver handle
 * @param channel Channel (0 = DAC, ignored for MCP4725)
 * @param value 12-bit value
 * @param pd_mode Power down mode
 * @return 0 on success, negative on error
 */
int mcp4725_set_channel(mcp4725_handle_t handle, uint8_t channel, uint16_t value, mcp4725_power_down_t pd_mode);

/**
 * @brief Set power down mode
 * @param handle Driver handle
 * @param mode Power down mode
 * @return 0 on success, negative on error
 */
int mcp4725_set_power_down(mcp4725_handle_t handle, mcp4725_power_down_t mode);

/**
 * @brief Get current DAC value
 * @param handle Driver handle
 * @param value Output 12-bit value
 * @return 0 on success, negative on error
 */
int mcp4725_get_value(mcp4725_handle_t handle, uint16_t *value);

/**
 * @brief Read current voltage from EEPROM
 * @param handle Driver handle
 * @param voltage Output voltage
 * @return 0 on success, negative on error
 */
int mcp4725_get_eeprom_voltage(mcp4725_handle_t handle, float *voltage);

/**
 * @brief Save current value to EEPROM
 * @param handle Driver handle
 * @param value 12-bit value to save
 * @return 0 on success, negative on error
 */
int mcp4725_save_to_eeprom(mcp4725_handle_t handle, uint16_t value);

/**
 * @brief Get output voltage in volts
 * @param handle Driver handle
 * @param voltage Output voltage
 * @return 0 on success, negative on error
 */
int mcp4725_get_voltage(mcp4725_handle_t handle, float *voltage);

#ifdef __cplusplus
}
#endif
