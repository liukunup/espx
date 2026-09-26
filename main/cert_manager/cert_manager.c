/**
 * @file cert_manager.c
 * @brief Certificate manager implementation
 *
 * Uses mbedTLS to generate self-signed RSA certificates.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <esp_log.h>
#include <mbedtls/x509write.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

#include "cert_manager.h"
#include "param_store/param_store.h"

static const char *TAG = "cert_manager";

// NVS namespace for certificates
#define CERT_NVS_NAMESPACE "espx_certs"
#define CERT_KEY_CERT "server_cert"
#define CERT_KEY_KEY "server_key"
#define CERT_KEY_VALID "valid"

// Certificate parameters
#define CERT_RSA_BITS 2048
#define CERT_VALID_DAYS 3650  // 10 years
#define CERT_ORG "ESPX"
#define CERT_ORG_UNIT "IoT"
#define CERT_COUNTRY "CN"

static bool g_cert_valid = false;
static char g_device_san[64] = {0};

/**
 * @brief Generate random bytes using mbedTLS
 */
static int generate_random(void *ctx, unsigned char *output, size_t output_len)
{
    mbedtls_ctr_drbg_context *ctr_drbg = (mbedtls_ctr_drbg_context *)ctx;
    return mbedtls_ctr_drbg_random(ctr_drbg, output, output_len);
}

/**
 * @brief Generate self-signed certificate using mbedTLS
 */
static esp_err_t generate_self_signed_cert(char **cert_pem, char **key_pem)
{
    int ret;
    char buf[4096];
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    mbedtls_mpi serial;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    const char *pers = "esp_x509_crt";

    // Seed random number generator
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                 (const unsigned char *)pers, strlen(pers));
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed failed: -0x%04x", -ret);
        goto cleanup;
    }

    // Generate RSA key
    mbedtls_pk_init(&key);
    ret = mbedtls_pk_setup(&key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_pk_setup failed: -0x%04x", -ret);
        goto cleanup;
    }

    ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(key), mbedtls_ctr_drbg_random, &ctr_drbg,
                                CERT_RSA_BITS, 65537);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_rsa_gen_key failed: -0x%04x", -ret);
        goto cleanup;
    }

    // Create certificate
    mbedtls_x509write_crt_init(&crt);
    mbedtls_x509write_crt_set_md_alg(&crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_version(&crt, 1);
    mbedtls_x509write_crt_set_validity_range(&crt, 0, CERT_VALID_DAYS);

    // Set subject and issuer
    char subject[256];
    snprintf(subject, sizeof(subject), "CN=%s, O=%s, OU=%s, C=%s",
             g_device_san, CERT_ORG, CERT_ORG_UNIT, CERT_COUNTRY);

    ret = mbedtls_x509write_crt_set_subject_name(&crt, subject);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_x509write_crt_set_subject_name failed: -0x%04x", -ret);
        goto cleanup;
    }

    ret = mbedtls_x509write_crt_set_issuer_name(&crt, subject);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_x509write_crt_set_issuer_name failed: -0x%04x", -ret);
        goto cleanup;
    }

    // Set RSA key
    ret = mbedtls_x509write_crt_set_subject_key(&crt, &key);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_x509write_crt_set_subject_key failed: -0x%04x", -ret);
        goto cleanup;
    }

    ret = mbedtls_x509write_crt_set_issuer_key(&crt, &key);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_x509write_crt_set_issuer_key failed: -0x%04x", -ret);
        goto cleanup;
    }

    // Set serial number
    mbedtls_mpi_init(&serial);
    ret = mbedtls_mpi_fill_random(&serial, 16, mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_mpi_fill_random failed: -0x%04x", -ret);
        goto cleanup;
    }
    mbedtls_x509write_crt_set_serial(&crt, &serial);

    // Write certificate to PEM
    ret = mbedtls_pem_write_buffer("-----BEGIN CERTIFICATE-----\n", "-----END CERTIFICATE-----\n",
                                    buf, sizeof(buf), &ret,
                                    mbedtls_ctr_drbg_random, &ctr_drbg,
                                    mbedtls_x509write_crt_get_csrf(&crt),
                                    NULL, 0);

    if (ret > 0) {
        // PEM buffer too small, allocate dynamically
        size_t pem_len = ret;
        *cert_pem = malloc(pem_len);
        if (*cert_pem == NULL) {
            ret = MBEDTLS_ERR_X509_ALLOC_FAILED;
            goto cleanup;
        }

        ret = mbedtls_pem_write_buffer("-----BEGIN CERTIFICATE-----\n", "-----END CERTIFICATE-----\n",
                                        *cert_pem, pem_len, &ret,
                                        mbedtls_ctr_drbg_random, &ctr_drbg,
                                        mbedtls_x509write_crt_get_csrf(&crt),
                                        NULL, 0);
        if (ret != 0) {
            ESP_LOGE(TAG, "mbedtls_pem_write_buffer(cert) failed: -0x%04x", -ret);
            free(*cert_pem);
            *cert_pem = NULL;
            goto cleanup;
        }
    } else if (ret == 0) {
        // Use buffer directly
        *cert_pem = strndup(buf, ret);
    } else {
        ESP_LOGE(TAG, "mbedtls_pem_write_buffer(cert) initial failed: -0x%04x", -ret);
        goto cleanup;
    }

    // Write private key to PEM
    ret = mbedtls_pk_write_key_pem(&key, (unsigned char *)buf, sizeof(buf));
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_pk_write_key_pem failed: -0x%04x", -ret);
        free(*cert_pem);
        *cert_pem = NULL;
        goto cleanup;
    }

    *key_pem = strdup(buf);

    ret = 0;  // Success

