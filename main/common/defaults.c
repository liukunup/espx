/**
 * @file defaults.c
 * @brief Seed the factory-default configuration exactly once
 */

#include <string.h>

#include <nvs.h>
#include <esp_log.h>
#include <cJSON.h>

#include "defaults.h"
#include "app_info.h"
#include "device_manager.h"

static const char *TAG = "defaults";

#define DEFAULTS_NVS_NAMESPACE "espx_node"
#define DEFAULTS_NVS_KEY       "seeded"

bool defaults_seeded(void)
{
    nvs_handle_t nvs;
    if (nvs_open(DEFAULTS_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }

    uint8_t flag = 0;
    esp_err_t err = nvs_get_u8(nvs, DEFAULTS_NVS_KEY, &flag);
    nvs_close(nvs);

    return (err == ESP_OK && flag == 1);
}

static esp_err_t mark_seeded(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(DEFAULTS_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(nvs, DEFAULTS_NVS_KEY, 1);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t defaults_seed_once(void)
{
#ifdef CONFIG_ESPX_DEFAULT_WS2812
    if (defaults_seeded()) {
        ESP_LOGD(TAG, "defaults already seeded, leaving the configuration alone");
        return ESP_OK;
    }

    if (device_get_count() > 0) {
        /* A factory payload or an earlier configuration already defines the node.
         * Seeding on top of it would silently add hardware that may not exist. */
        ESP_LOGI(TAG, "%u device(s) already configured, not adding defaults",
                 (unsigned)device_get_count());
        return mark_seeded();
    }

    cJSON *cfg = cJSON_CreateObject();
    cJSON_AddNumberToObject(cfg, "din", CONFIG_ESPX_DEFAULT_WS2812_GPIO);
    cJSON_AddNumberToObject(cfg, "count", CONFIG_ESPX_DEFAULT_WS2812_COUNT);
    cJSON_AddNumberToObject(cfg, "r", 0);
    cJSON_AddNumberToObject(cfg, "g", 0);
    cJSON_AddNumberToObject(cfg, "b", 0);
    cJSON_AddNumberToObject(cfg, "brightness", 128);

    esp_err_t err = device_add(CONFIG_ESPX_DEFAULT_WS2812_ID, "ws2812", cfg);
    cJSON_Delete(cfg);

    if (err != ESP_OK) {
        /* Do not mark seeded: the next boot should retry rather than leave the
         * node permanently without its default output. */
        ESP_LOGE(TAG, "failed to bind the default WS2812: %s", esp_err_to_name(err));
        return err;
    }

    device_manager_save();
    mark_seeded();

    ESP_LOGI(TAG, "bound default device '%s' (ws2812 on GPIO%d, %d pixel%s)",
             CONFIG_ESPX_DEFAULT_WS2812_ID, CONFIG_ESPX_DEFAULT_WS2812_GPIO,
             CONFIG_ESPX_DEFAULT_WS2812_COUNT,
             CONFIG_ESPX_DEFAULT_WS2812_COUNT == 1 ? "" : "s");
#else
    ESP_LOGD(TAG, "default device seeding disabled");
#endif

    return ESP_OK;
}
