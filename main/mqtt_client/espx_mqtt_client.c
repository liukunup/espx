/**
 * @file mqtt_client.c
 * @brief ESPX MQTT Client
 *
 * Topic format: {prefix}/{topic}
 *   Default prefix: "espx/{device_id}"
 *
 * Standard topics:
 *   {prefix}/state          - online/offline/heartbeat
 *   {prefix}/sensors        - sensor data
 *   {prefix}/attrs          - attribute changes
 *   {prefix}/cmd/query/+    - query commands
 *   {prefix}/cmd/control/+  - control commands
 *   {prefix}/cmd/config     - config commands
 *   {prefix}/cmd/reboot     - reboot command
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_event.h>
#include <mqtt_client.h>
#include <cJSON.h>

#include "espx_mqtt_client.h"
#include "node_config.h"

static const char *TAG = "mqtt_client";

static esp_mqtt_client_handle_t g_mqtt_client = NULL;
static bool g_started = false;
static bool g_connected = false;
static char g_topic_prefix[128] = {0};
static char g_device_id[64] = {0};
static char g_broker[256] = {0};
static char g_username[64] = {0};
static char g_password[64] = {0};

/**
 * @brief Load network configuration from node_config
 *
 * Reads the "network" object (set via Web UI or factory data),
 * falling back to built-in defaults.
 */
static void load_network_config(void)
{
    snprintf(g_broker, sizeof(g_broker), "mqtt://broker.example.com:1883");
    g_username[0] = '\0';
    g_password[0] = '\0';
    /* Default prefix is the device id itself: it already carries the product
     * prefix ("espx-<mac>"), so "espx/<device_id>" would duplicate it. */
    snprintf(g_topic_prefix, sizeof(g_topic_prefix), "%s", g_device_id);

    cJSON *cfg = node_config_get();
    if (cfg == NULL) return;

    cJSON *net = cJSON_GetObjectItem(cfg, "network");
    if (cJSON_IsObject(net)) {
        cJSON *v;

        v = cJSON_GetObjectItem(net, "mqtt_broker");
        if (cJSON_IsString(v) && v->valuestring[0] != '\0') {
            strncpy(g_broker, v->valuestring, sizeof(g_broker) - 1);
        }

        v = cJSON_GetObjectItem(net, "mqtt_username");
        if (cJSON_IsString(v)) {
            strncpy(g_username, v->valuestring, sizeof(g_username) - 1);
        }

        v = cJSON_GetObjectItem(net, "mqtt_password");
        if (cJSON_IsString(v)) {
            strncpy(g_password, v->valuestring, sizeof(g_password) - 1);
        }

        v = cJSON_GetObjectItem(net, "mqtt_topic_prefix");
        if (cJSON_IsString(v) && v->valuestring[0] != '\0') {
            strncpy(g_topic_prefix, v->valuestring, sizeof(g_topic_prefix) - 1);
        }
    }

    cJSON_Delete(cfg);

    ESP_LOGI(TAG, "Network: broker=%s prefix=%s user=%s",
             g_broker, g_topic_prefix, g_username[0] ? g_username : "-");
}

