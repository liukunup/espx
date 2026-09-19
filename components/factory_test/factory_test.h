/**
 * @file factory_test.h
 * @brief Factory Test component
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test type enumeration
 */
typedef enum {
    TEST_WIFI = 1,
    TEST_MQTT,
    TEST_FLASH,
    TEST_SENSOR,
    TEST_ACTUATOR,
    TEST_LED,
    TEST_FULL
} test_type_t;

/**
 * @brief Test result structure
 */
typedef struct {
    test_type_t type;
    bool passed;
    char message[256];
    int duration_ms;
} test_result_t;

/**
 * @brief Initialize factory test module
 *
 * @return 0 on success, negative on error
 */
int factory_test_init(void);

/**
 * @brief Run a specific test
 *
 * @param type Test type
 * @param result Test result output
 * @return 0 on success, negative on error
 */
int factory_test_run(test_type_t type, test_result_t *result);

/**
 * @brief Run all tests
 *
 * @param results Results array
 * @param count Number of results (input: max, output: actual)
 * @return 0 on success, negative on error
 */
int factory_test_run_all(test_result_t *results, int *count);

/**
 * @brief Perform sensor calibration
 *
 * @param params Calibration parameters
 * @param result Result output
 * @param len Buffer length
 * @return 0 on success, negative on error
 */
int factory_calibrate(const char *params, char *result, size_t len);

/**
 * @brief Generate unique device ID
 *
 * @param buffer Output buffer
 * @param len Buffer length
 * @return 0 on success, negative on error
 */
int factory_generate_device_id(char *buffer, size_t len);

#ifdef __cplusplus
}
#endif