cleanup:
    mbedtls_mpi_free(&serial);
    mbedtls_x509write_crt_free(&crt);
    mbedtls_pk_free(&key);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    return (ret == 0) ? ESP_OK : ESP_FAIL;
}

/**
 * @brief Save certificate to NVS
 */
static esp_err_t save_cert_to_nvs(const char *cert, const char *key)
{
    nvs_handle_t nvs;
    esp_err_t err;

    err = nvs_open(CERT_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, CERT_KEY_CERT, cert);
    if (err != ESP_OK) {
        goto done;
    }

    err = nvs_set_str(nvs, CERT_KEY_KEY, key);
    if (err != ESP_OK) {
        goto done;
    }

    err = nvs_set_u8(nvs, CERT_KEY_VALID, 1);
    if (err != ESP_OK) {
        goto done;
    }

    err = nvs_commit(nvs);

done:
    nvs_close(nvs);
    return err;
}

/**
 * @brief Load certificate from NVS
 */
static esp_err_t load_cert_from_nvs(char **cert, char **key)
{
    nvs_handle_t nvs;
    esp_err_t err;
    size_t cert_len, key_len;
    uint8_t valid;

    err = nvs_open(CERT_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    // Check if valid
    err = nvs_get_u8(nvs, CERT_KEY_VALID, &valid);
    if (err != ESP_OK || valid != 1) {
        nvs_close(nvs);
        return ESP_ERR_NOT_FOUND;
    }

    // Get certificate length
    cert_len = 0;
    err = nvs_get_str(nvs, CERT_KEY_CERT, NULL, &cert_len);
    if (err != ESP_OK) {
        nvs_close(nvs);
        return err;
    }

    *cert = malloc(cert_len);
    if (*cert == NULL) {
        nvs_close(nvs);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_str(nvs, CERT_KEY_CERT, *cert, &cert_len);
    if (err != ESP_OK) {
        free(*cert);
        *cert = NULL;
        nvs_close(nvs);
        return err;
    }

    // Get key length
    key_len = 0;
    err = nvs_get_str(nvs, CERT_KEY_KEY, NULL, &key_len);
    if (err != ESP_OK) {
        free(*cert);
        *cert = NULL;
        nvs_close(nvs);
        return err;
    }

    *key = malloc(key_len);
    if (*key == NULL) {
        free(*cert);
        *cert = NULL;
        nvs_close(nvs);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_str(nvs, CERT_KEY_KEY, *key, &key_len);
    if (err != ESP_OK) {
        free(*cert);
        *cert = NULL;
        free(*key);
        *key = NULL;
        nvs_close(nvs);
        return err;
    }

    nvs_close(nvs);
    return ESP_OK;
}

// Public API implementation
esp_err_t cert_manager_init(void)
{
    char *cert = NULL;
    char *key = NULL;
    esp_err_t err;

    // Generate device SAN (Subject Alternative Name)
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(g_device_san, sizeof(g_device_san), "ESPX-%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    ESP_LOGI(TAG, "Initializing certificate manager for device: %s", g_device_san);

    // Try to load existing certificate
    err = load_cert_from_nvs(&cert, &key);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Loaded existing certificate from NVS");
        g_cert_valid = true;
        free(cert);
        free(key);
        return ESP_OK;
    }

    if (err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to load certificate: %s, will regenerate", esp_err_to_name(err));
    }

    // Generate new certificate
    ESP_LOGI(TAG, "Generating new self-signed certificate...");
    err = generate_self_signed_cert(&cert, &key);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to generate certificate: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Certificate generated successfully");

    // Save to NVS
    err = save_cert_to_nvs(cert, key);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save certificate to NVS: %s", esp_err_to_name(err));
        // Continue anyway - cert is in memory
    } else {
        ESP_LOGI(TAG, "Certificate saved to NVS");
    }

    g_cert_valid = true;
    free(cert);
    free(key);

    return ESP_OK;
}

esp_err_t cert_manager_get_server_cert(server_cert_t *cert)
{
    if (!g_cert_valid) {
        return ESP_ERR_INVALID_STATE;
    }

    return load_cert_from_nvs(&cert->cert_pem, &cert->key_pem);
}

esp_err_t cert_manager_regenerate(void)
{
    char *cert = NULL;
    char *key = NULL;
    esp_err_t err;

    ESP_LOGI(TAG, "Regenerating certificate...");

    // Generate new certificate
    err = generate_self_signed_cert(&cert, &key);
    if (err != ESP_OK) {
        return err;
    }

    // Save to NVS
    err = save_cert_to_nvs(cert, key);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save new certificate: %s", esp_err_to_name(err));
    }

    g_cert_valid = true;
    free(cert);
    free(key);

    ESP_LOGI(TAG, "Certificate regenerated successfully");

    return ESP_OK;
}

bool cert_manager_is_valid(void)
{
    return g_cert_valid;
}

esp_err_t cert_manager_get_info(char *info, size_t max_len)
{
    if (!g_cert_valid) {
        snprintf(info, max_len, "{\"valid\": false}");
        return ESP_OK;
    }

    snprintf(info, max_len,
             "{\"valid\": true, \"device\": \"%s\", \"algorithm\": \"RSA-2048\", \"validity_days\": %d}",
             g_device_san, CERT_VALID_DAYS);

    return ESP_OK;
}

void cert_manager_free_cert(server_cert_t *cert)
{
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
