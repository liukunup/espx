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
 * @brief Handle config command
 */
static void handle_config(cJSON *data)
{
    cJSON *action = cJSON_GetObjectItem(data, "action");
    if (!cJSON_IsString(action)) {
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
        size_t count;
        const device_type_t *types = device_type_get_all(&count);

        cJSON *array = cJSON_CreateArray();
        for (size_t i = 0; i < count; i++) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "name", types[i].name);
            cJSON_AddStringToObject(item, "description", types[i].description);
            cJSON_AddNumberToObject(item, "capabilities", types[i].capabilities);

            cJSON *default_config = cJSON_CreateObject();
            if (types[i].get_default_config) {
                types[i].get_default_config(default_config);
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
        handle_config(data);
    } else if (topic_matches_cmd(topic, "reboot")) {
        handle_reboot(data);
    } else {
        ESP_LOGW(TAG, "Unknown command topic: %s", topic);
    }

    cJSON_Delete(data);
}
