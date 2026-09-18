/**
 * @file test_event_loop.c
 * @brief Unit tests for Event Loop component
 */

#include "unity.h"
#include "event_loop.h"
#include <string.h>

static int g_handler1_called = 0;
static int g_handler2_called = 0;
static event_type_t g_last_event_type = EVENT_TYPE_COUNT;

void setUp(void) {
    g_handler1_called = 0;
    g_handler2_called = 0;
    g_last_event_type = EVENT_TYPE_COUNT;
}

void tearDown(void) {
}

// Test handlers
void test_handler1(const event_t *event) {
    g_handler1_called++;
    g_last_event_type = event->type;
}

void test_handler2(const event_t *event) {
    g_handler2_called++;
    (void)event;
}

void test_event_loop_init(void) {
    int ret = event_loop_init();
    TEST_ASSERT_EQUAL_INT(0, ret);

    // Double init should also succeed (idempotent)
    ret = event_loop_init();
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_event_subscribe(void) {
    int ret = event_subscribe(EVENT_WIFI_CONNECTED, test_handler1);
    TEST_ASSERT_EQUAL_INT(0, ret);

    ret = event_subscribe(EVENT_WIFI_CONNECTED, test_handler2);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_event_subscribe_invalid_type(void) {
    int ret = event_subscribe(EVENT_TYPE_COUNT, test_handler1);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_event_subscribe_null_handler(void) {
    int ret = event_subscribe(EVENT_WIFI_CONNECTED, NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_event_type_to_string(void) {
    TEST_ASSERT_EQUAL_STRING("WIFI_CONNECTED", event_type_to_string(EVENT_WIFI_CONNECTED));
    TEST_ASSERT_EQUAL_STRING("WIFI_DISCONNECTED", event_type_to_string(EVENT_WIFI_DISCONNECTED));
    TEST_ASSERT_EQUAL_STRING("MQTT_CONNECTED", event_type_to_string(EVENT_MQTT_CONNECTED));
    TEST_ASSERT_EQUAL_STRING("OTA_START", event_type_to_string(EVENT_OTA_START));
    TEST_ASSERT_EQUAL_STRING("EXCEPTION", event_type_to_string(EVENT_EXCEPTION));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", event_type_to_string(EVENT_TYPE_COUNT));
}

void test_event_publish(void) {
    // Subscribe first
    event_subscribe(EVENT_SENSOR_DATA_READY, test_handler1);

    // Publish event
    int test_data = 42;
    int ret = event_publish(EVENT_SENSOR_DATA_READY, &test_data, sizeof(test_data));
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_event_publish_invalid_type(void) {
    int ret = event_publish(EVENT_TYPE_COUNT, NULL, 0);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_event_loop_run_tests(void) {
    // Initialize
    TEST_ASSERT_EQUAL_INT(0, event_loop_init());

    // Subscribe handlers
    TEST_ASSERT_EQUAL_INT(0, event_subscribe(EVENT_MQTT_CONNECTED, test_handler1));
    TEST_ASSERT_EQUAL_INT(0, event_subscribe(EVENT_MQTT_CONNECTED, test_handler2));

    // Publish multiple events
    TEST_ASSERT_EQUAL_INT(0, event_publish(EVENT_MQTT_CONNECTED, NULL, 0));

    // Give event loop time to process
    vTaskDelay(pdMS_TO_TICKS(100));

    // Both handlers should have been called
    TEST_ASSERT_GREATER_THAN(0, g_handler1_called);
    TEST_ASSERT_GREATER_THAN(0, g_handler2_called);
}

void app_main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_event_loop_init);
    RUN_TEST(test_event_subscribe);
    RUN_TEST(test_event_subscribe_invalid_type);
    RUN_TEST(test_event_subscribe_null_handler);
    RUN_TEST(test_event_type_to_string);
    RUN_TEST(test_event_publish);
    RUN_TEST(test_event_publish_invalid_type);
    RUN_TEST(test_event_loop_run_tests);

    UNITY_END();
}
