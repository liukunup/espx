/**
 * @file node_config.h
 * @brief Node configuration (top-level)
 */

#ifndef NODE_CONFIG_H
#define NODE_CONFIG_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cJSON;
typedef struct cJSON cJSON;

/**
 * @brief Initialize node config
 */
esp_err_t node_config_init(void);

/**
 * @brief Load config from NVS
 */
esp_err_t node_config_load(void);

/**
 * @brief Save config to NVS
 */
esp_err_t node_config_save(void);

/**
 * @brief Get full config as JSON
 */
cJSON* node_config_get(void);

/**
 * @brief Set config from JSON
 */
esp_err_t node_config_set(const cJSON *config);

/**
 * @brief Get device id (MAC-based if not set)
 */
const char* node_config_get_device_id(void);

/**
 * @brief Get node name
 */
const char* node_config_get_name(void);

#ifdef __cplusplus
}
#endif

#endif // NODE_CONFIG_H
