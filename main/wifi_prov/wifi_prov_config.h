/* Wi-Fi Provisioning Configuration

   This file contains all configurable parameters for Wi-Fi provisioning.
   Customize these macros to match your application requirements.
*/

#ifndef WIFI_PROV_CONFIG_H
#define WIFI_PROV_CONFIG_H

#include <sdkconfig.h>

/* ============================================
 * Transport Configuration
 * ============================================ */

/**
 * @brief Provisioning transport method
 * Options:
 *   - WIFI_PROV_TRANSPORT_BLE    : Use Bluetooth LE for provisioning
 *   - WIFI_PROV_TRANSPORT_SOFTAP : Use SoftAP (Wi-Fi Access Point) for provisioning
 */
#define WIFI_PROV_TRANSPORT WIFI_PROV_TRANSPORT_SOFTAP

/* ============================================
 * Security Configuration
 * ============================================ */

/**
 * @brief Security version for provisioning
 * Options:
 *   - 0 : No security (plain text communication)
 *   - 1 : Proof of Possession (PoP) based security with X25519 key exchange
 *   - 2 : SRP6a based authentication + AES-GCM encryption
 */
#define WIFI_PROV_SECURITY_VERSION 1

/**
 * @brief Proof of Possession (PoP) string for Security 1
 * Only used when WIFI_PROV_SECURITY_VERSION == 1
 */
#define WIFI_PROV_POP "abcd1234"

/**
 * @brief Username for Security 2 (SRP6a)
 * Only used when WIFI_PROV_SECURITY_VERSION == 2
 */
#define WIFI_PROV_SEC2_USERNAME "wifiprov"

/**
 * @brief Password for Security 2 (SRP6a)
 * Only used when WIFI_PROV_SECURITY_VERSION == 2 and dev mode
 */
#define WIFI_PROV_SEC2_PASSWORD "abcd1234"

/**
 * @brief Enable development mode for Security 2
 * When enabled, uses hardcoded salt/verifier (for development only)
 * When disabled, must provide salt/verifier from device manufacturing partition
 */
#define WIFI_PROV_SEC2_DEV_MODE 1

/* ============================================
 * QR Code Configuration
 * ============================================ */

/**
 * @brief Enable QR code display on console
 * When enabled, displays QR code for easy provisioning
 */
#define WIFI_PROV_SHOW_QR 1

/* ============================================
 * Reprovisioning Configuration
 * ============================================ */

/**
 * @brief Enable reprovisioning support
 * When enabled, allows device to be re-provisioned after initial setup
 */
#define WIFI_PROV_REPROVISIONING 0

/* ============================================
 * Failure Handling Configuration
 * ============================================ */

/**
 * @brief Reset provisioning manager on failure
 * When enabled, resets state machine after max connection attempts
 */
#define WIFI_PROV_RESET_ON_FAILURE 0

/**
 * @brief Number of Wi-Fi connection attempts before failure
 * Only used when WIFI_PROV_RESET_ON_FAILURE is enabled
 */
#define WIFI_PROV_CONNECTION_COUNT 5

/* ============================================
 * SoftAP Configuration
 * ============================================ */

/**
 * @brief SoftAP SSID prefix
 * Final SSID will be: PREFIX + last 3 bytes of STA MAC
 */
#define WIFI_PROV_SOFTAP_SSID_PREFIX "PROV_"

/* ============================================
 * Callback Configuration
 * ============================================ */

/**
 * @brief Enable application callback for provisioning events
 * When enabled, wifi_prov_app_callback() will be called on events
 */
#define WIFI_PROV_ENABLE_APP_CALLBACK 0

#endif /* WIFI_PROV_CONFIG_H */
