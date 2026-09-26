/**
 * @file cert_manager.h
 * @brief Certificate manager for ESPX device
 *
 * Handles self-signed certificate generation, storage, and retrieval
 * for HTTPS server functionality.
 */

#ifndef CERT_MANAGER_H
#define CERT_MANAGER_H

#include <stddef.h>
#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Server certificate structure
 */
typedef struct {
    char *cert_pem;    /**< PEM-encoded certificate (NUL terminated) */
    char *key_pem;     /**< PEM-encoded private key (NUL terminated) */
    /** Length INCLUDING the terminating NUL, as mbedtls_x509_crt_parse()
     *  requires for PEM input. Do not "fix" this by subtracting one: the TLS
     *  handshake will fail. */
    size_t cert_len;
    size_t key_len;
} server_cert_t;

/**
 * @brief Initialize certificate manager
 *
 * Checks for existing certificate in NVS, or generates new self-signed cert.
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t cert_manager_init(void);

/**
 * @brief Get server certificate and key
 *
 * @param cert Output certificate structure (caller must free strings)
 * @return ESP_OK on success, error code on failure
 */
esp_err_t cert_manager_get_server_cert(server_cert_t *cert);

/**
 * @brief Regenerate server certificate
 *
 * @return ESP_OK on success, error code on failure
 */
esp_err_t cert_manager_regenerate(void);

/**
 * @brief Check if valid certificate exists
 *
 * @return true if valid certificate exists, false otherwise
 */
bool cert_manager_is_valid(void);

/**
 * @brief Get certificate info
 *
 * @param info Output buffer for certificate info
 * @param max_len Maximum buffer size
 * @return ESP_OK on success, error code on failure
 */
esp_err_t cert_manager_get_info(char *info, size_t max_len);

/**
 * @brief Free certificate structure
 *
 * @param cert Certificate structure to free
 */
void cert_manager_free_cert(server_cert_t *cert);

#ifdef __cplusplus
}
#endif

#endif // CERT_MANAGER_H
