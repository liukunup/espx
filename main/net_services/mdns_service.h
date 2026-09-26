/**
 * @file mdns_service.h
 * @brief mDNS / DNS-SD advertisement
 *
 * Publishes the node as <prefix>-<mac>.local so it is reachable without
 * discovering its DHCP address, and advertises the HTTPS service so a browser
 * or tool can find it (e.g. https://espx-84c7bb772e74.local/).
 *
 * TXT records carry the identity a client needs before the first request:
 * id, model, version.
 */

#ifndef MDNS_SERVICE_H
#define MDNS_SERVICE_H

#include <stdbool.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start mDNS and advertise the node
 *
 * Needs an IP address; safe to call again if it fails.
 */
esp_err_t mdns_service_start(void);

/**
 * @brief Stop mDNS
 */
esp_err_t mdns_service_stop(void);

/**
 * @brief Whether mDNS is running
 */
bool mdns_service_is_running(void);

/**
 * @brief Hostname without the ".local" suffix (e.g. espx-84c7bb772e74)
 */
const char* mdns_service_hostname(void);

/**
 * @brief Fully qualified name (e.g. espx-84c7bb772e74.local)
 */
const char* mdns_service_fqdn(void);

#ifdef __cplusplus
}
#endif

#endif /* MDNS_SERVICE_H */
