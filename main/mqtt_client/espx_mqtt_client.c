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
#include <esp_timer.h>
#include <mqtt_client.h>
#include <cJSON.h>

#include "espx_mqtt_client.h"
#include "node_config.h"
#include "event_bus.h"
#include "app_info.h"
#if defined(CONFIG_MQTT_PROTOCOL_5)
#include "mqtt5_client.h"
#endif

static const char *TAG = "mqtt_client";

/* MQTT 5 session expiry: how long the broker keeps this client's session (and
 * therefore any QoS>0 command published while we are offline) after the link
 * drops. Bounded on purpose — a command meant for "now" must not survive an
 * outage and fire hours later. Publishers should additionally stamp control
 * commands with their own message expiry (see docs/DEPLOYMENT.md). */
#define MQTT_SESSION_EXPIRY_S 300

/* MQTT 5 message expiry on everything we publish. Retained state/status is
 * refreshed every 30 s, so a healthy node always looks fresh; if the node dies
 * the broker drops the stale retained snapshot instead of reporting it online
 * forever. */
#define MQTT_MSG_EXPIRY_S     300

static esp_err_t mqtt_client_reconfigure(void);

static esp_mqtt_client_handle_t g_mqtt_client = NULL;
static bool g_started = false;
static bool g_connected = false;
static char g_topic_prefix[128] = {0};
static char g_device_id[64] = {0};
static char g_broker[256] = {0};
static char g_username[64] = {0};
static char g_password[64] = {0};

#if defined(CONFIG_MQTT_PROTOCOL_5)
/* Topic aliases replace a repeated topic string with a small integer. Aliases
 * are scoped to one connection, so the table is cleared on every reconnect and
 * the first send for a topic re-states the full name (the announcement). */
#define TOPIC_ALIAS_SLOTS 8

typedef struct {
    char     topic[80];
    uint16_t alias;
    bool     in_use;
    bool     announced;
} topic_alias_slot_t;

static topic_alias_slot_t g_alias_slots[TOPIC_ALIAS_SLOTS];
static uint16_t g_alias_max = 0;   /* broker's topic_alias_maximum; 0 = unsupported */
static uint16_t g_alias_next = 1;

static void alias_table_reset(void)
{
    memset(g_alias_slots, 0, sizeof(g_alias_slots));
    g_alias_next = 1;
}

static topic_alias_slot_t *alias_slot_for(const char *topic)
{
    if (g_alias_max == 0) {
        return NULL;
    }
    for (int i = 0; i < TOPIC_ALIAS_SLOTS; i++) {
        if (g_alias_slots[i].in_use && strcmp(g_alias_slots[i].topic, topic) == 0) {
            return &g_alias_slots[i];
        }
    }
    for (int i = 0; i < TOPIC_ALIAS_SLOTS; i++) {
        if (!g_alias_slots[i].in_use) {
            if (g_alias_next > g_alias_max) {
                return NULL;   /* broker allows fewer aliases than we have topics */
            }
            snprintf(g_alias_slots[i].topic, sizeof(g_alias_slots[i].topic), "%s", topic);
            g_alias_slots[i].alias = g_alias_next++;
            g_alias_slots[i].in_use = true;
            g_alias_slots[i].announced = false;
            ESP_LOGI(TAG, "Topic alias %u -> %s",
                     (unsigned)g_alias_slots[i].alias, g_alias_slots[i].topic);
            return &g_alias_slots[i];
        }
    }
    return NULL;
}

/* Build the {"fw","model","dev"} metadata attached to every message we send. */
static mqtt5_user_property_handle_t metadata_user_properties(void)
{
    esp_mqtt5_user_property_item_t items[3];
    uint8_t n = 0;

    items[n].key = "fw";
    items[n++].value = app_version();
    items[n].key = "model";
    items[n++].value = CONFIG_IDF_TARGET;
    items[n].key = "dev";
    items[n++].value = g_device_id;

    mqtt5_user_property_handle_t handle = NULL;
    if (esp_mqtt5_client_set_user_property(&handle, items, n) != ESP_OK) {
        return NULL;
    }
    return handle;
}
#endif /* CONFIG_MQTT_PROTOCOL_5 */

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

    /* The topic tree carries no device id in its leaf names (it is
     * "<prefix>/sensors", not "<prefix>/<id>/sensors"), so the prefix MUST be
     * unique per node. Two units sharing a prefix would interleave their
     * readings, state and command topics on the broker. Default to
     * "espx/<device_id>"; strip a leading "espx-" so the id is not repeated. */
    const char *node_id = g_device_id;
    if (strncmp(node_id, "espx-", 5) == 0) {
        node_id += 5;
    }
    snprintf(g_topic_prefix, sizeof(g_topic_prefix), "espx/%s", node_id);

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
            g_topic_prefix[sizeof(g_topic_prefix) - 1] = '\0';

            /* Catch the "one fixed prefix for the whole fleet" mistake early:
             * without the device id in the prefix every node collides. */
            if (strstr(g_topic_prefix, node_id) == NULL) {
                ESP_LOGW(TAG, "mqtt_topic_prefix '%s' does not contain this node's id "
                              "'%s' — multiple nodes will publish into the same "
                              "topics and overwrite each other",
                         g_topic_prefix, node_id);
            }
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

#if defined(CONFIG_MQTT_PROTOCOL_5)
        /* Aliases are per-connection: drop the previous table and learn what
         * this broker is willing to accept from the CONNACK. */
        alias_table_reset();
        g_alias_max = (event->property != NULL)
                          ? event->property->server.topic_alias_maximum
                          : 0;
        ESP_LOGI(TAG, "Broker topic alias maximum: %u", (unsigned)g_alias_max);
