/**
 * @file log_system.h
 * @brief Log System component with ring buffer and remote upload
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Log level enumeration
 */
typedef enum {
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR = 1,
    LOG_LEVEL_WARN = 2,
    LOG_LEVEL_INFO = 3,
    LOG_LEVEL_DEBUG = 4,
    LOG_LEVEL_VERBOSE = 5
} log_level_t;

/**
 * @brief Log entry structure
 */
typedef struct {
    uint32_t index;
    log_level_t level;
    uint32_t timestamp;
    char tag[16];
    char message[128];
} log_entry_t;

/** @brief Maximum log buffer size */
#define LOG_BUFFER_SIZE 512

/** @brief Maximum tag length */
#define LOG_MAX_TAG_LEN 15

/**
 * @brief Initialize log system
 *
 * @return 0 on success, negative on error
 */
int log_system_init(void);

/**
 * @brief Set global log level
 *
 * @param level Minimum log level to output
 * @return 0 on success
 */
int log_set_level(log_level_t level);

/**
 * @brief Get current log level
 *
 * @return Current log level
 */
log_level_t log_get_level(void);

/**
 * @brief Write a log entry
 *
 * @param level Log level
 * @param tag Log tag (max 15 chars)
 * @param format Printf-style format string
 * @param ... Format arguments
 * @return Number of characters written, negative on error
 */
int log_write(log_level_t level, const char *tag, const char *format, ...);

/**
 * @brief Read log entries from buffer
 *
 * @param entries Output array
 * @param max_count Maximum number of entries to read
 * @param start_index Start reading from this index (0 = oldest)
 * @return Number of entries actually read
 */
int log_read_entries(log_entry_t *entries, int max_count, uint32_t start_index);

/**
 * @brief Get the number of entries in the log buffer
 *
 * @return Number of entries
 */
uint32_t log_get_count(void);

/**
 * @brief Clear log buffer
 */
void log_clear(void);

/**
 * @brief Export recent logs as JSON string
 *
 * @param buffer Output buffer
 * @param len Buffer size
 * @param count Number of recent entries to export
 * @return 0 on success, negative on error
 */
int log_export_recent(char *buffer, size_t len, int count);

/**
 * @brief Get log entry by index
 *
 * @param index Entry index
 * @param entry Output entry
 * @return 0 on success, negative if not found
 */
int log_get_entry(uint32_t index, log_entry_t *entry);

// Log macros with tag support
#define LOGE(tag, ...) log_write(LOG_LEVEL_ERROR, tag, __VA_ARGS__)
#define LOGW(tag, ...) log_write(LOG_LEVEL_WARN, tag, __VA_ARGS__)
#define LOGI(tag, ...) log_write(LOG_LEVEL_INFO, tag, __VA_ARGS__)
#define LOGD(tag, ...) log_write(LOG_LEVEL_DEBUG, tag, __VA_ARGS__)
#define LOGV(tag, ...) log_write(LOG_LEVEL_VERBOSE, tag, __VA_ARGS__)

#ifdef __cplusplus
}
#endif
