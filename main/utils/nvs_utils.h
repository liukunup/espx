/**
 * @file nvs_utils.h
 * @brief NVS helpers for the "read a string blob" pattern
 *
 * Reading a string from NVS takes two calls to nvs_get_str() (one to learn the
 * length, one to fetch), a malloc, and an open/close pair. That sequence is
 * repeated for the node config, the device list and the factory payload.
 * These helpers state it once.
 */

#ifndef NVS_UTILS_H
#define NVS_UTILS_H

#include <esp_err.h>
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read a string value from NVS, allocating exactly the needed size
 *
 * @param ns   Namespace
 * @param key  Key
 * @param out  Receives a malloc'd, NUL-terminated string. Caller frees with
 *             free(). Set to NULL on failure.
 * @return ESP_OK, ESP_ERR_NVS_NOT_FOUND (namespace or key absent),
 *         ESP_ERR_NO_MEM, or another nvs error
 */
esp_err_t nvs_load_alloc(const char *ns, const char *key, char **out);

/**
 * @brief Read and parse a JSON document stored in NVS
 *
 * @param out  Receives a cJSON tree. Caller owns it (cJSON_Delete). Set to
 *             NULL on failure.
 * @return ESP_OK, ESP_ERR_NVS_NOT_FOUND, ESP_ERR_INVALID_STATE when the
 *         stored text is not valid JSON, or an allocation/NVS error
 */
esp_err_t nvs_load_json(const char *ns, const char *key, cJSON **out);

/**
 * @brief Serialise a cJSON tree to unformatted text and store it in NVS
 *
 * Writes and commits in one call.
 *
 * @return ESP_OK, ESP_ERR_NO_MEM, or an NVS error
 */
esp_err_t nvs_save_json(const char *ns, const char *key, const cJSON *obj);

#ifdef __cplusplus
}
#endif

#endif /* NVS_UTILS_H */
