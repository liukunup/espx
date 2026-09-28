/* Host unit tests for main/utils/json_utils.c (no ESP-IDF dependency). */
#include <stdio.h>
#include <string.h>

#include <cJSON.h>
#include "json_utils.h"

static int g_fail = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

static void test_get_string(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", "Node-1");
    cJSON_AddNumberToObject(o, "port", 1883);

    CHECK(strcmp(json_get_string(o, "name", "?"), "Node-1") == 0);
    /* missing key -> default */
    CHECK(strcmp(json_get_string(o, "nope", "def"), "def") == 0);
    /* wrong type -> default */
    CHECK(strcmp(json_get_string(o, "port", "def"), "def") == 0);
    /* NULL object -> default */
    CHECK(strcmp(json_get_string(NULL, "name", "def"), "def") == 0);
    /* empty string is a value, not a default */
    cJSON_AddStringToObject(o, "empty", "");
    CHECK(strcmp(json_get_string(o, "empty", "def"), "") == 0);

    cJSON_Delete(o);
}

static void test_get_scalars(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "port", 1883);
    cJSON_AddBoolToObject(o, "enabled", 1);
    cJSON_AddStringToObject(o, "name", "x");

    CHECK(json_get_int(o, "port", 0) == 1883);
    CHECK(json_get_int(o, "name", -1) == -1);
    CHECK(json_get_bool(o, "enabled", false) == true);
    CHECK(json_get_bool(o, "name", true) == true);
    CHECK(json_get_int(NULL, "port", 7) == 7);

    cJSON_Delete(o);
}

static void test_set_string_replace(void)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "k", "old");

    json_set_string(o, "k", "new");
    CHECK(strcmp(json_get_string(o, "k", ""), "new") == 0);
    CHECK(cJSON_GetArraySize(o) == 1); /* replaced, not duplicated */

    json_set_string(o, "fresh", "v"); /* insert when missing */
    CHECK(strcmp(json_get_string(o, "fresh", ""), "v") == 0);
    CHECK(cJSON_GetArraySize(o) == 2);

    /* NULL deletes the key: the Web UI clears the Wi-Fi password this way */
    json_set_string(o, "fresh", NULL);
    CHECK(cJSON_GetObjectItem(o, "fresh") == NULL);
    CHECK(cJSON_GetArraySize(o) == 1);

    cJSON_Delete(o);
}

static void test_set_scalars(void)
{
    cJSON *o = cJSON_CreateObject();
    json_set_number(o, "n", 42);
    json_set_bool(o, "b", true);
    CHECK(json_get_int(o, "n", 0) == 42);
    CHECK(json_get_bool(o, "b", false) == true);

    json_set_number(o, "n", 7); /* replace in place */
    CHECK(json_get_int(o, "n", 0) == 7);
    CHECK(cJSON_GetArraySize(o) == 2);

    /* NULL object must not crash */
    json_set_string(NULL, "k", "v");
    json_set_number(NULL, "k", 1);
    json_set_bool(NULL, "k", true);

    cJSON_Delete(o);
}

int main(void)
{
    test_get_string();
    test_get_scalars();
    test_set_string_replace();
    test_set_scalars();

    if (g_fail == 0) {
        printf("json_utils: all tests passed\n");
        return 0;
    }
    printf("json_utils: %d check(s) failed\n", g_fail);
    return 1;
}
