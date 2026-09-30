/**
 * @file net_services.h
 * @brief Services that require an IP address
 *
 * NTP and mDNS cannot run before the station has an address, but the rest of
 * the firmware must not block waiting for one (see app_main's ordering notes).
 * net_services_start() is therefore called from the task that reports the Wi-Fi
 * connection, not from the boot sequence.
 */

#ifndef NET_SERVICES_H
#define NET_SERVICES_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start NTP and mDNS (idempotent, non-blocking)
 */
esp_err_t net_services_start(void);

/**
 * @brief Start ESP-NOW service (call after Wi-Fi connects)
 */
void net_services_start_esp_now(void);

/**
 * @brief Whether the initial time sync has completed
 */
bool net_services_time_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* NET_SERVICES_H */
