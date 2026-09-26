/**
 * @file mqtt_client.c
 * @brief MQTT Client implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <mqtt_client.h>

#include "mqtt_client.h"
#include "param_store/param_store.h"

static const char *TAG = "mqtt_client";

static esp_mqtt_client_handle_t g_mqtt_client = NULL;
static mqtt_event_cb_t g_event_cb = NULL;
static void *g_user_data = NULL;
static bool g_connected = false;
static bool g_started = false;

// MQTT configuration
static char g_broker[256] = {0};
static char g_username[64] = {0};
static char g_password[64] = {0};
static char g_client_id[64] = {0};
static char g_topic_prefix[128] = {0};

/**
 * @brief Load MQTT configuration from param_store
 */
static void load_mqtt_config(void)
{
    size_t len;

    len = sizeof(g_broker);
    if (param_store_get("mqtt_broker", g_broker, &len) != ESP_OK) {
        strcpy(g_broker, CONFIG_MQTT_BROKER_URL);
    }

    len = sizeof(g_username);
    param_store_get("mqtt_username", g_username, &len);

    len = sizeof(g_password);
    param_store_get("mqtt_password", g_password, &len);

    len = sizeof(g_client_id);
    if (param_store_get("mqtt_client_id", g_client_id, &len) != ESP_OK || strlen(g_client_id) == 0) {
        const char *device_id = param_store_get_device_id();
        snprintf(g_client_id, sizeof(g_client_id), "espx-%s", device_id);
    }

    len = sizeof(g_topic_prefix);
    if (param_store_get("mqtt_topic_prefix", g_topic_prefix, &len) != ESP_OK || strlen(g_topic_prefix) == 0) {
        const char *device_id = param_store_get_device_id();
        snprintf(g_topic_prefix, sizeof(g_topic_prefix), "espx/%s", device_id);
    }

    ESP_LOGI(TAG, "MQTT Config - Broker: %s, Client ID: %s, Topic Prefix: %s",
             g_broker, g_client_id, g_topic_prefix);
}

/**
 * @brief MQTT event handler
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case ESP_MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        g_connected = true;
        if (g_event_cb) {
            g_event_cb(MQTT_EVENT_CONNECTED, NULL, g_user_data);
        }
        // Auto-subscribe to commands topic
        char topic[256];
        snprintf(topic, sizeof(topic), "%s/commands/#", g_topic_prefix);
        mqtt_client_subscribe(topic, 1);
        break;

    case ESP_MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        g_connected = false;
        if (g_event_cb) {
            g_event_cb(MQTT_EVENT_DISCONNECTED, NULL, g_user_data);
        }
        break;

    case ESP_MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "MQTT subscribed: msg_id=%d", event->msg_id);
        if (g_event_cb) {
            g_event_cb(MQTT_EVENT_SUBSCRIBED, NULL, g_user_data);
        }
        break;

    case ESP_MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "MQTT published: msg_id=%d", event->msg_id);
        if (g_event_cb) {
            g_event_cb(MQTT_EVENT_PUBLISHED, NULL, g_user_data);
        }
        break;

    case ESP_MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "MQTT data: topic=%.*s", event->topic_len, event->topic);
        if (g_event_cb) {
            mqtt_message_t msg = {
                .topic = event->topic,
                .topic_len = event->topic_len,
                .data = event->data,
                .data_len = event->data_len
            };
            g_event_cb(MQTT_EVENT_DATA, &msg, g_user_data);
        }
        break;

    case ESP_MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;

    default:
        break;
    }
}

// Public API implementation
esp_err_t mqtt_client_init(void)
{
    ESP_LOGI(TAG, "Initializing MQTT client");
    load_mqtt_config();
    return ESP_OK;
}

esp_err_t mqtt_client_start(void)
{
    if (g_started && g_mqtt_client != NULL) {
        ESP_LOGW(TAG, "MQTT client already started");
        return ESP_OK;
    }

    // Reload config in case parameters changed
    load_mqtt_config();

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = g_broker,
        .credentials.client_id = g_client_id,
        .credentials.authentication.password = strlen(g_password) > 0 ? g_password : NULL,
        .credentials.username = strlen(g_username) > 0 ? g_username : NULL,
        .session.keepalive = 60,
        .session.disable_clean_session = false,
    };

    g_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (g_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_mqtt_client_register_event(g_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_mqtt_client_start(g_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        return err;
    }

    g_started = true;
    ESP_LOGI(TAG, "MQTT client started");

    return ESP_OK;
}

esp_err_t mqtt_client_stop(void)
{
    if (g_mqtt_client == NULL) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping MQTT client");

    esp_err_t err = esp_mqtt_client_stop(g_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop MQTT client: %s", esp_err_to_name(err));
        return err;
    }

    g_started = false;
    g_connected = false;

    return ESP_OK;
}

bool mqtt_client_is_connected(void)
{
    return g_connected;
}

esp_err_t mqtt_client_publish(const char *topic, const char *data, size_t len, int qos, bool retain)
{
    if (g_mqtt_client == NULL || !g_connected) {
        ESP_LOGW(TAG, "MQTT not connected, cannot publish");
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_publish(g_mqtt_client, topic, data, len, qos, retain ? 1 : 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish: %d", msg_id);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Published to %s, msg_id=%d", topic, msg_id);
    return ESP_OK;
}

esp_err_t mqtt_client_subscribe(const char *topic, int qos)
{
    if (g_mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_subscribe(g_mqtt_client, topic, qos);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to subscribe: %d", msg_id);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Subscribed to %s, msg_id=%d", topic, msg_id);
    return ESP_OK;
}

esp_err_t mqtt_client_unsubscribe(const char *topic)
{
    if (g_mqtt_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int msg_id = esp_mqtt_client_unsubscribe(g_mqtt_client, topic);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to unsubscribe: %d", msg_id);
        return ESP_FAIL;
    }

    return ESP_OK;
}

void mqtt_client_set_event_callback(mqtt_event_cb_t cb, void *user_data)
{
    g_event_cb = cb;
    g_user_data = user_data;
}

esp_err_t mqtt_client_reconnect(void)
{
    ESP_LOGI(TAG, "Reconnecting MQTT client");

    if (g_mqtt_client != NULL) {
        esp_mqtt_client_stop(g_mqtt_client);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_mqtt_client_start(g_mqtt_client);
    } else {
        return mqtt_client_start();
    }

    return ESP_OK;
}
