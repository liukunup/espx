/**
 * @file cert_manager.c
 * @brief Certificate manager using embedded self-signed certificate
 *
 * The certificate is embedded at build time (espx_cert.h), generated with:
 *   openssl req -x509 -newkey rsa:2048 -keyout key.pem -out cert.pem \
 *     -days 3650 -nodes -subj "/CN=espx.local/O=ESPX/C=CN"
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <esp_log.h>
#include <esp_mac.h>

#include "cert_manager.h"
#include "espx_cert.h"

static const char *TAG = "cert_manager";

static bool g_initialized = false;
static char g_device_san[64] = {0};

esp_err_t cert_manager_init(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(g_device_san, sizeof(g_device_san), "espx-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    g_initialized = true;
    ESP_LOGI(TAG, "Cert manager initialized, device: %s", g_device_san);
    ESP_LOGI(TAG, "Using embedded self-signed certificate");
    return ESP_OK;
}

esp_err_t cert_manager_get_server_cert(server_cert_t *cert)
{
    if (!g_initialized || cert == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    cert->cert_pem = strdup(ESPX_SERVER_CERT_PEM);
    cert->key_pem = strdup(ESPX_SERVER_KEY_PEM);

    if (cert->cert_pem == NULL || cert->key_pem == NULL) {
        cert_manager_free_cert(cert);
        return ESP_ERR_NO_MEM;
    }

    cert->cert_len = strlen(cert->cert_pem);
    cert->key_len = strlen(cert->key_pem);

    return ESP_OK;
}

esp_err_t cert_manager_regenerate(void)
{
    ESP_LOGW(TAG, "Regenerate not supported with embedded certificate");
    return ESP_ERR_NOT_SUPPORTED;
}

bool cert_manager_is_valid(void)
{
    return g_initialized;
}

esp_err_t cert_manager_get_info(char *info, size_t max_len)
{
    snprintf(info, max_len,
             "{\"valid\":%s,\"device\":\"%s\",\"algorithm\":\"RSA-2048\",\"source\":\"embedded\"}",
             g_initialized ? "true" : "false", g_device_san);
    return ESP_OK;
}

void cert_manager_free_cert(server_cert_t *cert)
{
    if (cert == NULL) return;

    if (cert->cert_pem) {
        free(cert->cert_pem);
        cert->cert_pem = NULL;
    }
    if (cert->key_pem) {
        free(cert->key_pem);
        cert->key_pem = NULL;
    }
    cert->cert_len = 0;
    cert->key_len = 0;
}
