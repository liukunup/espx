/**
 * @file sys_stats.c
 * @brief Runtime statistics implementation
 */

#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_chip_info.h>

#include "sys_stats.h"

static const char *TAG = "sys_stats";

#define SAMPLE_PERIOD_MS   2000
#define MAX_TASKS          40

static sys_stats_t s_stats;
static TaskHandle_t s_task = NULL;

/* Previous runtime-counter totals, for the CPU delta */
static uint64_t s_prev_total = 0;
static uint64_t s_prev_idle = 0;

/**
 * @brief Sample the FreeRTOS runtime counters
 *
 * Requires CONFIG_FREERTOS_USE_TRACE_FACILITY and
 * CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS. Both are enabled in
 * sdkconfig.defaults; without them this returns nothing and CPU load stays
 * unavailable rather than reporting a fabricated zero.
 */
static bool sample_cpu(void)
{
    static TaskStatus_t tasks[MAX_TASKS];

#if !defined(CONFIG_FREERTOS_USE_TRACE_FACILITY) || \
    !defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
    return false;
#else
    UBaseType_t n = uxTaskGetSystemState(tasks, MAX_TASKS, NULL);
    if (n == 0) {
        return false;
    }

    uint64_t total = 0;
    uint64_t idle = 0;

    for (UBaseType_t i = 0; i < n; i++) {
        total += tasks[i].ulRunTimeCounter;

        /* Both cores have their own idle task; on an SMP build the aggregate
         * load is 1 - (all idle time / all task time). */
        const char *name = tasks[i].pcTaskName;
        if (name != NULL &&
            (strncmp(name, "IDLE", 4) == 0 || strcmp(name, "esp_timer") != 0)) {
            if (strncmp(name, "IDLE", 4) == 0) {
                idle += tasks[i].ulRunTimeCounter;
            }
        }
    }

    s_stats.task_count = (uint32_t)n;

    if (s_prev_total != 0 && total > s_prev_total) {
        uint64_t d_total = total - s_prev_total;
        uint64_t d_idle = (idle >= s_prev_idle) ? (idle - s_prev_idle) : 0;

        float usage = 100.0f * (1.0f - ((float)d_idle / (float)d_total));
        if (usage < 0.0f) usage = 0.0f;
        if (usage > 100.0f) usage = 100.0f;
        s_stats.cpu_usage = usage;
        s_stats.cpu_valid = true;
    }

    s_prev_total = total;
    s_prev_idle = idle;
    return true;
#endif
}

static void collect(void)
{
    sample_cpu();

    s_stats.int_total    = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    s_stats.int_free     = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    s_stats.int_min_free = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    s_stats.int_largest  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    if (psram_total > 0) {
        s_stats.psram_present = true;
        s_stats.psram_total   = psram_total;
        s_stats.psram_free    = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    }

    s_stats.uptime_s = (uint64_t)(esp_timer_get_time() / 1000000ULL);
}

static void sys_stats_task(void *arg)
{
    /* First pass only primes the counters: the CPU figure needs a delta. */
    collect();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
        collect();
    }
}

esp_err_t sys_stats_start(void)
{
    if (s_task != NULL) {
        return ESP_OK;
    }

    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.cpu_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;

    /* Publish a first snapshot synchronously so an early request is not empty. */
    collect();

    if (xTaskCreate(sys_stats_task, "sys_stats", 3072, NULL, 1, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "failed to start the sampler task");
        return ESP_FAIL;
    }

    if (!s_stats.cpu_valid) {
        ESP_LOGW(TAG, "CPU load unavailable: enable CONFIG_FREERTOS_USE_TRACE_FACILITY "
                      "and CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS");
    }

    ESP_LOGI(TAG, "statistics sampler started (%d ms period)", SAMPLE_PERIOD_MS);
    return ESP_OK;
}

void sys_stats_get(sys_stats_t *out)
{
    if (out == NULL) {
        return;
    }

    if (s_task == NULL) {
        /* Not started yet: return a one-shot sample rather than zeros. */
        memset(&s_stats, 0, sizeof(s_stats));
        s_stats.cpu_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
        collect();
    }

    *out = s_stats;
}
