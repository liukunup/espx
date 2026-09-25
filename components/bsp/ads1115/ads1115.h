/**
 * @file ads1115.h
 * @brief ADS1115 16-bit ADC Driver (I2C)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ADS1115 I2C address options
 */
typedef enum {
    ADS1115_ADDR_GND = 0x48,  // ADDR = GND
    ADS1115_ADDR_VCC = 0x49,  // ADDR = VCC
    ADS1115_ADDR_SDA = 0x4A,  // ADDR = SDA
    ADS1115_ADDR_SCL = 0x4B,  // ADDR = SCL
} ads1115_addr_t;

/**
 * @brief ADS1115 input multiplexer configuration
 */
typedef enum {
    ADS1115_MUX_AIN0_AIN1 = 0,  // Differential: AIN0 - AIN1
    ADS1115_MUX_AIN0_AIN3,      // Differential: AIN0 - AIN3
    ADS1115_MUX_AIN1_AIN3,      // Differential: AIN1 - AIN3
    ADS1115_MUX_AIN2_AIN3,      // Differential: AIN2 - AIN3
    ADS1115_MUX_AIN0_GND,       // Single-ended: AIN0 vs GND
    ADS1115_MUX_AIN1_GND,       // Single-ended: AIN1 vs GND
    ADS1115_MUX_AIN2_GND,       // Single-ended: AIN2 vs GND
    ADS1115_MUX_AIN3_GND,       // Single-ended: AIN3 vs GND
} ads1115_mux_t;

/**
 * @brief ADS1115 programmable gain amplifier
 */
typedef enum {
    ADS1115_PGA_6_144V = 0,  // ±6.144V range
    ADS1115_PGA_4_096V,      // ±4.096V range
    ADS1115_PGA_2_048V,      // ±2.048V range (default)
    ADS1115_PGA_1_024V,      // ±1.024V range
    ADS1115_PGA_0_512V,      // ±0.512V range
    ADS1115_PGA_0_256V,      // ±0.256V range
} ads1115_pga_t;

/**
 * @brief ADS1115 data rate (samples per second)
 */
typedef enum {
    ADS1115_DR_8_SPS   = 0,   // 8 samples/sec
    ADS1115_DR_16_SPS  = 1,   // 16 samples/sec
    ADS1115_DR_32_SPS  = 2,   // 32 samples/sec
    ADS1115_DR_64_SPS  = 3,   // 64 samples/sec
    ADS1115_DR_128_SPS = 4,   // 128 samples/sec (default)
    ADS1115_DR_250_SPS = 5,   // 250 samples/sec
    ADS1115_DR_475_SPS = 6,   // 475 samples/sec
    ADS1115_DR_860_SPS = 7,   // 860 samples/sec
} ads1115_dr_t;

/**
 * @brief ADS1115 conversion mode
 */
typedef enum {
    ADS1115_MODE_CONTINUOUS = 0,  // Continuous conversion
    ADS1115_MODE_SINGLE_SHOT,      // Single-shot conversion
} ads1115_mode_t;

/**
 * @brief ADS1115 configuration
 */
typedef struct {
    ads1115_addr_t i2c_addr;
    ads1115_pga_t pga;            // Programmable gain
    ads1115_dr_t data_rate;       // Data rate
    ads1115_mode_t mode;           // Conversion mode
    uint32_t i2c_timeout_ms;      // I2C timeout
} ads1115_config_t;

/**
 * @brief ADS1115 reading result
 */
typedef struct {
    int16_t raw;              // Raw ADC value
    float voltage;            // Voltage in volts
    ads1115_mux_t mux;        // Channel used
    uint32_t timestamp;       // Reading timestamp
    bool valid;               // Data validity
} ads1115_data_t;

/**
 * @brief ADS1115 handle
 */
typedef struct ads1115_handle_s *ads1115_handle_t;

/**
 * @brief Create ADS1115 driver instance
 * @param config Configuration
 * @return Handle or NULL on error
 */
ads1115_handle_t ads1115_create(const ads1115_config_t *config);

/**
 * @brief Delete ADS1115 driver instance
 * @param handle Driver handle
 */
void ads1115_delete(ads1115_handle_t handle);

/**
 * @brief Initialize ADS1115
 * @param handle Driver handle
 * @return 0 on success, negative on error
 */
int ads1115_init(ads1115_handle_t handle);

/**
 * @brief Set configuration register
 * @param handle Driver handle
 * @param mux Input multiplexer
 * @param pga Programmable gain
 * @param mode Conversion mode
 * @param data_rate Data rate
 * @param start_conversion true to start conversion immediately
 * @return 0 on success, negative on error
 */
int ads1115_configure(ads1115_handle_t handle, ads1115_mux_t mux, ads1115_pga_t pga,
                      ads1115_mode_t mode, ads1115_dr_t data_rate, bool start_conversion);

/**
 * @brief Read single conversion
 * @param handle Driver handle
 * @param mux Input multiplexer
 * @param data Output data
 * @return 0 on success, negative on error
 */
int ads1115_read(ads1115_handle_t handle, ads1115_mux_t mux, ads1115_data_t *data);

/**
 * @brief Read single-ended channel (0-3)
 * @param handle Driver handle
 * @param channel Channel number (0-3)
 * @param voltage Output voltage
 * @return 0 on success, negative on error
 */
int ads1115_read_channel(ads1115_handle_t handle, uint8_t channel, float *voltage);

/**
 * @brief Read differential channel
 * @param handle Driver handle
 * @param mux Differential configuration
 * @param voltage Output voltage
 * @return 0 on success, negative on error
 */
int ads1115_read_differential(ads1115_handle_t handle, ads1115_mux_t mux, float *voltage);

/**
 * @brief Check if conversion is complete
 * @param handle Driver handle
 * @param ready Output ready status
 * @return 0 on success, negative on error
 */
int ads1115_is_ready(ads1115_handle_t handle, bool *ready);

/**
 * @brief Start a single-shot conversion
 * @param handle Driver handle
 * @param mux Input multiplexer
 * @return 0 on success, negative on error
 */
int ads1115_start_conversion(ads1115_handle_t handle, ads1115_mux_t mux);

/**
 * @brief Get PGA voltage range
 * @param pga PGA setting
 * @return Voltage range in volts
 */
float ads1115_pga_to_voltage(ads1115_pga_t pga);

#ifdef __cplusplus
}
#endif
