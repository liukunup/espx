/**
 * @file mfg_provision.h
 * @brief Manufacturing provisioning
 *
 * Reads factory-preset configuration from the "mfg_data" NVS partition and
 * applies it on first boot, then clears it.
 *
 * Factory data format (JSON string stored in NVS namespace "factory", key "config"):
 *
 * {
 *   "node": {
 *     "device_id": "espx-0001",
 *     "name": "Line-1 Sensor"
 *   },
 *   "network": {
 *     "mqtt_broker": "mqtt://broker.example.com:1883",
 *     "mqtt_username": "user",
 *     "mqtt_password": "pass",
 *     "mqtt_topic_prefix": "plant/line1"
 *   },
 *   "devices": [
 *     { "id": "temp1", "type": "dht11", "enabled": true,
 *       "config": { "gpio": 4, "interval_ms": 5000 } }
 *   ]
 * }
 */

#ifndef MFG_PROVISION_H
#define MFG_PROVISION_H

#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/** NVS partition holding factory data (see partitions.csv) */
#define MFG_PARTITION_NAME  "mfg_data"
#define MFG_NVS_NAMESPACE   "factory"
#define MFG_NVS_KEY         "config"

/**
 * @brief Initialize the manufacturing data partition
 *
 * Safe to call when the partition does not exist.
 */
esp_err_t mfg_provision_init(void);

/**
 * @brief Check whether factory data is present
 */
bool mfg_provision_has_data(void);

/**
 * @brief Load factory data and apply it
 *
 * Applies node identity, network settings and device bindings, then clears
 * the factory data so it runs only once.
 *
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if no data
 */
esp_err_t mfg_provision_load(void);

/**
 * @brief Write factory data (used by production tooling / test console)
 *
 * @param json JSON string with the factory payload
 */
esp_err_t mfg_provision_write(const char *json);

/**
 * @brief Erase factory data
 */
esp_err_t mfg_provision_clear(void);

#ifdef __cplusplus
}
#endif

#endif // MFG_PROVISION_H
