/**
 * @file test_nvs_storage.c
 * @brief Unit tests for NVS Storage component
 */

#include "unity.h"
#include "nvs_storage.h"
#include <string.h>

void setUp(void) {
    // Initialize NVS before each test
    nvs_init();
}

void tearDown(void) {
    // Clean up
    nvs_erase_all();
}

void test_nvs_init(void) {
    int ret = nvs_init();
    TEST_ASSERT_EQUAL_INT(0, ret);

    // Double init should be idempotent
    ret = nvs_init();
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_nvs_set_get_string(void) {
    const char *key = "test_string";
    const char *value = "Hello, ESP32!";

    int ret = nvs_set_string(key, value);
    TEST_ASSERT_EQUAL_INT(0, ret);

    char buffer[64] = {0};
    ret = nvs_get_string(key, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING(value, buffer);
}

void test_nvs_set_get_int(void) {
    const char *key = "test_int";
    int value = 12345;

    int ret = nvs_set_int(key, value);
    TEST_ASSERT_EQUAL_INT(0, ret);

    int retrieved = 0;
    ret = nvs_get_int(key, &retrieved);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT(value, retrieved);
}

void test_nvs_set_get_blob(void) {
    const char *key = "test_blob";
    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x12, 0x34, 0x56, 0x78};
    size_t data_len = sizeof(data);

    int ret = nvs_set_blob(key, data, data_len);
    TEST_ASSERT_EQUAL_INT(0, ret);

    uint8_t buffer[16] = {0};
    size_t buf_len = sizeof(buffer);
    ret = nvs_get_blob(key, buffer, &buf_len);
    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_INT(data_len, buf_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, buffer, data_len);
}

void test_nvs_get_nonexistent(void) {
    char buffer[64] = {0};
    int ret = nvs_get_string("nonexistent_key", buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_INT(-4, ret);  // ESP_ERR_NVS_NOT_FOUND
}

void test_nvs_erase(void) {
    const char *key = "test_erase";
    const char *value = "will be erased";

    // Set value
    int ret = nvs_set_string(key, value);
    TEST_ASSERT_EQUAL_INT(0, ret);

    // Erase
    ret = nvs_erase(key);
    TEST_ASSERT_EQUAL_INT(0, ret);

    // Verify erased
    char buffer[64] = {0};
    ret = nvs_get_string(key, buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL_INT(-4, ret);  // Not found
}

void test_nvs_erase_all(void) {
    // Set multiple values
    nvs_set_string("key1", "value1");
    nvs_set_string("key2", "value2");
    nvs_set_int("key3", 42);

    // Erase all
    int ret = nvs_erase_all();
    TEST_ASSERT_EQUAL_INT(0, ret);

    // Verify all erased
    char buffer[64] = {0};
    TEST_ASSERT_EQUAL_INT(-4, nvs_get_string("key1", buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_INT(-4, nvs_get_string("key2", buffer, sizeof(buffer)));

    int value = 0;
    TEST_ASSERT_EQUAL_INT(-4, nvs_get_int("key3", &value));
}

void test_nvs_commit(void) {
    nvs_set_string("commit_test", "test_value");

    int ret = nvs_commit();
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void app_main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_nvs_init);
    RUN_TEST(test_nvs_set_get_string);
    RUN_TEST(test_nvs_set_get_int);
    RUN_TEST(test_nvs_set_get_blob);
    RUN_TEST(test_nvs_get_nonexistent);
    RUN_TEST(test_nvs_erase);
    RUN_TEST(test_nvs_erase_all);
    RUN_TEST(test_nvs_commit);

    UNITY_END();
}
