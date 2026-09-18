/**
 * @file test_mqtt_client.c
 * @brief Unit tests for MQTT Client component
 */

#include "unity.h"
#include "mqtt_client.h"
#include <string.h>

void setUp(void) {
    mqtt_client_init();
}

void tearDown(void) {
    // Stop if running
    mqtt_client_stop();
}

void test_mqtt_client_init(void) {
    int ret = mqtt_client_init();
    TEST_ASSERT_EQUAL_INT(0, ret);

    // Double init should be idempotent
    ret = mqtt_client_init();
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_mqtt_client_configure(void) {
    int ret = mqtt_client_configure("mqtt://192.168.1.100:8883");
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_mqtt_client_configure_invalid(void) {
    int ret = mqtt_client_configure(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);

    ret = mqtt_client_configure("");
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_mqtt_client_set_auth(void) {
    int ret = mqtt_client_set_auth("user", "pass");
    TEST_ASSERT_EQUAL_INT(0, ret);

    // NULL password should also work
    ret = mqtt_client_set_auth("user", NULL);
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_mqtt_client_set_client_id(void) {
    int ret = mqtt_client_set_client_id("ESP32-001122334455");
    TEST_ASSERT_EQUAL_INT(0, ret);

    ret = mqtt_client_set_client_id(NULL);
    TEST_ASSERT_EQUAL_INT(-1, ret);
}

void test_mqtt_client_state_enum(void) {
    TEST_ASSERT_EQUAL_INT(0, MQTT_STATE_IDLE);
    TEST_ASSERT_EQUAL_INT(1, MQTT_STATE_CONNECTING);
    TEST_ASSERT_EQUAL_INT(2, MQTT_STATE_CONNECTED);
    TEST_ASSERT_EQUAL_INT(3, MQTT_STATE_DISCONNECTED);
    TEST_ASSERT_EQUAL_INT(4, MQTT_STATE_ERROR);
}

void test_mqtt_get_state_initial(void) {
    mqtt_state_t state = mqtt_get_state();
    TEST_ASSERT_EQUAL_INT(MQTT_STATE_IDLE, state);
}

void test_mqtt_is_connected_initial(void) {
    bool connected = mqtt_is_connected();
    TEST_ASSERT_FALSE(connected);
}

void test_mqtt_client_stop_when_idle(void) {
    // Stop when not started should be safe
    int ret = mqtt_client_stop();
    TEST_ASSERT_EQUAL_INT(0, ret);
}

void test_topic_macros(void) {
    const char *device_id = "ESP32-123";

    // Test that macros produce expected topics
    TEST_ASSERT_TRUE(strstr(TOPIC_TELEMETRY(device_id), "/telemetry") != NULL);
    TEST_ASSERT_TRUE(strstr(TOPIC_STATUS(device_id), "/status") != NULL);
    TEST_ASSERT_TRUE(strstr(TOPIC_CMD(device_id), "/cmd") != NULL);
    TEST_ASSERT_TRUE(strstr(TOPIC_LOG(device_id), "/log") != NULL);
    TEST_ASSERT_TRUE(strstr(TOPIC_OTA_START(device_id), "/ota/start") != NULL);
}

void app_main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_mqtt_client_init);
    RUN_TEST(test_mqtt_client_configure);
    RUN_TEST(test_mqtt_client_configure_invalid);
    RUN_TEST(test_mqtt_client_set_auth);
    RUN_TEST(test_mqtt_client_set_client_id);
    RUN_TEST(test_mqtt_client_state_enum);
    RUN_TEST(test_mqtt_get_state_initial);
    RUN_TEST(test_mqtt_is_connected_initial);
    RUN_TEST(test_mqtt_client_stop_when_idle);
    RUN_TEST(test_topic_macros);

    UNITY_END();
}
