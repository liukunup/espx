/**
 * @file json_utils.c
 * @brief Implementation of the cJSON helpers (see json_utils.h)
 */

#include "json_utils.h"

const char *json_get_string(const cJSON *obj, const char *key, const char *def)
{
    if (obj == NULL || key == NULL) {
        return def;
    }

    const cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        return item->valuestring;
    }
    return def;
}

int json_get_int(const cJSON *obj, const char *key, int def)
{
    if (obj == NULL || key == NULL) {
        return def;
    }

    const cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    return def;
}

bool json_get_bool(const cJSON *obj, const char *key, bool def)
{
    if (obj == NULL || key == NULL) {
        return def;
    }

    const cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return def;
}

void json_set_string(cJSON *obj, const char *key, const char *value)
{
    if (obj == NULL || key == NULL) {
        return;
    }

    /* cJSON_ReplaceItemInObject() ignores a key that does not exist yet, which
     * silently drops first-time settings. Delete, then add. */
    cJSON_DeleteItemFromObject(obj, key);
    if (value != NULL) {
        cJSON_AddStringToObject(obj, key, value);
    }
}

void json_set_number(cJSON *obj, const char *key, double value)
{
    if (obj == NULL || key == NULL) {
        return;
    }

    cJSON_DeleteItemFromObject(obj, key);
    cJSON_AddNumberToObject(obj, key, value);
}

void json_set_bool(cJSON *obj, const char *key, bool value)
{
    if (obj == NULL || key == NULL) {
        return;
    }

    cJSON_DeleteItemFromObject(obj, key);
    cJSON_AddBoolToObject(obj, key, value);
}
