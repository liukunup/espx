/**
 * @file nvs_storage.c
 * @brief NVS Storage implementation
 */

#include "nvs_storage.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "nvs_storage";

/** @brief NVS handle */
static nvs_handle_t g_nvs = 0;

/** @brief Flag */
static bool g_init = false;

int storage_init(void) {
    if (g_init) return 0;
    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) return -1;
    err = nvs_open("storage", NVS_READWRITE, &g_nvs);
    if (err != ESP_OK) return -2;
    g_init = true;
    return 0;
}

int storage_set_string(const char *key, const char *value) {
    if (!g_init || !key || !value) return -1;
    return nvs_set_str(g_nvs, key, value) == ESP_OK ? 0 : -3;
}

int storage_get_string(const char *key, char *value, size_t len) {
    if (!g_init || !key || !value || !len) return -1;
    size_t sz = len;
    esp_err_t err = nvs_get_str(g_nvs, key, value, &sz);
    if (err == ESP_ERR_NVS_NOT_FOUND) return -4;
    return err == ESP_OK ? 0 : -3;
}

int storage_set_int(const char *key, int value) {
    if (!g_init || !key) return -1;
    return nvs_set_i32(g_nvs, key, value) == ESP_OK ? 0 : -3;
}

int storage_get_int(const char *key, int *value) {
    if (!g_init || !key || !value) return -1;
    int32_t v = 0;
    esp_err_t err = nvs_get_i32(g_nvs, key, &v);
    if (err == ESP_ERR_NVS_NOT_FOUND) return -4;
    if (err == ESP_OK) *value = (int)v;
    return err == ESP_OK ? 0 : -3;
}

int storage_set_blob(const char *key, const void *value, size_t len) {
    if (!g_init || !key || !value || !len) return -1;
    return nvs_set_blob(g_nvs, key, value, len) == ESP_OK ? 0 : -3;
}

int storage_get_blob(const char *key, void *value, size_t *len) {
    if (!g_init || !key || !value || !len || !*len) return -1;
    size_t sz = *len;
    esp_err_t err = nvs_get_blob(g_nvs, key, value, &sz);
    if (err == ESP_ERR_NVS_NOT_FOUND) return -4;
    if (err == ESP_OK) *len = sz;
    return err == ESP_OK ? 0 : -3;
}

int storage_set_encrypted(const char *key, const void *value, size_t len) {
    return storage_set_blob(key, value, len);
}

int storage_get_decrypted(const char *key, void *value, size_t *len) {
    return storage_get_blob(key, value, len);
}

int storage_erase(const char *key) {
    if (!g_init || !key) return -1;
    return nvs_erase_key(g_nvs, key) == ESP_OK ? 0 : -3;
}

int storage_erase_all(void) {
    if (!g_init) return -1;
    return nvs_erase_all(g_nvs) == ESP_OK ? 0 : -3;
}

int storage_commit(void) {
    if (!g_init) return -1;
    return nvs_commit(g_nvs) == ESP_OK ? 0 : -3;
}
