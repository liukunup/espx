/**
 * @file health_monitor.h
 * @brief Health Monitor component for watchdog and metrics
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reset reason enumeration
 */
typedef enum {
    RESET_REASON_POWER_ON = 0,
    RESET_REASON_SW = 1,
    RESET_REASON_PANIC = 2,
    RESET_REASON_EXCEPTION = 3,
    RESET_REASON_WDT = 4,
    RESET_REASON_BROWNOUT = 5,
    RESET_REASON_OTA = 6
} reset_reason_t;

/**
 * @brief System metrics structure
 */
typedef struct {
    uint32_t timestamp;
    uint32_t free_heap_min;
    uint32_t free_heap_current;
    float cpu_temp;
    int mqtt_reconnect_count;
    uint32_t mqtt_packets_sent;
    uint32_t mqtt_packets_failed;
    uint32_t sensor_read_count;
    uint32_t sensor_error_count;
    float sensor_success_rate;
} metrics_t;

/**
 * @brief Watchdog configuration
 */
typedef struct {
    const char *task_name;
    uint32_t timeout_ms;
} watchdog_config_t;

/**
 * @brief Initialize health monitor
 *
 * @return 0 on success, negative on error
 */
int health_monitor_init(void);

/**
 * @brief Register a watchdog timer for a task
 *
 * @param task_name Task name (for identification)
 * @param timeout_ms Watchdog timeout in milliseconds
 * @return 0 on success, negative on error
 */
int watchdog_register(const char *task_name, uint32_t timeout_ms);

/**
 * @brief Feed (reset) a watchdog timer
 *
 * @param task_name Task name
 * @return 0 on success, negative on error
 */
int watchdog_feed(const char *task_name);

/**
 * @brief Unregister a watchdog timer
 *
 * @param task_name Task name
 * @return 0 on success, negative on error
 */
int watchdog_unregister(const char *task_name);

/**
 * @brief Report device status
 *
 * @return 0 on success, negative on error
 */
int health_report_status(void);

/**
 * @brief Report device metrics
 *
 * @return 0 on success, negative on error
 */
int health_report_metrics(void);

/**
 * @brief Collect current metrics
 *
 * @param metrics Output metrics structure
 * @return 0 on success, negative on error
 */
int metrics_collect(metrics_t *metrics);

/**
 * @brief Export metrics as JSON
 *
 * @param buffer Output buffer
 * @param len Buffer length
 * @return 0 on success, negative on error
 */
int metrics_export_json(char *buffer, size_t len);

/**
 * @brief Register panic handler
 */
void register_panic_handler(void);

/**
 * @brief Get reset count since boot
 *
 * @return Reset count
 */
int health_get_reset_count(void);

/**
 * @brief Get last reset reason
 *
 * @return Reset reason
 */
reset_reason_t health_get_last_reset_reason(void);

/**
 * @brief Get reset reason as string
 *
 * @param reason Reset reason
 * @return String representation
 */
const char* health_get_reset_reason_string(reset_reason_t reason);

/**
 * @brief Get minimum free heap since boot
 *
 * @return Minimum free heap in bytes
 */
uint32_t health_get_min_free_heap(void);

/**
 * @brief Get current free heap
 *
 * @return Current free heap in bytes
 */
uint32_t health_get_free_heap(void);

/**
 * @brief Increment MQTT reconnect counter
 */
void health_inc_mqtt_reconnect(void);

/**
 * @brief Increment MQTT packets sent
 */
void health_inc_mqtt_sent(void);

/**
 * @brief Increment MQTT packets failed
 */
void health_inc_mqtt_failed(void);

/**
 * @brief Increment sensor read counter
 */
void health_inc_sensor_read(void);

/**
 * @brief Increment sensor error counter
 */
void health_inc_sensor_error(void);

#ifdef __cplusplus
}
#endif
