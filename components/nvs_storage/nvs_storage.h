/**
 * @file nvs_storage.h
 * @brief NVS Storage wrapper API
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Initialize NVS storage
 * @return 0 on success, negative on error
 */
int storage_init(void);

/**
 * @brief Set a string value
 */
int storage_set_string(const char *key, const char *value);

/**
 * @brief Get a string value
 */
int storage_get_string(const char *key, char *value, size_t len);

/**
 * @brief Set an integer value
 */
int storage_set_int(const char *key, int value);

/**
 * @brief Get an integer value
 */
int storage_get_int(const char *key, int *value);

/**
 * @brief Set a blob value
 */
int storage_set_blob(const char *key, const void *value, size_t len);

/**
 * @brief Get a blob value
 */
int storage_get_blob(const char *key, void *value, size_t *len);

/**
 * @brief Set encrypted blob (uses flash encryption)
 */
int storage_set_encrypted(const char *key, const void *value, size_t len);

/**
 * @brief Get decrypted blob
 */
int storage_get_decrypted(const char *key, void *value, size_t *len);

/**
 * @brief Erase a key
 */
int storage_erase(const char *key);

/**
 * @brief Erase all keys in namespace
 */
int storage_erase_all(void);

/**
 * @brief Commit changes
 */
int storage_commit(void);
