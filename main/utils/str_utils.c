/**
 * @file str_utils.c
 * @brief Implementation of the string helpers (see str_utils.h)
 */

#include <string.h>
#include <stdio.h>

#include "str_utils.h"

size_t str_copy(char *dest, size_t dest_size, const char *src)
{
    if (dest == NULL || dest_size == 0) {
        return 0;
    }

    if (src == NULL) {
        dest[0] = '\0';
        return 0;
    }

    size_t i = 0;
    while (i + 1 < dest_size && src[i] != '\0') {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
    return i;
}

bool str_contains(const char *haystack, const char *needle)
{
    if (haystack == NULL || needle == NULL) {
        return false;
    }
    return strstr(haystack, needle) != NULL;
}

void mac_to_str(const uint8_t *mac, char *dest, size_t dest_size)
{
    if (dest == NULL || dest_size == 0) {
        return;
    }

    if (mac == NULL) {
        dest[0] = '\0';
        return;
    }

    snprintf(dest, dest_size, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}
