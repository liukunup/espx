/**
 * @file config_apply.h
 * @brief Apply a configuration document (YAML or JSON) to the node
 *
 * One code path is shared by the MQTT YAML interface and the HTTPS API, so a
 * configuration pushed either way has identical semantics.
 *
 * Document schema
 * ---------------
 *   node:                    # merged into node_config.node
 *     device_id: espx-0001
 *     name: Line-1
 *
 *   network:                 # merged into node_config.network
 *     wifi_ssid: PlantNet           # pre-provisioned Wi-Fi (optional)
 *     wifi_password: secret
 *     mqtt_broker: mqtt://host:1883
 *     mqtt_username: user
 *     mqtt_password: pass
 *     mqtt_topic_prefix: plant/line1
 *
 *   devices:                 # upserted by id
 *     - id: temp_in
 *       type: dht11
 *       enabled: true
 *       config: {gpio: 4, interval_ms: 5000}
 *
 *   remove_devices: [old1, old2]   # ids to delete
 *   replace_devices: true          # also delete devices not listed above
 *
 * Everything except `remove_devices` and `replace_devices` is optional; only the
 * sections present in the document are touched. `replace_devices` defaults to
 * false, so a partial push can never silently unbind hardware.
 */

#ifndef CONFIG_APPLY_H
#define CONFIG_APPLY_H

#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cJSON;
typedef struct cJSON cJSON;

/**
 * @brief Result of applying a configuration document
 */
typedef struct {
    int devices_added;
    int devices_updated;
    int devices_removed;
    int devices_failed;
    bool node_changed;
    bool network_changed;
    bool reboot_recommended;   /**< network/MQTT changed: reboot to apply */
    char error[128];           /**< first failure, empty when none */
} config_apply_result_t;

/**
 * @brief Parse a configuration document (YAML or JSON) into cJSON
 *
 * @param payload  Text payload
 * @param err_line Output: 1-based line for a YAML error (0 when not applicable)
 * @return Parsed document, or NULL on error
 */
cJSON *config_parse_document(const char *payload, int *err_line);

/**
 * @brief Apply a configuration document
 *
 * @param doc     Document from config_parse_document()
 * @param result  Output summary (optional)
 * @return ESP_OK when the document was valid and applied (individual device
 *         failures are reported in result, not as a top-level error)
 */
esp_err_t config_apply(const cJSON *doc, config_apply_result_t *result);

/**
 * @brief Apply a raw payload (YAML or JSON) in one step
 *
 * @param payload Text payload
 * @param result  Output summary (optional)
 * @param err_out Output: human readable error (optional, points to static text
 *                or into a caller-provided buffer; see err_buf)
 * @return ESP_OK on success
 */
esp_err_t config_apply_payload(const char *payload, config_apply_result_t *result,
                               char *err_buf, size_t err_buf_len);

/**
 * @brief Render the current node configuration as JSON
 */
cJSON *config_export(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_APPLY_H */
