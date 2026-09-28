/**
 * @file nvs_utils.c
 * @brief Implementation of the NVS helpers (see nvs_utils.h)
 */

#include <stdlib.h>

#include <nvs.h>
#include <esp_log.h>
#include <cJSON.h>

#include "nvs_utils.h"

static const char *const TAG = "nvs_utils";

esp_err_t nvs_load_alloc(const char *ns, const char *key, char **out)
{
    if (ns == NULL || key == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        /* ESP_ERR_NVS_NOT_FOUND simply means "never written" */
        return err;
    }

    size_t len = 0;
    err = nvs_get_str(nvs, key, NULL, &len);
    if (err != ESP_OK || len == 0) {
        nvs_close(nvs);
        return (err == ESP_OK) ? ESP_ERR_NVS_NOT_FOUND : err;
    }

    char *buf = malloc(len);
    if (buf == NULL) {
        nvs_close(nvs);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_str(nvs, key, buf, &len);
    nvs_close(nvs);
    if (err != ESP_OK) {
        free(buf);
        return err;
    }

    *out = buf;
    return ESP_OK;
}

esp_err_t nvs_load_json(const char *ns, const char *key, cJSON **out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = NULL;

    char *str = NULL;
    esp_err_t err = nvs_load_alloc(ns, key, &str);
    if (err != ESP_OK) {
        return err;
    }

    cJSON *json = cJSON_Parse(str);
    free(str);
    if (json == NULL) {
        ESP_LOGE(TAG, "'%s/%s' holds invalid JSON", ns, key);
        return ESP_ERR_INVALID_STATE;
    }

    *out = json;
    return ESP_OK;
}

esp_err_t nvs_save_json(const char *ns, const char *key, const cJSON *obj)
{
    if (ns == NULL || key == NULL || obj == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    char *str = cJSON_PrintUnformatted((cJSON *)obj);
    if (str == NULL) {
        return ESP_ERR_NO_MEM;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(ns, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        free(str);
        return err;
    }

    err = nvs_set_str(nvs, key, str);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    free(str);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to store '%s/%s': %s", ns, key, esp_err_to_name(err));
    }
    return err;
}