#endif

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

        // Publish online state through the common path so it carries the
        // MQTT 5 metadata, message expiry and (when supported) a topic alias.
        char payload[128];
        snprintf(payload, sizeof(payload),
                 "{\"online\":true,\"device_id\":\"%s\",\"uptime\":%lu}",
                 g_device_id, (unsigned long)(esp_timer_get_time() / 1000000ULL));
        mqtt_client_publish("state", payload, strlen(payload), 1, true);
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

static void mqtt_on_config_changed(const event_t *event, void *user_data)
{
    (void)event;
    (void)user_data;
    // Reload device_id (may have been updated), then reconfigure MQTT
    strncpy(g_device_id, node_config_get_device_id(), sizeof(g_device_id) - 1);
    mqtt_client_reconfigure();
}

esp_err_t mqtt_client_init(void)
{
    strncpy(g_device_id, node_config_get_device_id(), sizeof(g_device_id) - 1);
    load_network_config();
    // Reconfigure when network config changes via API/MQTT
    event_bus_subscribe(EVENT_CONFIG_CHANGED, mqtt_on_config_changed, NULL);
    ESP_LOGI(TAG, "MQTT client initialized, device_id: %s", g_device_id);
    return ESP_OK;
}

/**
 * @brief Check if the broker URL is the default/placeholder (unconfigured)
 */
static bool is_broker_unconfigured(const char *broker)
{
    if (broker == NULL || broker[0] == '\0') {
        return true;
    }
    /* Treat the example.com placeholder as "not configured" */
    if (strstr(broker, "broker.example.com") != NULL) {
        return true;
    }
    return false;
}

esp_err_t mqtt_client_start(void)
{
    if (g_started && g_mqtt_client != NULL) {
        return ESP_OK;
    }

    // (Re)load configuration
    load_network_config();

    /* Skip connection attempt if broker is not configured */
    if (is_broker_unconfigured(g_broker)) {
        ESP_LOGW(TAG, "MQTT broker not configured — skipping connection. "
                      "Set mqtt_broker in /api/config or network config.");
        return ESP_ERR_INVALID_ARG;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = g_broker,
        .credentials.client_id = g_device_id,
        .session.keepalive = 60,
        .session.disable_clean_session = false,
    };

    /* MQTT 5 gives us reason codes on failure instead of a bare 0x04/0x05.
     * Guarded so a build with the protocol disabled still falls back to the
     * library default (3.1.1) rather than failing to start. */
#if defined(CONFIG_MQTT_PROTOCOL_5)
    mqtt_cfg.session.protocol_ver = MQTT_PROTOCOL_V_5;
#endif

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

#if defined(CONFIG_MQTT_PROTOCOL_5)
    /* CONNECT properties must be set before start(). User properties here are
     * visible to the broker/rule engine for every message of the session;
     * session expiry bounds how long undelivered commands may wait for us. */
    esp_mqtt5_connection_property_config_t conn_props = {
        .session_expiry_interval = MQTT_SESSION_EXPIRY_S,
        .user_property = metadata_user_properties(),
    };
    if (esp_mqtt5_client_set_connect_property(g_mqtt_client, &conn_props) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set MQTT 5 connect properties");
    }
    if (conn_props.user_property != NULL) {
        /* The client copied the list; our handle is no longer needed. */
        esp_mqtt5_client_delete_user_property(conn_props.user_property);
    }
#endif

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

/**
 * @brief Stop, reload config from NVS, and start the MQTT client again.
 *        Used when network settings change at runtime.
 */
static esp_err_t mqtt_client_reconfigure(void)
{
    ESP_LOGI(TAG, "Reconfiguring MQTT client due to config change");
    if (g_mqtt_client != NULL) {
        esp_mqtt_client_stop(g_mqtt_client);
        esp_mqtt_client_destroy(g_mqtt_client);
        g_mqtt_client = NULL;
    }
    g_started = false;
    g_connected = false;
    return mqtt_client_start();
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

#if defined(CONFIG_MQTT_PROTOCOL_5)
    esp_mqtt5_publish_property_config_t props = {
        .message_expiry_interval = MQTT_MSG_EXPIRY_S,
        .user_property = metadata_user_properties(),
    };

    /* Once an alias has been announced on this connection, the topic name can
     * be dropped from the packet entirely and the broker resolves the alias. */
    const char *wire_topic = topic;
    topic_alias_slot_t *slot = alias_slot_for(topic);
    if (slot != NULL) {
        props.topic_alias = slot->alias;
        if (slot->announced) {
            wire_topic = "";
        }
    }

    bool alias_shortened = (slot != NULL && slot->announced);

    int msg_id = -1;
    if (esp_mqtt5_client_set_publish_property(g_mqtt_client, &props) == ESP_OK) {
        msg_id = esp_mqtt_client_publish(g_mqtt_client, wire_topic, data, len,
                                         qos, retain ? 1 : 0);
        if (msg_id >= 0 && slot != NULL) {
            slot->announced = true;
        }
    }

    if (props.user_property != NULL) {
        esp_mqtt5_client_delete_user_property(props.user_property);
    }

    if (msg_id < 0) {
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Published to %s%s, msg_id=%d", topic,
             alias_shortened ? " (alias)" : "", msg_id);
    return ESP_OK;
#else
    int msg_id = esp_mqtt_client_publish(g_mqtt_client, topic, data, len, qos, retain ? 1 : 0);
    if (msg_id < 0) {
        return ESP_FAIL;
    }
    ESP_LOGD(TAG, "Published to %s, msg_id=%d", topic, msg_id);
    return ESP_OK;
#endif
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
