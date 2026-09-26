/**
 * @file cert_manager.c
 * @brief Certificate manager
 *
 * The server certificate and private key are plain PEM files in
 *   main/cert_manager/certs/server.crt
 *   main/cert_manager/certs/server.key
 * and are embedded into the firmware at build time by CMake EMBED_FILES
 * (see main/CMakeLists.txt). Regenerate with: tools/gen_certs.sh
 *
 * Replacing the certificate therefore requires editing files, not C source.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <esp_log.h>
#include <esp_mac.h>

#include "cert_manager.h"

static const char *TAG = "cert_manager";

/* Embedded at build time (EMBED_FILES adds a trailing NUL) */
extern const uint8_t server_crt_start[] asm("_binary_server_crt_start");
extern const uint8_t server_crt_end[]   asm("_binary_server_crt_end");
extern const uint8_t server_key_start[] asm("_binary_server_key_start");
extern const uint8_t server_key_end[]   asm("_binary_server_key_end");

static bool g_initialized = false;
static char g_device_san[64] = {0};
static size_t g_cert_len = 0;
static size_t g_key_len = 0;

esp_err_t cert_manager_init(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(g_device_san, sizeof(g_device_san), "espx-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* EMBED_FILES appends a NUL; exclude it from the length. */
    g_cert_len = (size_t)(server_crt_end - server_crt_start);
    g_key_len  = (size_t)(server_key_end - server_key_start);
    if (g_cert_len > 0 && server_crt_start[g_cert_len - 1] == '\0') g_cert_len--;
    if (g_key_len  > 0 && server_key_start[g_key_len - 1]  == '\0') g_key_len--;

    if (g_cert_len == 0 || g_key_len == 0) {
        ESP_LOGE(TAG, "Embedded certificate or key is empty");
        return ESP_ERR_INVALID_STATE;
    }

    /* Sanity check: PEM markers present */
    if (strstr((const char *)server_crt_start, "BEGIN CERTIFICATE") == NULL) {
        ESP_LOGE(TAG, "server.crt does not look like a PEM certificate");
        return ESP_ERR_INVALID_STATE;
    }
    if (strstr((const char *)server_key_start, "PRIVATE KEY") == NULL) {
        ESP_LOGE(TAG, "server.key does not look like a PEM private key");
        return ESP_ERR_INVALID_STATE;
    }

    g_initialized = true;

    ESP_LOGI(TAG, "Certificate loaded from build-time files (%u / %u bytes)",
             (unsigned)g_cert_len, (unsigned)g_key_len);
    ESP_LOGI(TAG, "Device: %s (certificate CN is independent of this)", g_device_san);
    return ESP_OK;
}

esp_err_t cert_manager_get_server_cert(server_cert_t *cert)
{
    if (!g_initialized || cert == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    /* Copy so the caller can free uniformly; PEM data needs a NUL terminator. */
    cert->cert_pem = malloc(g_cert_len + 1);
    cert->key_pem  = malloc(g_key_len + 1);
    if (cert->cert_pem == NULL || cert->key_pem == NULL) {
        cert_manager_free_cert(cert);
        return ESP_ERR_NO_MEM;
    }

    memcpy(cert->cert_pem, server_crt_start, g_cert_len);
    cert->cert_pem[g_cert_len] = '\0';
    memcpy(cert->key_pem, server_key_start, g_key_len);
    cert->key_pem[g_key_len] = '\0';

    /* The reported length MUST include the terminating NUL.
     *
     * mbedtls_x509_crt_parse() documents: "buflen: The size of buf, including
     * the terminating NULL byte in case of PEM encoded data." esp-tls passes
     * this length straight through, so excluding the NUL makes every TLS
     * handshake fail with ESP_ERR_MBEDTLS_X509_CRT_PARSE_FAILED and the server
     * resets each connection. */
    cert->cert_len = g_cert_len + 1;
    cert->key_len  = g_key_len + 1;

    return ESP_OK;
}

esp_err_t cert_manager_regenerate(void)
{
    ESP_LOGW(TAG, "Certificate is baked into the firmware at build time;");
    ESP_LOGW(TAG, "run tools/gen_certs.sh and rebuild the project instead.");
    return ESP_ERR_NOT_SUPPORTED;
}

bool cert_manager_is_valid(void)
{
    return g_initialized;
}

esp_err_t cert_manager_get_info(char *info, size_t max_len)
{
    if (info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(info, max_len,
             "{\"valid\":%s,\"device\":\"%s\",\"algorithm\":\"RSA-2048\","
             "\"source\":\"build-time PEM files\","
             "\"cert_bytes\":%u,\"key_bytes\":%u}",
             g_initialized ? "true" : "false", g_device_san,
             (unsigned)g_cert_len, (unsigned)g_key_len);

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
