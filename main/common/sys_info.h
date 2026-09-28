/**
 * @file sys_info.h
 * @brief Node status snapshot shared by the HTTP API and the MQTT reporter
 *
 * The same fields are needed in two places: GET /api/system/info for the web
 * UI and the periodic MQTT report used for fleet monitoring. Keeping one
 * builder avoids the two drifting apart.
 */

#ifndef SYS_INFO_H
#define SYS_INFO_H

#include <cJSON.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Add the node status fields to an existing JSON object
 *
 * Adds uptime, heap/RAM figures, clock/NTP state, mDNS name, WebSocket client
 * count, CPU load and Wi-Fi state. Fields that are not available yet (e.g. the
 * clock before NTP sync) are emitted as null so a consumer can tell "unknown"
 * apart from "zero".
 *
 * @param json Destination object; must already exist
 */
void sys_info_add(cJSON *json);

/**
 * @brief Build a fresh JSON object containing the node status
 *
 * @return Newly allocated object, or NULL on OOM. Caller owns it.
 */
cJSON *sys_info_build(void);

#ifdef __cplusplus
}
#endif

#endif /* SYS_INFO_H */
