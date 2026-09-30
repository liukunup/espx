/**
 * @file mdns_service.c
 * @brief mDNS / DNS-SD advertisement implementation
 */

#include <stdio.h>
#include <string.h>

#include <esp_log.h>
#include <esp_mac.h>
#include <mdns.h>

#include "app_info.h"
#include "mdns_service.h"
#include "node_config.h"

static const char *TAG = "mdns";

static bool s_running = false;
static char s_hostname[40] = {0};
static char s_fqdn[48] = {0};

/**
 * @brief Build the mDNS hostname from the device id
 *
 * The name is the device id itself, sanitised into a valid DNS label:
 * lower case (mDNS labels are case-insensitive and conventionally lower case),
 * keeping only [a-z0-9-]. So "espx-84C7BB772E74" becomes
 * "espx-84c7bb772e74".
 *
 * Deliberately NOT "<configured prefix><device_id>": the device id already
 * carries the product prefix, and prepending another one produced
 * "espx-espx-84c7bb772e74.local". Keeping the mDNS name equal to the device id
 * also makes it match the MQTT topic prefix, so there is one identity to
 * remember rather than three.
 *
 * If the stored device_id lacks the "espx-" prefix (e.g. legacy config),
 * it is added automatically so mDNS always shows "espx-<mac>.local".
 */
static void build_hostname(void)
{
    const char *device_id = node_config_get_device_id();
    const char *prefix = "espx-";
    size_t prefix_len = 5;

    /* Ensure the device_id always has the "espx-" prefix for mDNS */
    if (strncmp(device_id, prefix, prefix_len) != 0) {
        /* Prefix is missing: add it */
        snprintf(s_hostname, sizeof(s_hostname), "%s%s", prefix, device_id);
    } else {
        strncpy(s_hostname, device_id, sizeof(s_hostname) - 1);
        s_hostname[sizeof(s_hostname) - 1] = '\0';
    }

    /* Sanitize to valid DNS label: lowercase, keep only [a-z0-9-] */
    size_t o = 0;
    for (const char *p = s_hostname; *p && o < sizeof(s_hostname) - 1; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') {
            s_hostname[o++] = c;
        }
    }

    /* A device id of only invalid characters would leave an empty label. */
    if (o == 0) {
        snprintf(s_hostname, sizeof(s_hostname), "espx-node");
    } else {
        s_hostname[o] = '\0';
    }

    snprintf(s_fqdn, sizeof(s_fqdn), "%s.local", s_hostname);
}

esp_err_t mdns_service_start(void)
{
    if (s_running) {
        return ESP_OK;
    }

    build_hostname();

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = mdns_hostname_set(s_hostname);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_hostname_set(%s) failed: %s", s_hostname, esp_err_to_name(err));
        return err;
    }

    err = mdns_instance_name_set(CONFIG_DEVICE_NAME);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mdns_instance_name_set failed: %s", esp_err_to_name(err));
    }

    /* Identity a client needs before it can call the API. */
    mdns_txt_item_t txt[] = {
        { "id",      (char *)node_config_get_device_id() },
        { "model",   "ESPX" },
        { "version", app_version() },
        { "vendor",  "espressif" },
    };

    /* Advertise the services this node actually offers. */
    err = mdns_service_add(NULL, "_https", "_tcp", CONFIG_ESPX_HTTPS_PORT,
                            txt, sizeof(txt) / sizeof(txt[0]));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "advertising _https._tcp failed: %s", esp_err_to_name(err));
    }

#ifdef CONFIG_ESPX_MDNS_ADVERTISE_MQTT
    err = mdns_service_add(NULL, "_mqtt", "_tcp", 1883, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "advertising _mqtt._tcp failed: %s", esp_err_to_name(err));
    }
#endif

    s_running = true;
    ESP_LOGI(TAG, "mDNS started: %s  (https://%s/)", s_fqdn, s_fqdn);
    return ESP_OK;
}

esp_err_t mdns_service_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }
    mdns_free();
    s_running = false;
    ESP_LOGI(TAG, "mDNS stopped");
    return ESP_OK;
}

bool mdns_service_is_running(void)
{
    return s_running;
}

const char* mdns_service_hostname(void)
{
    return s_hostname;
}

const char* mdns_service_fqdn(void)
{
    return s_fqdn;
}
