/**
 * @file security.h
 * @brief Security component for signature verification
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief OTA header magic number */
#define OTA_HEADER_MAGIC 0x454C4F47  // "ELOG"

/**
 * @brief OTA header structure
 */
typedef struct __attribute__((packed)) {
    uint32_t magic;           /**< Magic number (ELOG) */
    uint32_t version;         /**< Firmware version */
    uint32_t size;            /**< Firmware size */
    uint8_t  sha256[32];      /**< SHA256 checksum */
    uint8_t  signature[256];  /**< RSA-2048 signature */
    uint8_t  reserved[252];   /**< Reserved */
} ota_header_t;

/**
 * @brief Initialize security module
 *
 * @return 0 on success, negative on error
 */
int security_init(void);

/**
 * @brief Verify signature of data
 *
 * @param data Data to verify
 * @param data_len Data length
 * @param signature Signature to check
 * @param public_key Public key (PEM format, can be NULL to use built-in)
 * @return true if signature is valid
 */
bool security_verify_signature(const uint8_t *data, size_t data_len,
                               const uint8_t *signature,
                               const char *public_key);

/**
 * @brief Calculate SHA256 hash
 *
 * @param data Input data
 * @param len Data length
 * @param hash Output buffer (32 bytes)
 * @return 0 on success, negative on error
 */
int security_sha256(const uint8_t *data, size_t len, uint8_t *hash);

/**
 * @brief Generate random bytes
 *
 * @param buffer Output buffer
 * @param len Number of random bytes to generate
 * @return 0 on success, negative on error
 */
int security_get_random(uint8_t *buffer, size_t len);

/**
 * @brief Verify OTA header
 *
 * @param header OTA header to verify
 * @return true if header is valid
 */
bool security_verify_ota_header(const ota_header_t *header);

/**
 * @brief Verify OTA firmware
 *
 * @param firmware Firmware data
 * @param size Firmware size
 * @param header OTA header
 * @return true if firmware is valid
 */
bool security_verify_firmware(const uint8_t *firmware, size_t size,
                               const ota_header_t *header);

#ifdef __cplusplus
}
#endif
