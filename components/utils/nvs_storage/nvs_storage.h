/**
 * @file nvs_storage.h
 * @brief NVS Storage component with encryption support
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief NVS namespace for device configuration */
#define NVS_NAMESPACE_CONFIG "config"

/** @brief NVS namespace for system data */
#define NVS_NAMESPACE_SYSTEM "system"

/**
 * @brief Initialize NVS storage
 *
 * @return 0 on success, negative on error
 */
int nvs_init(void);

/**
 * @brief Set a string value
 *
 * @param key Key name
 * @param value String value
 * @return 0 on success, negative on error
 */
int nvs_set_string(const char *key, const char *value);

/**
 * @brief Get a string value
 *
 * @param key Key name
 * @param value Output buffer
 * @param len Buffer length
 * @return 0 on success, negative on error
 */
int nvs_get_string(const char *key, char *value, size_t len);

/**
 * @brief Set an integer value
 *
 * @param key Key name
 * @param value Integer value
 * @return 0 on success, negative on error
 */
int nvs_set_int(const char *key, int value);

/**
 * @brief Get an integer value
 *
 * @param key Key name
 * @param value Output pointer
 * @return 0 on success, negative on error
 */
int nvs_get_int(const char *key, int *value);

/**
 * @brief Set a blob (binary data)
 *
 * @param key Key name
 * @param value Data pointer
 * @param len Data length
 * @return 0 on success, negative on error
 */
int nvs_set_blob(const char *key, const void *value, size_t len);

/**
 * @brief Get a blob (binary data)
 *
 * @param key Key name
 * @param value Output buffer
 * @param len Buffer length (input: max size, output: actual size)
 * @return 0 on success, negative on error
 */
int nvs_get_blob(const char *key, void *value, size_t *len);

/**
 * @brief Set encrypted value (for sensitive data)
 *
 * @param key Key name
 * @param value Data pointer
 * @param len Data length
 * @return 0 on success, negative on error
 */
int nvs_set_encrypted(const char *key, const void *value, size_t len);

/**
 * @brief Get decrypted value
 *
 * @param key Key name
 * @param value Output buffer
 * @param len Buffer length (input: max size, output: actual size)
 * @return 0 on success, negative on error
 */
int nvs_get_decrypted(const char *key, void *value, size_t *len);

/**
 * @brief Erase a key
 *
 * @param key Key name
 * @return 0 on success, negative on error
 */
int nvs_erase(const char *key);

/**
 * @brief Erase all keys in namespace
 *
 * @return 0 on success, negative on error
 */
int nvs_erase_all(void);

/**
 * @brief Commit changes to flash
 *
 * @return 0 on success, negative on error
 */
int nvs_commit(void);

#ifdef __cplusplus
}
#endif
