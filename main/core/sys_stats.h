/**
 * @file sys_stats.h
 * @brief Runtime statistics: CPU load, RAM usage, task count
 *
 * CPU load needs a delta between two samples of the FreeRTOS runtime counters,
 * so it is computed by a small periodic task and cached. Readers get the latest
 * snapshot without blocking and without touching the scheduler.
 *
 * Two figures matter for RAM on this chip and they are different things:
 *   - INTERNAL RAM: the small, constrained pool. Wi-Fi, Bluetooth, TLS session
 *     state and PSA crypto all come from here. When it runs out you see TLS
 *     handshakes failing, not a headline number changing.
 *   - PSRAM: 8 MB, plentiful, but not usable for DMA or for most mbedTLS
 *     structures.
 * A dashboard that shows only a combined total hides the pool that actually
 * runs out, so both are reported separately.
 */

#ifndef SYS_STATS_H
#define SYS_STATS_H

#include <stdbool.h>
#include <stdint.h>
#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* CPU */
    bool     cpu_valid;         /**< false until a second sample is available */
    float    cpu_usage;         /**< percent, aggregate over both cores */
    uint32_t cpu_freq_mhz;

    /* Internal RAM */
    size_t   int_total;
    size_t   int_free;
    size_t   int_min_free;      /**< low-water mark since boot */
    size_t   int_largest;       /**< largest allocatable block */

    /* PSRAM */
    bool     psram_present;
    size_t   psram_total;
    size_t   psram_free;

    /* Tasks and time */
    uint32_t task_count;
    uint64_t uptime_s;
} sys_stats_t;

/**
 * @brief Start the sampler task (idempotent)
 */
esp_err_t sys_stats_start(void);

/**
 * @brief Copy the latest snapshot
 */
void sys_stats_get(sys_stats_t *out);

/**
 * @brief Internal RAM used, in percent (0 when unknown)
 */
static inline float sys_stats_internal_used_pct(const sys_stats_t *s)
{
    if (s->int_total == 0) return 0.0f;
    return 100.0f * (float)(s->int_total - s->int_free) / (float)s->int_total;
}

/**
 * @brief PSRAM used, in percent (0 when absent)
 */
static inline float sys_stats_psram_used_pct(const sys_stats_t *s)
{
    if (!s->psram_present || s->psram_total == 0) return 0.0f;
    return 100.0f * (float)(s->psram_total - s->psram_free) / (float)s->psram_total;
}

#ifdef __cplusplus
}
#endif

#endif /* SYS_STATS_H */
