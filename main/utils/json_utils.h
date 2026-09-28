/**
 * @file json_utils.h
 * @brief Small, type-checked helpers over cJSON
 *
 * cJSON's accessors are not type-safe: cJSON_GetObjectItem() returns an item
 * whatever its type, and reading ->valuestring off a number is a NULL
 * dereference. These helpers collapse the repeated
 * "cJSON_IsString(x) ? x->valuestring : default" pattern into one call and
 * always return a usable value.
 */

#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <stdbool.h>
#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Read a string member
 *
 * @param obj  Object to read from (NULL yields @p def)
 * @param key  Member name
 * @param def  Value returned when the member is absent or not a string
 * @return The string value, or @p def. Never NULL unless @p def is NULL.
 */
const char *json_get_string(const cJSON *obj, const char *key, const char *def);

/**
 * @brief Read a numeric member
 *
 * @return The integer value, or @p def when absent or not a number
 */
int json_get_int(const cJSON *obj, const char *key, int def);

/**
 * @brief Read a boolean member
 *
 * @return The boolean value, or @p def when absent or not a bool
 */
bool json_get_bool(const cJSON *obj, const char *key, bool def);

/**
 * @brief Set a string member, replacing any existing value
 *
 * @param value New value. Passing NULL REMOVES the member (the Web UI relies
 *              on this to clear a stored Wi-Fi password).
 */
void json_set_string(cJSON *obj, const char *key, const char *value);

/**
 * @brief Set a numeric member, replacing any existing value
 */
void json_set_number(cJSON *obj, const char *key, double value);

/**
 * @brief Set a boolean member, replacing any existing value
 */
void json_set_bool(cJSON *obj, const char *key, bool value);

#ifdef __cplusplus
}
#endif

#endif /* JSON_UTILS_H */
