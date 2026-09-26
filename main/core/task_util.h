/**
 * @file task_util.h
 * @brief Task creation helpers that place stacks in PSRAM where safe
 *
 * Internal RAM on this chip is only 345 KB and is shared with Wi-Fi, Bluetooth,
 * TLS handshake state and PSA crypto. Every FreeRTOS task stack defaults to
 * internal RAM, and the application's own tasks accounted for ~37 KB of it.
 *
 * PSRAM is 8 MB and otherwise idle, so application task stacks are placed there.
 * Stacks are the right thing to move: they are plain memory, they are touched
 * often (so PSRAM latency is hidden), and no peripheral DMA targets them.
 *
 * The rule, from ESP-IDF's own Kconfig text for
 * CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM:
 *
 *     "This should only be used for tasks where the stack is never accessed
 *      while the cache is disabled."
 *
 * Flash writes and erases disable the cache, and PSRAM is reached through that
 * same cache, so a task with a PSRAM stack must never perform a flash
 * operation. Concretely, in this firmware that means:
 *
 *   MUST stay on internal RAM (touches flash via NVS or the OTA partition):
 *     ota_service   esp_ota_write()
 *     at_service    AT+CFG -> config_apply -> NVS
 *     ws_server     a config push over the socket -> NVS
 *     test_mode     writes the NVS test-mode request flag
 *     wifi_status   starts mDNS, which persists its hostname to NVS
 *     httpd         any REST handler that saves configuration (created by
 *                   esp_https_server, not by this file)
 *
 *   Safe in PSRAM (no flash access):
 *     sys_stats, device_manager tick, mqtt_publisher
 *
 * Getting this wrong is not subtle: the OTA task was briefly moved to PSRAM and
 * every update silently died at the first flash write.
 *
 * Requires CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM. Without it the helpers
 * fall back to an internal-RAM task, so the code stays correct either way.
 */

#ifndef TASK_UTIL_H
#define TASK_UTIL_H

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <esp_log.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM)
#define ESPX_TASK_HAS_EXT_MEM 1
#else
#define ESPX_TASK_HAS_EXT_MEM 0
#endif

/**
 * @brief Create a task whose stack lives in PSRAM when possible
 *
 * Falls back to internal RAM if PSRAM allocation fails, so a task is never
 * silently lost.
 */
static inline BaseType_t espx_task_create(TaskFunction_t fn, const char *name,
                                          uint32_t stack_depth, void *arg,
                                          UBaseType_t priority,
                                          TaskHandle_t *handle)
{
#if ESPX_TASK_HAS_EXT_MEM
    BaseType_t ok = xTaskCreateWithCaps(fn, name, stack_depth, arg, priority, handle,
                                        MALLOC_CAP_SPIRAM);
    if (ok == pdPASS) {
        return ok;
    }
    /* PSRAM exhausted or unusable: fall back rather than fail the subsystem. */
    ESP_LOGW("task_util", "PSRAM stack for '%s' failed, using internal RAM", name);
#endif
    return xTaskCreate(fn, name, stack_depth, arg, priority, handle);
}

/**
 * @brief Delete the calling task, releasing a PSRAM stack correctly
 *
 * ONLY for tasks created with espx_task_create(). Calling it from a task whose
 * stack came from plain xTaskCreate() aborts:
 *
 *     assert failed: prvTaskDeleteWithCaps idf_additions.c
 *
 * because the stack was not allocated with the caps allocator. Conversely a
 * task created with espx_task_create() must not use plain vTaskDelete(NULL), or
 * its stack is freed to the wrong allocator.
 *
 * Keep them paired: espx_task_create <-> espx_task_delete_self.
 */
static inline void espx_task_delete_self(void)
{
#if ESPX_TASK_HAS_EXT_MEM
    vTaskDeleteWithCaps(NULL);
#else
    vTaskDelete(NULL);
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* TASK_UTIL_H */
