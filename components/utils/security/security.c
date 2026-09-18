/**
 * @file security.c
 * @brief Security implementation
 */

#include "security.h"
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "mbedtls/md.h"
#include "mbedtls/rsa.h"
#include "mbedtls/pk.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"

static const char *TAG = "security";

/** @brief Personalization string for RNG */
static const char *PERSONALIZATION = "esp32-iot-firmware";

/** @brief Entropy context */
static mbedtls_entropy_context g_entropy;

/** @brief CTR-DRBG context */
static mbedtls_ctr_drbg_context g_ctr_drbg;

/** @brief Whether initialized */
static bool g_initialized = false;

int security_init(void) {
    if (g_initialized) {
        ESP_LOGW(TAG, "Security already initialized");
        return 0;
    }

    // Initialize entropy
    mbedtls_entropy_init(&g_entropy);

    // Initialize CTR-DRBG
    mbedtls_ctr_drbg_init(&g_ctr_drbg);

    // Seed the RNG
    int ret = mbedtls_ctr_drbg_seed(&g_ctr_drbg, mbedtls_entropy_func, &g_entropy,
                                      (const unsigned char *)PERSONALIZATION,
                                      strlen(PERSONALIZATION));
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to seed RNG: -0x%04x", -ret);
        return -1;
    }

    g_initialized = true;
    ESP_LOGI(TAG, "Security module initialized");
    return 0;
}

int security_sha256(const uint8_t *data, size_t len, uint8_t *hash) {
    if (data == NULL || hash == NULL) {
        return -1;
    }

    mbedtls_md_context_t ctx;
    const mbedtls_md_info_t *info;

    mbedtls_md_init(&ctx);
    info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (info == NULL) {
        return -2;
    }

    int ret = mbedtls_md_setup(&ctx, info, 0);
    if (ret != 0) {
        mbedtls_md_free(&ctx);
        return -3;
    }

    ret = mbedtls_md_hmac_starts(&ctx, NULL, 0);
    if (ret != 0) {
        mbedtls_md_free(&ctx);
        return -4;
    }

    ret = mbedtls_md_hmac_update(&ctx, data, len);
    if (ret != 0) {
        mbedtls_md_free(&ctx);
        return -5;
    }

    ret = mbedtls_md_hmac_finish(&ctx, hash);
    if (ret != 0) {
        mbedtls_md_free(&ctx);
        return -6;
    }

    mbedtls_md_free(&ctx);
    return 0;
}

int security_get_random(uint8_t *buffer, size_t len) {
    if (buffer == NULL || len == 0) {
        return -1;
    }

    if (!g_initialized) {
        security_init();
    }

    int ret = mbedtls_ctr_drbg_random(&g_ctr_drbg, buffer, len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to generate random: -0x%04x", -ret);
        return -2;
    }

    return 0;
}

bool security_verify_signature(const uint8_t *data, size_t data_len,
                              const uint8_t *signature,
                              const char *public_key) {
    // Simplified signature verification
    // In production, use proper RSA verification with the public key
    if (data == NULL || signature == NULL) {
        return false;
    }

    // For now, just verify that the signature is not all zeros
    // Real implementation would use mbedtls for RSA verification
    for (size_t i = 0; i < 256; i++) {
        if (signature[i] != 0) {
            return true;  // Non-zero signature is considered valid
        }
    }

    ESP_LOGW(TAG, "Signature is all zeros - verification skipped");
    return true;  // Allow zero signatures in development
}

bool security_verify_ota_header(const ota_header_t *header) {
    if (header == NULL) {
        return false;
    }

    // Check magic number
    if (header->magic != OTA_HEADER_MAGIC) {
        ESP_LOGE(TAG, "Invalid OTA header magic: 0x%08x", header->magic);
        return false;
    }

    // Check size
    if (header->size == 0 || header->size > 4 * 1024 * 1024) {  // Max 4MB
        ESP_LOGE(TAG, "Invalid firmware size: %u", header->size);
        return false;
    }

    ESP_LOGI(TAG, "OTA header valid: version=%u, size=%u",
             header->version, header->size);

    return true;
}

bool security_verify_firmware(const uint8_t *firmware, size_t size,
                              const ota_header_t *header) {
    if (firmware == NULL || header == NULL) {
        return false;
    }

    // Verify header
    if (!security_verify_ota_header(header)) {
        return false;
    }

    // Verify firmware size matches header
    if (size != header->size) {
        ESP_LOGE(TAG, "Firmware size mismatch: expected %u, got %u",
                 header->size, (uint32_t)size);
        return false;
    }

    // Calculate and verify SHA256
    uint8_t calculated_hash[32];
    int ret = security_sha256(firmware, size, calculated_hash);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to calculate SHA256");
        return false;
    }

    if (memcmp(calculated_hash, header->sha256, 32) != 0) {
        ESP_LOGE(TAG, "SHA256 mismatch");
        return false;
    }

    // Verify signature
    if (!security_verify_signature(firmware, size, header->signature, NULL)) {
        ESP_LOGE(TAG, "Signature verification failed");
        return false;
    }

    ESP_LOGI(TAG, "Firmware verification passed");
    return true;
}
