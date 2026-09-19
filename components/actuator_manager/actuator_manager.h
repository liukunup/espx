/**
 * @file actuator_manager.h
 * @brief Actuator Manager component
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Actuator type enumeration
 */
typedef enum {
    ACTUATOR_TYPE_LED = 1,
    ACTUATOR_TYPE_RELAY,
    ACTUATOR_TYPE_MOTOR,
    ACTUATOR_TYPE_SERVO,
    ACTUATOR_TYPE_CUSTOM
} actuator_type_t;

/**
 * @brief Actuator set function pointer
 */
typedef int (*actuator_set_fn)(int value);

/**
 * @brief Actuator configuration
 */
typedef struct {
    char name[32];
    actuator_type_t type;
    actuator_set_fn set_fn;
    int min_value;
    int max_value;
} actuator_config_t;

/**
 * @brief Initialize actuator manager
 *
 * @return 0 on success, negative on error
 */
int actuator_manager_init(void);

/**
 * @brief Register an actuator
 *
 * @param config Actuator configuration
 * @return 0 on success, negative on error
 */
int actuator_register(const actuator_config_t *config);

/**
 * @brief Set actuator value
 *
 * @param name Actuator name
 * @param value Value to set
 * @return 0 on success, negative on error
 */
int actuator_set(const char *name, int value);

/**
 * @brief Get current actuator value
 *
 * @param name Actuator name
 * @param value Output value
 * @return 0 on success, negative on error
 */
int actuator_get(const char *name, int *value);

#ifdef __cplusplus
}
#endif
