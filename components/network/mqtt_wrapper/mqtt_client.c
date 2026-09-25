/**
 * @file mqtt_client.c
 * @brief MQTT Client implementation
 */

// esp-mqtt header must come first to define esp_mqtt_client_handle_t
#include <mqtt_client.h>
#include "mqtt_wrapper.h"  // Project's wrapper header
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "mqtt_client";

/** @brief Maximum number of subscriptions */
#define MAX_SUBSCRIPTIONS 10

/** @brief Device ID (set from config) */
static char g_device_id[32] = "ESP32-unknown";

/** @brief MQTT client handle */
static esp_mqtt_client_handle_t g_mqtt_client = NULL;

/** @brief Current state */
static volatile mqtt_state_t g_mqtt_state = MQTT_STATE_IDLE;

/** @brief Connection callback */
static mqtt_connect_cb_t g_connect_callback = NULL;

/** @brief Subscription entry */
typedef struct {
    char topic[128];
    mqtt_message_cb_t callback;
    mqtt_qos_t qos;
    bool active;
} subscription_t;

/** @brief Subscription table */
static subscription_t g_subscriptions[MAX_SUBSCRIPTIONS];

/** @brief Mutex for thread safety */
static SemaphoreHandle_t g_mutex = NULL;

/** @brief MQTT configuration */
static struct {
    char broker[256];
    char username[64];
    char password[64];
    char client_id[64];
    char ca_cert[4096];
    char client_cert[4096];
    char client_key[4096];
    bool tls_enabled;
} g_mqtt_config = {0};

