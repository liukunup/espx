/**
 * @file mqtt_commander.c
 * @brief MQTT command handler
 *
 * Handles commands from MQTT:
 *   {prefix}/cmd/query/{device_id}      - {"action":"get"} -> publish current value
 *   {prefix}/cmd/control/{device_id}    - {"action":"set","value":...}
 *   {prefix}/cmd/config                - {"action":"get_devices"|"set_devices",...}
 *   {prefix}/cmd/reboot                - reboot device
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_system.h>
#include <cJSON.h>

#include "mqtt_commander.h"
#include "espx_mqtt_client.h"
#include "device_manager.h"
#include "device_type.h"
#include "node_config.h"
#include "ota_service/ota_service.h"
#include "test_mode/test_mode.h"
#include "config_apply.h"
#include "yaml.h"

static const char *TAG = "mqtt_commander";

/**
 * @brief Extract device_id from topic after prefix/cmd/query/ or cmd/control/
 */
static bool extract_cmd_target(const char *topic, const char *cmd, char *out, size_t out_size)
{
    const char *prefix = mqtt_client_get_prefix();
    size_t prefix_len = strlen(prefix);

    if (strncmp(topic, prefix, prefix_len) != 0) {
        return false;
    }

    const char *p = topic + prefix_len;
    if (*p != '/') return false;
    p++;

    // Skip "cmd/"
    if (strncmp(p, "cmd/", 4) != 0) return false;
    p += 4;

    // Skip command name
    size_t cmd_len = strlen(cmd);
    if (strncmp(p, cmd, cmd_len) != 0) return false;
    p += cmd_len;

    // Skip '/'
    if (*p != '/') return false;
    p++;

    // Copy rest to out
    strncpy(out, p, out_size - 1);
    out[out_size - 1] = '\0';
    return true;
}

/**
 * @brief Check if topic matches a command pattern
 */
static bool topic_matches_cmd(const char *topic, const char *cmd)
{
    const char *prefix = mqtt_client_get_prefix();
    size_t prefix_len = strlen(prefix);

    if (strncmp(topic, prefix, prefix_len) != 0) {
        return false;
    }

    const char *p = topic + prefix_len;
    if (*p != '/') return false;
    p++;

    if (strncmp(p, "cmd/", 4) != 0) return false;
    p += 4;

    return strncmp(p, cmd, strlen(cmd)) == 0;
}

/**
 * @brief Handle query command
 */
static void handle_query(const char *device_id, cJSON *data)
{
    cJSON *value = cJSON_CreateObject();

    if (device_id == NULL || strlen(device_id) == 0 || strcmp(device_id, "all") == 0) {
        // Query all devices
        device_read_all(value);
    } else {
        // Query specific device
        device_read(device_id, value);
    }

    char *json_str = cJSON_PrintUnformatted(value);
    if (json_str) {
        char topic[128];
        snprintf(topic, sizeof(topic), "attrs/%s", device_id ? device_id : "all");
        mqtt_client_publish(topic, json_str, strlen(json_str), 1, false);
        free(json_str);
    }

    cJSON_Delete(value);
}

/**
 * @brief Handle control command
 */
static void handle_control(const char *device_id, cJSON *data)
{
    if (device_id == NULL || strlen(device_id) == 0) {
        ESP_LOGW(TAG, "Control command missing device_id");
        return;
    }

    cJSON *action = cJSON_GetObjectItem(data, "action");
    if (!cJSON_IsString(action)) {
        ESP_LOGW(TAG, "Control command missing action");
        return;
    }

    if (strcmp(action->valuestring, "set") == 0) {
        cJSON *value = cJSON_GetObjectItem(data, "value");
        if (value == NULL) {
            ESP_LOGW(TAG, "Set command missing value");
            return;
        }

        esp_err_t err = device_write(device_id, value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write %s: %s", device_id, esp_err_to_name(err));

            // Publish error
            char payload[256];
            snprintf(payload, sizeof(payload),
                     "{\"device\":\"%s\",\"error\":\"%s\"}",
                     device_id, esp_err_to_name(err));
            mqtt_client_publish("attrs/error", payload, strlen(payload), 1, false);
        }
    } else if (strcmp(action->valuestring, "get") == 0) {
        handle_query(device_id, data);
    } else {
        ESP_LOGW(TAG, "Unknown action: %s", action->valuestring);
    }
}

