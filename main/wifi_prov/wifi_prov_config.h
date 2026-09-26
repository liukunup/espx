/* Wi-Fi Provisioning Configuration
 *
 * Every value here is derived from Kconfig: run `idf.py menuconfig`
 * (ESPX Configuration -> Wi-Fi provisioning) instead of editing this file.
 * Keeping a single source of truth avoids the two drifting apart.
 */

#ifndef WIFI_PROV_CONFIG_H
#define WIFI_PROV_CONFIG_H

#include <sdkconfig.h>

/* ============================================
 * Transport
 * ============================================ */
#define WIFI_PROV_TRANSPORT_SOFTAP 0
#define WIFI_PROV_TRANSPORT_BLE    1

#if defined(CONFIG_ESPX_PROV_TRANSPORT_BLE)
#define WIFI_PROV_TRANSPORT WIFI_PROV_TRANSPORT_BLE
#elif defined(CONFIG_ESPX_PROV_TRANSPORT_SOFTAP)
#define WIFI_PROV_TRANSPORT WIFI_PROV_TRANSPORT_SOFTAP
#else
#error "No provisioning transport selected (ESPX Configuration -> Provisioning transport)"
#endif

/* ============================================
 * Security
 * ============================================ */
#if defined(CONFIG_ESPX_PROV_SECURITY_VERSION_0)
#define WIFI_PROV_SECURITY_VERSION 0
#elif defined(CONFIG_ESPX_PROV_SECURITY_VERSION_2)
#define WIFI_PROV_SECURITY_VERSION 2
#else
#define WIFI_PROV_SECURITY_VERSION 1
#endif

#define WIFI_PROV_POP             CONFIG_ESPX_PROV_POP
#define WIFI_PROV_SEC2_USERNAME   CONFIG_ESPX_PROV_SEC2_USERNAME
#define WIFI_PROV_SEC2_PASSWORD   CONFIG_ESPX_PROV_SEC2_PASSWORD

#ifdef CONFIG_ESPX_PROV_SEC2_DEV_MODE
#define WIFI_PROV_SEC2_DEV_MODE 1
#else
#define WIFI_PROV_SEC2_DEV_MODE 0
#endif

/* ============================================
 * QR code
 * ============================================ */
#ifdef CONFIG_ESPX_PROV_SHOW_QR
#define WIFI_PROV_SHOW_QR 1
#else
#define WIFI_PROV_SHOW_QR 0
#endif

#ifdef CONFIG_ESPX_PROV_SHOW_POP_IN_QR
#define WIFI_PROV_SHOW_POP_IN_QR 1
#else
#define WIFI_PROV_SHOW_POP_IN_QR 0
#endif

/* ============================================
 * Re-provisioning and failure handling
 * ============================================ */
#ifdef CONFIG_ESPX_PROV_REPROVISIONING
#define WIFI_PROV_REPROVISIONING 1
#else
#define WIFI_PROV_REPROVISIONING 0
#endif

#ifdef CONFIG_ESPX_PROV_RESET_ON_FAILURE
#define WIFI_PROV_RESET_ON_FAILURE 1
#define WIFI_PROV_CONNECTION_COUNT CONFIG_ESPX_PROV_CONNECTION_COUNT
#else
#define WIFI_PROV_RESET_ON_FAILURE 0
#endif

/* ============================================
 * SoftAP
 * ============================================ */
#ifdef CONFIG_ESPX_PROV_SOFTAP_PREFIX
#define WIFI_PROV_SOFTAP_SSID_PREFIX CONFIG_ESPX_PROV_SOFTAP_PREFIX
#else
#define WIFI_PROV_SOFTAP_SSID_PREFIX "PROV_"
#endif

/* ============================================
 * Application callback
 * ============================================ */
#ifdef CONFIG_ESPX_PROV_ENABLE_APP_CALLBACK
#define WIFI_PROV_ENABLE_APP_CALLBACK 1
#else
#define WIFI_PROV_ENABLE_APP_CALLBACK 0
#endif

#endif /* WIFI_PROV_CONFIG_H */
