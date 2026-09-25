/**
 * @file sensor_manager.h
 * @brief Sensor Manager component
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sensor type enumeration
 */
typedef enum {
    SENSOR_TYPE_TEMPERATURE = 1,
    SENSOR_TYPE_HUMIDITY,
    SENSOR_TYPE_PRESSURE,
    SENSOR_TYPE_LIGHT,
    SENSOR_TYPE_DOOR,
    SENSOR_TYPE_MOTION,
    SENSOR_TYPE_CUSTOM
} sensor_type_t;

/**
 * @brief Sensor data structure
 */
typedef struct {
    char name[32];
    sensor_type_t type;
    float value;
    char unit[8];
    uint32_t last_read;
    bool valid;
} sensor_data_t;

/**
 * @brief Sensor read function pointer
 */
typedef int (*sensor_read_fn)(float *value);

/**
 * @brief Sensor configuration
 */
typedef struct {
    char name[32];
    sensor_type_t type;
    sensor_read_fn read_fn;
    char unit[8];
    float calibration_offset;
    float calibration_scale;
} sensor_config_t;

/**
 * @brief Initialize sensor manager
 *
 * @return 0 on success, negative on error
 */
int sensor_manager_init(void);

/**
 * @brief Register a sensor
 *
 * @param config Sensor configuration
 * @return 0 on success, negative on error
 */
int sensor_register(const sensor_config_t *config);

/**
 * @brief Read a sensor value
 *
 * @param name Sensor name
 * @param value Output value
 * @return 0 on success, negative on error
 */
int sensor_read(const char *name, float *value);

/**
 * @brief Read all registered sensors
 *
 * @param data Data array output
 * @param max_count Maximum sensors to read
 * @param actual_count Actual number read
 * @return 0 on success, negative on error
 */
int sensor_read_all(sensor_data_t *data, int max_count, int *actual_count);

/**
 * @brief Get sensor configuration
 *
 * @param name Sensor name
 * @param config Output configuration
 * @return 0 on success, negative on error
 */
int sensor_get_config(const char *name, sensor_config_t *config);

/**
 * @brief Set sensor calibration
 *
 * @param name Sensor name
 * @param offset Calibration offset
 * @param scale Calibration scale
 * @return 0 on success, negative on error
 */
int sensor_set_calibration(const char *name, float offset, float scale);

#ifdef __cplusplus
}
#endif