/**
 * @brief Handle OTA command
 *
 * {"action":"start","url":"http://host/espx.patch"}
 * {"action":"status"}
 * {"action":"cancel"}
 */
static void handle_ota(cJSON *data)
{
    cJSON *action = cJSON_GetObjectItem(data, "action");
    const char *act = cJSON_IsString(action) ? action->valuestring : "status";

    if (strcmp(act, "start") == 0) {
        cJSON *url = cJSON_GetObjectItem(data, "url");
        if (!cJSON_IsString(url) || url->valuestring[0] == '\0') {
            const char *e = "{\"error\":\"missing url\"}";
            mqtt_client_publish("ota/status", e, strlen(e), 1, false);
            return;
        }

        esp_err_t err = ota_service_start(url->valuestring);
        char resp[192];
        snprintf(resp, sizeof(resp), "{\"started\":%s,\"error\":\"%s\"}",
                 err == ESP_OK ? "true" : "false",
                 err == ESP_OK ? "" : esp_err_to_name(err));
        mqtt_client_publish("ota/status", resp, strlen(resp), 1, false);

    } else if (strcmp(act, "cancel") == 0) {
        ota_service_cancel();
        const char *c = "{\"cancelled\":true}";
        mqtt_client_publish("ota/status", c, strlen(c), 1, false);

    } else {
        ota_status_t st;
        ota_service_get_status(&st);

        static const char *names[] = {
            "IDLE", "CONNECTING", "DOWNLOADING", "VERIFYING",
            "APPLYING", "REBOOTING", "SUCCESS", "FAILED"
        };

        cJSON *out = cJSON_CreateObject();
        cJSON_AddStringToObject(out, "state",
                                names[st.state <= OTA_STATE_FAILED ? st.state : OTA_STATE_FAILED]);
        cJSON_AddNumberToObject(out, "progress", st.progress);
        cJSON_AddStringToObject(out, "running_version", st.running_version);
        char *s = cJSON_PrintUnformatted(out);
        if (s) {
            mqtt_client_publish("ota/status", s, strlen(s), 1, false);
            free(s);
        }
        cJSON_Delete(out);
    }
}

/**
 * @brief Publish a JSON object to a subtopic
 */
static void publish_json(const char *subtopic, cJSON *obj)
{
    if (obj == NULL) return;
    char *s = cJSON_PrintUnformatted(obj);
    if (s) {
        mqtt_client_publish(subtopic, s, strlen(s), 1, false);
        free(s);
    }
}

/**
 * @brief Apply a configuration document pushed over MQTT
 *
 * Accepted on {prefix}/cmd/config. The payload may be YAML or JSON.
 *
 * YAML:
 *   node:
 *     name: Line-1
 *   network:
 *     mqtt_broker: mqtt://host:1883
 *   devices:
 *     - id: relay_a
 *       type: relay
 *       config: {gpio: 5, active_level: 1}
 *   remove_devices: [old1]
 *   replace_devices: false
 *
 * The result is published to {prefix}/config/result.
 */
static void handle_config_push(const char *payload)
{
    config_apply_result_t res;
    char err[128] = {0};

    esp_err_t err_code = config_apply_payload(payload, &res, err, sizeof(err));

    cJSON *out = cJSON_CreateObject();
    cJSON_AddBoolToObject(out, "ok", err_code == ESP_OK && res.devices_failed == 0);

    cJSON *counts = cJSON_AddObjectToObject(out, "applied");
    cJSON_AddNumberToObject(counts, "added", res.devices_added);
    cJSON_AddNumberToObject(counts, "updated", res.devices_updated);
    cJSON_AddNumberToObject(counts, "removed", res.devices_removed);
    cJSON_AddNumberToObject(counts, "failed", res.devices_failed);

    cJSON_AddBoolToObject(out, "node_changed", res.node_changed);
    cJSON_AddBoolToObject(out, "network_changed", res.network_changed);
    cJSON_AddBoolToObject(out, "reboot_required", res.reboot_recommended);

    const char *msg = res.error[0] ? res.error : err;
    if (msg[0]) {
        cJSON_AddStringToObject(out, "error", msg);
    }

    publish_json("config/result", out);
    cJSON_Delete(out);
}

/**
 * @brief Handle config command
 */
