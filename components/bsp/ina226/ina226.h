/**
 * @file ina226.h
 * @brief INA226 High-Side Current/Voltage Monitor Driver (I2C)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief INA226 I2C address options
 * (A0 and A1 pins determine lower bits)
 */
typedef enum {
    INA226_ADDR_40H = 0x40,  // A1=0, A0=0
    INA226_ADDR_41H = 0x41,  // A1=0, A0=1
    INA226_ADDR_44H = 0x44,  // A1=1, A0=0
    INA226_ADDR_45H = 0x45,  // A1=1, A0=1
} ina226_addr_t;

/**
 * @brief INA226 averaging mode
 */
typedef enum {
    INA226_AVG_1   = 0,  // 1 sample
    INA226_AVG_4   = 1,  // 4 samples
    INA226_AVG_16  = 2,  // 16 samples
    INA226_AVG_64  = 3,  // 64 samples
    INA226_AVG_128 = 4,  // 128 samples
    INA226_AVG_256 = 5,  // 256 samples
    INA226_AVG_512 = 6,  // 512 samples
    INA226_AVG_1024 = 7, // 1024 samples
} ina226_avg_t;

/**
 * @brief INA226 bus voltage conversion time
 */
typedef enum {
    INA226_VBUS_CT_140US  = 0,  // 140μs
    INA226_VBUS_CT_204US  = 1,  // 204μs
    INA226_VBUS_CT_332US  = 2,  // 332μs
    INA226_VBUS_CT_588US  = 3,  // 588μs
    INA226_VBUS_CT_1100US = 4,  // 1.1ms
    INA226_VBUS_CT_2116US = 5,  // 2.116ms
    INA226_VBUS_CT_4156US = 6,  // 4.156ms
    INA226_VBUS_CT_8244US = 7,  // 8.244ms
} ina226_vbus_ct_t;

/**
 * @brief INA226 shunt voltage conversion time
 */
typedef enum {
    INA226_VSH_CT_140US  = 0,  // 140μs
    INA226_VSH_CT_204US  = 1,  // 204μs
    INA226_VSH_CT_332US  = 2,  // 332μs
    INA226_VSH_CT_588US  = 3,  // 588μs
    INA226_VSH_CT_1100US = 4,  // 1.1ms
    INA226_VSH_CT_2116US = 5,  // 2.116ms
    INA226_VSH_CT_4156US = 6,  // 4.156ms
    INA226_VSH_CT_8244US = 7,  // 8.244ms
} ina226_vsh_ct_t;

/**
 * @brief INA226 operating mode
 */
typedef enum {
    INA226_MODE_POWER_DOWN = 0,
    INA226_MODE_SHUNT_TRIG = 1,
    INA226_MODE_BUS_TRIG = 2,
    INA226_MODE_SHUNT_BUS_TRIG = 3,
    INA226_MODE_ADC_OFF = 4,
    INA226_MODE_SHUNT_CONT = 5,
    INA226_MODE_BUS_CONT = 6,
    INA226_MODE_SHUNT_BUS_CONT = 7,  // Continuous mode
} ina226_mode_t;

/**
 * @brief INA226 configuration
 */
typedef struct {
    ina226_addr_t i2c_addr;
    float r_shunt_ohm;       // Shunt resistor value in ohms
    float max_current_amp;    // Expected max current for calibration
    ina226_avg_t avg_mode;
    ina226_vbus_ct_t vbus_ct;
    ina226_vsh_ct_t vsh_ct;
    ina226_mode_t mode;
    uint32_t i2c_timeout_ms;
} ina226_config_t;

/**
 * @brief INA226 measurement data
 */
typedef struct {
    float bus_voltage;      // Bus voltage in volts
    float shunt_voltage;   // Shunt voltage in mV
    float current_amp;      // Current in amps
    float power_watt;      // Power in watts
    uint32_t timestamp;    // Reading timestamp
    bool valid;            // Data validity
} ina226_data_t;

/**
 * @brief INA226 handle
 */
typedef struct ina226_handle_s *ina226_handle_t;

/**
 * @brief Create INA226 driver instance
 * @param config Configuration
 * @return Handle or NULL on error
 */
ina226_handle_t ina226_create(const ina226_config_t *config);

/**
 * @brief Delete INA226 driver instance
 * @param handle Driver handle
 */
void ina226_delete(ina226_handle_t handle);

/**
 * @brief Initialize INA226
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int ina226_init(ina226_handle_t handle);

/**
 * @brief Configure INA226
 * @param handle Driver handle
 * @param avg Averaging mode
 * @param vbus_ct Bus voltage conversion time
 * @param vsh_ct Shunt voltage conversion time
 * @param mode Operating mode
 * @return 0 on success, negative on error
 */
int ina226_configure(ina226_handle_t handle, ina226_avg_t avg, ina226_vbus_ct_t vbus_ct,
                     ina226_vsh_ct_t vsh_ct, ina226_mode_t mode);

/**
 * @brief Read all measurements
 * @param handle Driver handle
 * @param data Output data
 * @return 0 on success, negative on error
 */
int ina226_read(ina226_handle_t handle, ina226_data_t *data);

/**
 * @brief Read bus voltage only
 * @param handle Driver handle
 * @param voltage Output voltage in volts
 * @return 0 on success, negative on error
 */
int ina226_read_bus_voltage(ina226_handle_t handle, float *voltage);

/**
 * @brief Read shunt voltage only
 * @param handle Driver handle
 * @param voltage Output voltage in mV
 * @return 0 on success, negative on error
 */
int ina226_read_shunt_voltage(ina226_handle_t handle, float *voltage);

/**
 * @brief Read current
 * @param handle Driver handle
 * @param current Output current in amps
 * @return 0 on success, negative on error
 */
int ina226_read_current(ina226_handle_t handle, float *current);

/**
 * @brief Read power
 * @param handle Driver handle
 * @param power Output power in watts
 * @return 0 on success, negative on error
 */
int ina226_read_power(ina226_handle_t handle, float *power);

/**
 * @brief Set calibration
 * @param handle Driver handle
 * @param r_shunt Shunt resistor value in ohms
 * @param max_current Expected max current in amps
 * @return 0 on success, negative on error
 */
int ina226_set_calibration(ina226_handle_t handle, float r_shunt, float max_current);

/**
 * @brief Check if device is present
 * @param handle Driver handle
 * @param present Output presence status
 * @return 0 on success, negative on error
 */
int ina226_check_present(ina226_handle_t handle, bool *present);

#ifdef __cplusplus
}
#endif
