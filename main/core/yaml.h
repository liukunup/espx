/**
 * @file yaml.h
 * @brief Minimal YAML subset parser producing cJSON
 *
 * Supports the configuration subset used by ESPX:
 *   - block mappings            key: value
 *   - nested mappings           by indentation
 *   - block sequences           - item
 *   - sequences of mappings     - id: x  /  type: y
 *   - scalars                   int, float, true/false/yes/no/on/off, null/~, strings
 *   - single/double quoted strings (with escapes in double quotes)
 *   - comments (#), blank lines, CRLF
 *
 * Not supported (deliberately): anchors/aliases, multi-line block scalars
 * (| and >), flow collections ([a, b] / {a: b}), tags, documents (---).
 * Inline JSON is accepted where a scalar is expected, so a value may be a
 * JSON object/array if needed.
 *
 * The parser is dependency-light on purpose (cJSON + libc only) so it can be
 * unit-tested on the host.
 */

#ifndef ESPX_YAML_H
#define ESPX_YAML_H

#include <stdbool.h>

struct cJSON;
typedef struct cJSON cJSON;

/**
 * @brief Parse a YAML document
 *
 * @param text NUL-terminated YAML text
 * @return Newly allocated cJSON value (caller frees), or NULL on error
 */
cJSON *yaml_parse(const char *text);

/**
 * @brief Parse and report the error location
 *
 * @param text      NUL-terminated YAML text
 * @param err_line  Optional: 1-based line number of the failure
 * @param err_msg   Optional: static description of the failure
 * @return Newly allocated cJSON value, or NULL on error
 */
cJSON *yaml_parse_ex(const char *text, int *err_line, const char **err_msg);

/**
 * @brief Whether a payload looks like YAML rather than JSON
 *
 * JSON objects/arrays start with '{' or '['; everything else is treated as YAML.
 */
static inline bool yaml_looks_like_yaml(const char *text)
{
    while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n') text++;
    return !(*text == '{' || *text == '[');
}

#endif /* ESPX_YAML_H */
