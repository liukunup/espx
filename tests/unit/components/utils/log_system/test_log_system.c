/**
 * @file test_log_system.c
 * @brief Unit tests for Log System component
 */

#include "unity.h"
#include "log_system.h"
#include <string.h>

void setUp(void) {
    log_system_init();
}

void tearDown(void) {
    log_clear();
}

void test_log_system_init(void) {
    int ret = log_system_init();
    TEST_ASSERT_EQUAL_INT(0, ret);

    // Double init should be idempotent
    ret = log_system_init();
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_log_set_level(void) {
    log_set_level(LOG_LEVEL_DEBUG);
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_DEBUG, log_get_level());

    log_set_level(LOG_LEVEL_ERROR);
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_ERROR, log_get_level());
}

void test_log_write_basic(void) {
    int ret = log_write(LOG_LEVEL_INFO, "test", "Hello %s", "World");
    TEST_ASSERT_GREATER_THAN(0, ret);

    // Verify count increased
    TEST_ASSERT_EQUAL_INT32(1, log_get_count());
}

void test_log_write_filter(void) {
    // Set level to ERROR
    log_set_level(LOG_LEVEL_ERROR);

    // Write DEBUG (should be filtered)
    log_write(LOG_LEVEL_DEBUG, "test", "This should be filtered");
    TEST_ASSERT_EQUAL_INT32(0, log_get_count());

    // Write ERROR (should pass)
    log_write(LOG_LEVEL_ERROR, "test", "This should be logged");
    TEST_ASSERT_EQUAL_INT32(1, log_get_count());
}

void test_log_read_entries(void) {
    // Write some logs
    log_write(LOG_LEVEL_INFO, "tag1", "Message 1");
    log_write(LOG_LEVEL_ERROR, "tag2", "Message 2");
    log_write(LOG_LEVEL_WARN, "tag3", "Message 3");

    log_entry_t entries[10];
    int count = log_read_entries(entries, 10, 0);

    TEST_ASSERT_EQUAL_INT(3, count);
    TEST_ASSERT_EQUAL_STRING("tag1", entries[0].tag);
    TEST_ASSERT_EQUAL_STRING("Message 1", entries[0].message);
}

void test_log_read_entries_with_start_index(void) {
    // Write some logs
    for (int i = 0; i < 5; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "Message %d", i);
        log_write(LOG_LEVEL_INFO, "test", "%s", msg);
    }

    log_entry_t entries[10];
    int count = log_read_entries(entries, 10, 2);

    TEST_ASSERT_EQUAL_INT(3, count);  // Should get last 3
}

void test_log_export_recent(void) {
    log_write(LOG_LEVEL_INFO, "test", "Hello");
    log_write(LOG_LEVEL_ERROR, "test", "World");

    char buffer[1024];
    int ret = log_export_recent(buffer, sizeof(buffer), 10);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_TRUE(strstr(buffer, "[") != NULL);
    TEST_ASSERT_TRUE(strstr(buffer, "Hello") != NULL);
    TEST_ASSERT_TRUE(strstr(buffer, "World") != NULL);
    TEST_ASSERT_TRUE(strstr(buffer, "]") != NULL);
}

void test_log_get_entry(void) {
    log_write(LOG_LEVEL_INFO, "mytag", "My message");

    log_entry_t entry;
    int ret = log_get_entry(0, &entry);

    TEST_ASSERT_EQUAL_INT(0, ret);
    TEST_ASSERT_EQUAL_STRING("mytag", entry.tag);
    TEST_ASSERT_EQUAL_STRING("My message", entry.message);
    TEST_ASSERT_EQUAL_INT(LOG_LEVEL_INFO, entry.level);
}

void test_log_get_entry_invalid(void) {
    log_entry_t entry;
    int ret = log_get_entry(999, &entry);  // Non-existent index
    TEST_ASSERT_EQUAL_INT(-3, ret);
}

void test_log_clear(void) {
    log_write(LOG_LEVEL_INFO, "test", "Test message");
    TEST_ASSERT_EQUAL_INT32(1, log_get_count());

    log_clear();
    TEST_ASSERT_EQUAL_INT32(0, log_get_count());
}

void app_main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_log_system_init);
    RUN_TEST(test_log_set_level);
    RUN_TEST(test_log_write_basic);
    RUN_TEST(test_log_write_filter);
    RUN_TEST(test_log_read_entries);
    RUN_TEST(test_log_read_entries_with_start_index);
    RUN_TEST(test_log_export_recent);
    RUN_TEST(test_log_get_entry);
    RUN_TEST(test_log_get_entry_invalid);
    RUN_TEST(test_log_clear);

    UNITY_END();
}
