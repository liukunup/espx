/**
 * @file time_sync.h
 * @brief NTP time synchronisation
 *
 * Needs an IP address, so it is started once the station is connected.
 * A correct clock matters for:
 *   - log timestamps that can be correlated across devices
 *   - certificate validity checks (a clock before the cert's notBefore makes
 *     every TLS client reject it)
 *   - `Retry-After`-style scheduling and anything the cloud timestamps
 */

#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Configure SNTP (does not start it; safe before Wi-Fi is up)
 */
esp_err_t time_sync_init(void);

/**
 * @brief Start SNTP polling
 *
 * Non-blocking. Use time_sync_wait() if a synced clock is required.
 */
esp_err_t time_sync_start(void);

/**
 * @brief Wait for the first successful sync
 *
 * @param timeout_ms 0 to wait forever
 * @return ESP_OK when synced, ESP_ERR_TIMEOUT otherwise
 */
esp_err_t time_sync_wait(uint32_t timeout_ms);

/**
 * @brief Whether the clock has ever been set by NTP
 */
bool time_sync_is_synced(void);

/**
 * @brief Unix time in seconds (0 when never synced)
 */
int64_t time_sync_epoch(void);

/**
 * @brief Format the local time as ISO 8601 (e.g. 2026-09-26T18:04:05+0800)
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE when not synced yet
 */
esp_err_t time_sync_iso8601(char *buf, size_t len);

/**
 * @brief Configured timezone string (POSIX TZ)
 */
const char* time_sync_timezone(void);

/**
 * @brief Configured NTP server
 */
const char* time_sync_server(void);

#ifdef __cplusplus
}
#endif

#endif /* TIME_SYNC_H */
