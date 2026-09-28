/**
 * @file str_utils.h
 * @brief String helpers that are safe by construction
 *
 * The codebase used `strncpy(dst, src, sizeof(dst) - 1)` in 25 places. That
 * idiom is wrong by one (it wastes a byte and never terminates when the source
 * fills the buffer) and easy to get wrong when the buffer arrives as a pointer
 * plus a length. str_copy() states the intent once.
 */

#ifndef STR_UTILS_H
#define STR_UTILS_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Copy a string into a fixed-size buffer, always NUL-terminating
 *
 * Never writes past @p dest_size bytes. A source longer than the buffer is
 * truncated; the destination is still NUL-terminated.
 *
 * @param dest       Destination buffer (may be NULL, then nothing happens)
 * @param dest_size  Total size of @p dest in bytes
 * @param src        Source string (may be NULL, then @p dest is emptied)
 * @return Number of characters copied, excluding the terminator
 */
size_t str_copy(char *dest, size_t dest_size, const char *src);

/**
 * @brief Whether @p haystack contains @p needle
 *
 * @return false when either argument is NULL
 */
bool str_contains(const char *haystack, const char *needle);

/**
 * @brief Render a 6-byte MAC as "AA:BB:CC:DD:EE:FF"
 *
 * @param mac       6-byte address (NULL empties @p dest)
 * @param dest      Destination buffer
 * @param dest_size Size of @p dest; 18 bytes holds the full string
 */
void mac_to_str(const uint8_t *mac, char *dest, size_t dest_size);

#ifdef __cplusplus
}
#endif

#endif /* STR_UTILS_H */
