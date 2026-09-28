/* Host unit tests for main/utils/str_utils.c (no ESP-IDF dependency). */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "str_utils.h"

static int g_fail = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

static void test_str_copy(void)
{
    char buf[8];

    memset(buf, 'X', sizeof(buf));
    CHECK(str_copy(buf, sizeof(buf), "abc") == 3);
    CHECK(strcmp(buf, "abc") == 0);

    /* exact fit: 7 chars + NUL in 8 bytes */
    CHECK(str_copy(buf, sizeof(buf), "1234567") == 7);
    CHECK(strcmp(buf, "1234567") == 0);

    /* truncation: NUL-terminated, last byte is NUL, nothing past the end */
    memset(buf, 'X', sizeof(buf));
    CHECK(str_copy(buf, sizeof(buf), "0123456789abcdef") == 7);
    CHECK(strcmp(buf, "0123456") == 0);
    CHECK(buf[7] == '\0');

    /* NULL source empties the destination */
    CHECK(str_copy(buf, sizeof(buf), NULL) == 0);
    CHECK(buf[0] == '\0');

    /* zero size writes nothing */
    CHECK(str_copy(buf, 0, "abc") == 0);

    /* NULL destination is a no-op */
    CHECK(str_copy(NULL, 8, "abc") == 0);

    /* size 1 writes only the terminator */
    buf[0] = 'X';
    CHECK(str_copy(buf, 1, "abc") == 0);
    CHECK(buf[0] == '\0');
}

static void test_str_contains(void)
{
    CHECK(str_contains("wifi_password", "password") == true);
    CHECK(str_contains("pass", "password") == false);
    CHECK(str_contains("mqtt://host", "://") == true);
    CHECK(str_contains("abc", "") == true);
    CHECK(str_contains(NULL, "x") == false);
    CHECK(str_contains("x", NULL) == false);
    CHECK(str_contains(NULL, NULL) == false);
}

static void test_mac_to_str(void)
{
    const uint8_t mac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
    char buf[18];

    mac_to_str(mac, buf, sizeof(buf));
    CHECK(strcmp(buf, "DE:AD:BE:EF:00:01") == 0);

    /* small buffer truncates safely and stays NUL-terminated */
    char small[9];
    memset(small, 'X', sizeof(small));
    mac_to_str(mac, small, sizeof(small));
    CHECK(strlen(small) == 8);
    CHECK(small[8] == '\0');

    /* NULL mac empties */
    mac_to_str(NULL, buf, sizeof(buf));
    CHECK(buf[0] == '\0');

    /* NULL dest / zero size must not crash */
    mac_to_str(mac, NULL, 18);
    mac_to_str(mac, buf, 0);
}

int main(void)
{
    test_str_copy();
    test_str_contains();
    test_mac_to_str();

    if (g_fail == 0) {
        printf("str_utils: all tests passed\n");
        return 0;
    }
    printf("str_utils: %d check(s) failed\n", g_fail);
    return 1;
}