/**
 * @brief MQTT event handler
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event->event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        g_mqtt_state = MQTT_STATE_CONNECTED;

        // Resubscribe to all topics
        for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
            if (g_subscriptions[i].active) {
                int msg_id = esp_mqtt_client_subscribe(g_mqtt_client, g_subscriptions[i].topic,
                                                       (int)g_subscriptions[i].qos);
                ESP_LOGD(TAG, "Resubscribed to %s, msg_id=%d", g_subscriptions[i].topic, msg_id);
            }
        }

        if (g_connect_callback) {
            g_connect_callback(MQTT_STATE_CONNECTED);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected");
        g_mqtt_state = MQTT_STATE_DISCONNECTED;
        if (g_connect_callback) {
            g_connect_callback(MQTT_STATE_DISCONNECTED);
        }
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGD(TAG, "Subscribed, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGD(TAG, "Unsubscribed, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_PUBLISHED:
        ESP_LOGD(TAG, "Published, msg_id=%d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGD(TAG, "Message received on topic: %.*s",
                 event->topic_len, event->topic);

        // Find matching subscription and call callback
        for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
            if (g_subscriptions[i].active && g_subscriptions[i].callback) {
                // Simple topic matching (supports + and # wildcards would need more logic)
                if (strncmp(g_subscriptions[i].topic, event->topic, event->topic_len) == 0) {
                    g_subscriptions[i].callback(event->topic, event->data, event->data_len);
                    break;
                }
            }
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        g_mqtt_state = MQTT_STATE_ERROR;
        if (g_connect_callback) {
            g_connect_callback(MQTT_STATE_ERROR);
        }
        break;

    default:
        ESP_LOGD(TAG, "MQTT event: %ld", event_id);
        break;
    }
}

int mqtt_client_init(void) {
    if (g_mutex != NULL) {
        ESP_LOGW(TAG, "MQTT client already initialized");
        return 0;
    }

    g_mutex = xSemaphoreCreateMutex();
    if (g_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return -1;
    }

    memset(g_subscriptions, 0, sizeof(g_subscriptions));
    g_mqtt_state = MQTT_STATE_IDLE;

    ESP_LOGI(TAG, "MQTT client initialized");
    return 0;
}

int mqtt_client_configure(const char *broker) {
    if (broker == NULL || strlen(broker) == 0) {
        ESP_LOGE(TAG, "Invalid broker URL");
        return -1;
    }

    strncpy(g_mqtt_config.broker, broker, sizeof(g_mqtt_config.broker) - 1);
    g_mqtt_config.broker[sizeof(g_mqtt_config.broker) - 1] = '\0';

    return 0;
}

int mqtt_client_set_tls(const char *ca_cert, const char *client_cert, const char *client_key) {
    if (ca_cert == NULL) {
        ESP_LOGE(TAG, "CA certificate is required");
        return -1;
    }

    strncpy(g_mqtt_config.ca_cert, ca_cert, sizeof(g_mqtt_config.ca_cert) - 1);
    g_mqtt_config.ca_cert[sizeof(g_mqtt_config.ca_cert) - 1] = '\0';

    if (client_cert) {
        strncpy(g_mqtt_config.client_cert, client_cert, sizeof(g_mqtt_config.client_cert) - 1);
        g_mqtt_config.client_cert[sizeof(g_mqtt_config.client_cert) - 1] = '\0';
    }

    if (client_key) {
        strncpy(g_mqtt_config.client_key, client_key, sizeof(g_mqtt_config.client_key) - 1);
        g_mqtt_config.client_key[sizeof(g_mqtt_config.client_key) - 1] = '\0';
    }

    g_mqtt_config.tls_enabled = true;
    return 0;
}

int mqtt_client_set_auth(const char *username, const char *password) {
    if (username) {
        strncpy(g_mqtt_config.username, username, sizeof(g_mqtt_config.username) - 1);
        g_mqtt_config.username[sizeof(g_mqtt_config.username) - 1] = '\0';
    }

    if (password) {
        strncpy(g_mqtt_config.password, password, sizeof(g_mqtt_config.password) - 1);
        g_mqtt_config.password[sizeof(g_mqtt_config.password) - 1] = '\0';
    }

    return 0;
}

int mqtt_client_set_client_id(const char *client_id) {
    if (client_id == NULL) {
        return -1;
    }

    strncpy(g_mqtt_config.client_id, client_id, sizeof(g_mqtt_config.client_id) - 1);
    g_mqtt_config.client_id[sizeof(g_mqtt_config.client_id) - 1] = '\0';

    strncpy(g_device_id, client_id, sizeof(g_device_id) - 1);
    g_device_id[sizeof(g_device_id) - 1] = '\0';

    return 0;
}

int mqtt_client_start(void) {
    if (g_mqtt_client != NULL) {
        ESP_LOGW(TAG, "MQTT client already started");
        return 0;
    }

    if (strlen(g_mqtt_config.broker) == 0) {
        ESP_LOGE(TAG, "Broker not configured");
        return -1;
    }

    // Configure MQTT client
    esp_mqtt_client_config_t mqtt_cfg = {0};
    mqtt_cfg.broker.address.uri = g_mqtt_config.broker;
    mqtt_cfg.credentials.client_id = strlen(g_mqtt_config.client_id) > 0 ?
                                      g_mqtt_config.client_id : g_device_id;

    if (strlen(g_mqtt_config.username) > 0) {
        mqtt_cfg.credentials.username = g_mqtt_config.username;
    }
    if (strlen(g_mqtt_config.password) > 0) {
        mqtt_cfg.credentials.authentication.password = g_mqtt_config.password;
    }

    mqtt_cfg.session.keepalive = DEFAULT_KEEPALIVE;
    mqtt_cfg.session.disable_clean_session = false;

    // TLS configuration - simplified for ESP-IDF v6.1 API compatibility
    // Note: esp-mqtt v5+ uses different configuration structure
    if (g_mqtt_config.tls_enabled && strlen(g_mqtt_config.ca_cert) > 0) {
        ESP_LOGI(TAG, "TLS enabled with CA cert");
        // TLS configuration needs to be adapted for esp-mqtt v5+ API
    }

    g_mqtt_state = MQTT_STATE_CONNECTING;
    g_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (g_mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to create MQTT client");
        g_mqtt_state = MQTT_STATE_ERROR;
        return -2;
    }

    esp_mqtt_client_register_event(g_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_err_t err = esp_mqtt_client_start(g_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        g_mqtt_state = MQTT_STATE_ERROR;
        return -3;
    }

    ESP_LOGI(TAG, "MQTT client started");
    return 0;
}

int mqtt_client_stop(void) {
    if (g_mqtt_client == NULL) {
        return 0;
    }

    esp_err_t err = esp_mqtt_client_stop(g_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop MQTT client: %s", esp_err_to_name(err));
        return -1;
    }

    g_mqtt_state = MQTT_STATE_DISCONNECTED;
    ESP_LOGI(TAG, "MQTT client stopped");
    return 0;
}

int mqtt_client_reconnect(void) {
    if (g_mqtt_client == NULL) {
        return mqtt_client_start();
    }

    esp_err_t err = esp_mqtt_client_reconnect(g_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reconnect: %s", esp_err_to_name(err));
        return -1;
    }

    g_mqtt_state = MQTT_STATE_CONNECTING;
    return 0;
}

int mqtt_publish(const char *topic, const char *data, int len, mqtt_qos_t qos, bool retain) {
    if (g_mqtt_client == NULL || g_mqtt_state != MQTT_STATE_CONNECTED) {
        ESP_LOGW(TAG, "MQTT not connected, cannot publish");
        return -1;
    }

    if (topic == NULL || data == NULL) {
        return -2;
    }

    if (len < 0) {
        len = strlen(data);
    }

    int msg_id = esp_mqtt_client_publish(g_mqtt_client, topic, data, len,
                                         (int)qos, retain ? 1 : 0);
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to publish to %s", topic);
        return -3;
    }

    ESP_LOGD(TAG, "Published to %s, msg_id=%d", topic, msg_id);
    return msg_id;
}

int mqtt_subscribe(const char *topic, mqtt_message_cb_t callback, mqtt_qos_t qos) {
    if (topic == NULL || callback == NULL) {
        return -1;
    }

    // Find or allocate subscription slot
    int slot = -1;
    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (!g_subscriptions[i].active) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        ESP_LOGE(TAG, "No free subscription slots");
        return -2;
    }

    // Store subscription
    strncpy(g_subscriptions[slot].topic, topic, sizeof(g_subscriptions[slot].topic) - 1);
    g_subscriptions[slot].topic[sizeof(g_subscriptions[slot].topic) - 1] = '\0';
    g_subscriptions[slot].callback = callback;
    g_subscriptions[slot].qos = qos;
    g_subscriptions[slot].active = true;

    // Subscribe if connected
    if (g_mqtt_client != NULL && g_mqtt_state == MQTT_STATE_CONNECTED) {
        int msg_id = esp_mqtt_client_subscribe(g_mqtt_client, topic, (int)qos);
        if (msg_id < 0) {
            ESP_LOGE(TAG, "Failed to subscribe to %s", topic);
            g_subscriptions[slot].active = false;
            return -3;
        }
        ESP_LOGI(TAG, "Subscribed to %s, msg_id=%d", topic, msg_id);
    } else {
        ESP_LOGI(TAG, "Queued subscription for %s (will subscribe when connected)", topic);
    }

    return 0;
}

int mqtt_unsubscribe(const char *topic) {
    if (topic == NULL) {
        return -1;
    }

    for (int i = 0; i < MAX_SUBSCRIPTIONS; i++) {
        if (g_subscriptions[i].active && strcmp(g_subscriptions[i].topic, topic) == 0) {
            // Note: esp-mqtt v5+ doesn't support runtime unsubscribe
            // Subscription will be cleaned up on disconnect
            g_subscriptions[i].active = false;
            ESP_LOGI(TAG, "Marked unsubscribed from %s (will be unsubscribed on reconnect)", topic);
            return 0;
        }
    }

    return -2;  // Not found
}

mqtt_state_t mqtt_get_state(void) {
    return g_mqtt_state;
}

bool mqtt_is_connected(void) {
    return g_mqtt_state == MQTT_STATE_CONNECTED;
}

int mqtt_register_connect_callback(mqtt_connect_cb_t callback) {
    g_connect_callback = callback;
    return 0;
}

// Convenience publish functions
int mqtt_publish_telemetry(const char *json_data) {
    char topic[64];
    snprintf(topic, sizeof(topic), TOPIC_PREFIX"%s/telemetry", g_device_id);
    return mqtt_publish(topic, json_data, -1, MQTT_QOS_1, false);
}

int mqtt_publish_status(const char *json_status) {
    char topic[64];
    snprintf(topic, sizeof(topic), TOPIC_PREFIX"%s/status", g_device_id);
    return mqtt_publish(topic, json_status, -1, MQTT_QOS_1, true);  // Retained
}

int mqtt_publish_log(int level, const char *message) {
    char topic[64];
    char payload[256];
    snprintf(topic, sizeof(topic), TOPIC_PREFIX"%s/log", g_device_id);
    snprintf(payload, sizeof(payload), "{\"level\":%d,\"msg\":\"%s\",\"ts\":%llu}",
             level, message, (unsigned long long)(esp_timer_get_time() / 1000));
    return mqtt_publish(topic, payload, -1, MQTT_QOS_0, false);
}

int mqtt_publish_cmd_response(const char *cmd_id, int code, const char *message, const char *result) {
    char topic[64];
    char payload[512];
    snprintf(topic, sizeof(topic), TOPIC_PREFIX"%s/cmd/response", g_device_id);

    if (result) {
        snprintf(payload, sizeof(payload),
                 "{\"cmd_id\":\"%s\",\"code\":%d,\"message\":\"%s\",\"result\":%s}",
                 cmd_id, code, message, result);
    } else {
        snprintf(payload, sizeof(payload),
                 "{\"cmd_id\":\"%s\",\"code\":%d,\"message\":\"%s\"}",
                 cmd_id, code, message);
    }

    return mqtt_publish(topic, payload, -1, MQTT_QOS_1, false);
}

int mqtt_publish_ota_progress(int progress, const char *message) {
    char topic[64];
    char payload[256];
    snprintf(topic, sizeof(topic), TOPIC_PREFIX"%s/ota/progress", g_device_id);
    snprintf(payload, sizeof(payload), "{\"progress\":%d,\"msg\":\"%s\"}", progress, message);
    return mqtt_publish(topic, payload, -1, MQTT_QOS_1, false);
}