static void handle_config(cJSON *data)
{
    cJSON *action = cJSON_GetObjectItem(data, "action");

    /* A configuration document has no "action" key: it is a YAML/JSON push.
     * A payload WITH "action" keeps the legacy introspection verbs. */
    if (!cJSON_IsString(action)) {
        return;
    }

    if (strcmp(action->valuestring, "apply") == 0) {
        cJSON *doc = cJSON_GetObjectItem(data, "config");
        if (doc == NULL) {
            const char *e = "{\"ok\":false,\"error\":\"missing 'config'\"}";
            mqtt_client_publish("config/result", e, strlen(e), 1, false);
            return;
        }
        char *text = cJSON_PrintUnformatted(doc);
        if (text) {
            handle_config_push(text);
            free(text);
        }
        return;
    }

    if (strcmp(action->valuestring, "get_config") == 0) {
        cJSON *cfg = config_export();
        publish_json("attrs/config", cfg);
        cJSON_Delete(cfg);
        return;
    }

    if (strcmp(action->valuestring, "get_devices") == 0) {
        cJSON *array = cJSON_CreateArray();
        device_get_json_array(array);
        char *json_str = cJSON_PrintUnformatted(array);
        if (json_str) {
            mqtt_client_publish("attrs/devices", json_str, strlen(json_str), 1, false);
            free(json_str);
        }
        cJSON_Delete(array);
    } else if (strcmp(action->valuestring, "get_device_types") == 0) {
        cJSON *array = cJSON_CreateArray();

        for (size_t i = 0; i < device_type_count(); i++) {
            const device_type_t *t = device_type_get_by_index(i);
            if (t == NULL) continue;

            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "name", t->name);
            cJSON_AddStringToObject(item, "description", t->description);
            cJSON_AddNumberToObject(item, "capabilities", t->capabilities);

            cJSON *default_config = cJSON_CreateObject();
            if (t->get_default_config) {
                t->get_default_config(default_config);
            }
            cJSON_AddItemToObject(item, "default_config", default_config);

            cJSON_AddItemToArray(array, item);
        }

        char *json_str = cJSON_PrintUnformatted(array);
        if (json_str) {
            mqtt_client_publish("attrs/device_types", json_str, strlen(json_str), 1, false);
            free(json_str);
        }
        cJSON_Delete(array);
    } else if (strcmp(action->valuestring, "reboot") == 0) {
        // Reboot device
        ESP_LOGI(TAG, "Reboot requested via MQTT");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    } else if (strcmp(action->valuestring, "testmode") == 0) {
        ESP_LOGW(TAG, "Test mode requested via MQTT");
        test_mode_request();
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
    }
}

/**
 * @brief Handle reboot command
 */
static void handle_reboot(cJSON *data)
{
    ESP_LOGI(TAG, "Reboot requested");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

esp_err_t mqtt_commander_init(void)
{
    ESP_LOGI(TAG, "MQTT commander initialized");
    return ESP_OK;
}

void mqtt_commander_handle(const char *topic, const char *payload, int payload_len)
{
    ESP_LOGI(TAG, "Handle command: %s", topic);

    // Parse payload
    cJSON *data = NULL;
    if (payload_len > 0) {
        char *payload_str = strndup(payload, payload_len);
        if (payload_str) {
            data = cJSON_Parse(payload_str);
            free(payload_str);
        }
    }
    if (data == NULL) {
        data = cJSON_CreateObject();
    }

    // Route based on topic
    char target[64];

    if (topic_matches_cmd(topic, "query/")) {
        if (extract_cmd_target(topic, "query", target, sizeof(target))) {
            handle_query(target, data);
        }
    } else if (topic_matches_cmd(topic, "control/")) {
        if (extract_cmd_target(topic, "control", target, sizeof(target))) {
            handle_control(target, data);
        }
    } else if (topic_matches_cmd(topic, "config")) {
        /* A YAML (or JSON) configuration document has no "action" member and is
         * applied directly; a payload with "action" uses the legacy verbs. */
        if (yaml_looks_like_yaml(payload)) {
            handle_config_push(payload);
        } else {
            handle_config(data);
        }
    } else if (topic_matches_cmd(topic, "ota")) {
        handle_ota(data);
    } else if (topic_matches_cmd(topic, "reboot")) {
        handle_reboot(data);
    } else {
        ESP_LOGW(TAG, "Unknown command topic: %s", topic);
    }

    cJSON_Delete(data);
}