static void build_topic(char *dest, size_t dest_size, const char *subtopic)
{
    snprintf(dest, dest_size, "%s/%s", g_topic_prefix, subtopic);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        g_connected = true;

        // Subscribe to command topics
        char topic[256];

        build_topic(topic, sizeof(topic), "cmd/query/+");
        esp_mqtt_client_subscribe(g_mqtt_client, topic, 1);
        ESP_LOGI(TAG, "Subscribed: %s", topic);

        build_topic(topic, sizeof(topic), "cmd/control/+");
        esp_mqtt_client_subscribe(g_mqtt_client, topic, 1);
        ESP_LOGI(TAG, "Subscribed: %s", topic);

        build_topic(topic, sizeof(topic), "cmd/config");
        esp_mqtt_client_subscribe(g_mqtt_client, topic, 1);
        ESP_LOGI(TAG, "Subscribed: %s", topic);

        build_topic(topic, sizeof(topic), "cmd/reboot");
        esp_mqtt_client_subscribe(g_mqtt_client, topic, 1);
        ESP_LOGI(TAG, "Subscribed: %s", topic);

        // Publish online state
        char payload[128];
        snprintf(payload, sizeof(payload), "{\"online\":true,\"device_id\":\"%s\"}", g_device_id);
        build_topic(topic, sizeof(topic), "state");
        esp_mqtt_client_publish(g_mqtt_client, topic, payload, strlen(payload), 1, true);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        g_connected = false;
        break;

    case MQTT_EVENT_DATA: {
        ESP_LOGI(TAG, "MQTT data: topic=%.*s", event->topic_len, event->topic);

        // Parse topic
        char topic_str[256];
        size_t topic_len = event->topic_len < sizeof(topic_str) - 1 ? event->topic_len : sizeof(topic_str) - 1;
        memcpy(topic_str, event->topic, topic_len);
        topic_str[topic_len] = '\0';

        char *payload = malloc(event->data_len + 1);
        if (payload == NULL) break;
        memcpy(payload, event->data, event->data_len);
        payload[event->data_len] = '\0';

        // Forward to commander
        extern void mqtt_commander_handle(const char *topic, const char *payload, int payload_len);
        mqtt_commander_handle(topic_str, payload, event->data_len);

        free(payload);
        break;
    }

    default:
        break;
    }
}

esp_err_t mqtt_client_init(void)
{
    strncpy(g_device_id, node_config_get_device_id(), sizeof(g_device_id) - 1);
    load_network_config();
    ESP_LOGI(TAG, "MQTT client initialized, device_id: %s", g_device_id);
    return ESP_OK;
}

esp_err_t mqtt_client_start(void)
{
    if (g_started && g_mqtt_client != NULL) {
        return ESP_OK;
    }

    // (Re)load configuration
    load_network_config();

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = g_broker,
        .credentials.client_id = g_device_id,
        .session.keepalive = 60,
        .session.disable_clean_session = false,
    };

    if (g_username[0] != '\0') {
        mqtt_cfg.credentials.username = g_username;
    }
    if (g_password[0] != '\0') {
        mqtt_cfg.credentials.authentication.password = g_password;
    }

    g_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (g_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(g_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_err_t err = esp_mqtt_client_start(g_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        return err;
    }

    g_started = true;
    ESP_LOGI(TAG, "MQTT client started, prefix: %s", g_topic_prefix);
    return ESP_OK;
}

esp_err_t mqtt_client_stop(void)
{
    if (g_mqtt_client == NULL) {
        return ESP_OK;
    }
    esp_err_t err = esp_mqtt_client_stop(g_mqtt_client);
    g_started = false;
    g_connected = false;
    return err;
}

bool mqtt_client_is_connected(void)
{
    return g_connected;
}

esp_err_t mqtt_client_publish(const char *subtopic, const char *data, size_t len, int qos, bool retain)
{
    if (g_mqtt_client == NULL || !g_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    char topic[256];
    build_topic(topic, sizeof(topic), subtopic);

    int msg_id = esp_mqtt_client_publish(g_mqtt_client, topic, data, len, qos, retain ? 1 : 0);
    if (msg_id < 0) {
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Published to %s, msg_id=%d", topic, msg_id);
    return ESP_OK;
}

esp_err_t mqtt_client_publish_absolute(const char *topic, const char *data,
                                       size_t len, int qos, bool retain)
{
    if (g_mqtt_client == NULL || !g_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    int msg_id = esp_mqtt_client_publish(g_mqtt_client, topic, data, len, qos,
                                         retain ? 1 : 0);
    return msg_id >= 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_client_subscribe(const char *topic_filter, int qos)
{
    if (g_mqtt_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int msg_id = esp_mqtt_client_subscribe(g_mqtt_client, topic_filter, qos);
    return msg_id >= 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t mqtt_client_unsubscribe(const char *topic_filter)
{
    if (g_mqtt_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    int msg_id = esp_mqtt_client_unsubscribe(g_mqtt_client, topic_filter);
    return msg_id >= 0 ? ESP_OK : ESP_FAIL;
}

const char* mqtt_client_get_prefix(void)
{
    return g_topic_prefix;
}
