/**
 * @file config_apply.c
 * @brief Apply a configuration document (YAML or JSON) to the node
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <esp_log.h>
#include <cJSON.h>

#include "config_apply.h"
#include "yaml.h"
#include "node_config.h"
#include "device_manager.h"
#include "device_type.h"
#include "mqtt_client/espx_mqtt_client.h"
#include "json_utils.h"
#include "str_utils.h"

static const char *TAG = "config_apply";

/* Keys accepted inside `network` */
static const char *NETWORK_KEYS[] = {
    "wifi_ssid", "wifi_password",
    "mqtt_broker", "mqtt_username", "mqtt_password", "mqtt_topic_prefix",
};

/* Keys accepted inside `node` */
static const char *NODE_KEYS[] = { "device_id", "name" };

/* -------------------------------------------------------------------------- */

cJSON *config_parse_document(const char *payload, int *err_line)
{
    if (payload == NULL) {
        return NULL;
    }

    if (err_line) *err_line = 0;

    /* Try JSON first: it is unambiguous and cheap to reject.
     *
     * Do NOT discriminate on the first character. A YAML document written in
     * flow style starts with '{' or '[' exactly like JSON, so "starts with a
     * brace means JSON" misclassifies it and the document fails to parse. */
    cJSON *doc = cJSON_Parse(payload);
    if (doc != NULL) {
        return doc;
    }

    int line = 0;
    const char *msg = NULL;
    doc = yaml_parse_ex(payload, &line, &msg);
    if (doc == NULL) {
        ESP_LOGE(TAG, "payload is neither valid JSON nor YAML (line %d: %s)",
                 line, msg ? msg : "?");
    }
    if (err_line) *err_line = line;
    return doc;
}

/* -------------------------------------------------------------------------- */

static bool is_secret(const char *key)
{
    return str_contains(key, "password") || str_contains(key, "key");
}

/**
 * @brief Merge a flat object into a sub-object of the node config
 *
 * @param target_container node_config document (rewritten)
 * @param section          "node" or "network"
 * @param incoming         values to merge
 * @return true when something changed
 */
static bool merge_section(cJSON *target, const char *section,
                          const cJSON *incoming, const char **allowed, size_t allowed_n)
{
    if (!cJSON_IsObject(incoming)) {
        return false;
    }

    cJSON *dst = cJSON_GetObjectItem(target, section);
    if (!cJSON_IsObject(dst)) {
        dst = cJSON_AddObjectToObject(target, section);
    }

    bool changed = false;

    const cJSON *item = NULL;
    cJSON_ArrayForEach(item, incoming) {
        if (item->string == NULL) continue;

        bool ok = false;
        for (size_t i = 0; i < allowed_n; i++) {
            if (strcmp(allowed[i], item->string) == 0) { ok = true; break; }
        }
        if (!ok) {
            ESP_LOGW(TAG, "Ignoring unknown %s key '%s'", section, item->string);
            continue;
        }

        if (!cJSON_IsString(item) && !cJSON_IsNumber(item) && !cJSON_IsBool(item)) {
            ESP_LOGW(TAG, "Ignoring non-scalar %s.%s", section, item->string);
            continue;
        }

        /* node/network values are all textual. A YAML scalar that happens to
         * look numeric (an all-digit password, a numeric device name) would
         * otherwise be stored as a number and then silently ignored by the
         * readers, which require a string. Coerce to text. */
        cJSON *value = cJSON_IsString(item) ? cJSON_Duplicate((cJSON *)item, true)
                                            : cJSON_CreateString(cJSON_IsTrue(item) ? "true"
                                                 : cJSON_IsFalse(item) ? "false"
                                                 : (char *)cJSON_PrintUnformatted((cJSON *)item));
        if (value == NULL) {
            ESP_LOGW(TAG, "Ignoring %s.%s (out of memory)", section, item->string);
            continue;
        }

        cJSON *prev = cJSON_GetObjectItem(dst, item->string);
        /* compare by rendered value to avoid rewriting identical config */
        char *a = prev ? cJSON_PrintUnformatted(prev) : NULL;
        char *b = cJSON_PrintUnformatted(value);
        bool same = (a && b && strcmp(a, b) == 0);
        free(a); free(b);

        if (same) {
            cJSON_Delete(value);
            continue;
        }

        if (prev) cJSON_DeleteItemFromObject(dst, item->string);
        cJSON_AddItemToObject(dst, item->string, value);
        changed = true;

        if (is_secret(item->string)) {
            ESP_LOGI(TAG, "  %s.%s = <set>", section, item->string);
        } else {
            ESP_LOGI(TAG, "  %s.%s = %s", section, item->string,
                     value->valuestring ? value->valuestring : "?");
        }
    }

    return changed;
}

/**
 * @brief Upsert one device entry
 */
static esp_err_t upsert_device(const cJSON *entry, bool *added, char *err, size_t err_len)
{
    const char *id = json_get_string(entry, "id", NULL);
    const char *type = json_get_string(entry, "type", NULL);
    cJSON *config = cJSON_GetObjectItem(entry, "config");
    cJSON *enabled = cJSON_GetObjectItem(entry, "enabled");

    if (id == NULL || id[0] == '\0') {
        snprintf(err, err_len, "device entry without an id");
        return ESP_ERR_INVALID_ARG;
    }
    if (type == NULL || type[0] == '\0') {
        snprintf(err, err_len, "device '%s' without a type", id);
        return ESP_ERR_INVALID_ARG;
    }

    bool exists = (device_get(id) != NULL);

    if (config != NULL && !cJSON_IsObject(config)) {
        snprintf(err, err_len, "device '%s' config must be a mapping", id);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;
    if (exists) {
        ret = device_update_config(id, config);
        if (ret == ESP_OK) *added = false;
    } else {
        ret = device_add(id, type, config);
        if (ret == ESP_OK) *added = true;
    }

    if (ret != ESP_OK) {
        snprintf(err, err_len, "device '%s': %s", id, esp_err_to_name(ret));
        return ret;
    }

    /* device_add() always enables; honour an explicit false afterwards */
    if (enabled != NULL && cJSON_IsBool(enabled)) {
        device_set_enabled(id, cJSON_IsTrue(enabled));
    }

    return ESP_OK;
}

esp_err_t config_apply(const cJSON *doc, config_apply_result_t *result)
{
    config_apply_result_t local = {0};
    if (result == NULL) result = &local;

    if (!cJSON_IsObject(doc)) {
        snprintf(result->error, sizeof(result->error), "configuration must be a mapping");
        return ESP_ERR_INVALID_ARG;
    }

    /* ---- node / network ------------------------------------------------ */
    cJSON *node_doc = cJSON_GetObjectItem(doc, "node");
    cJSON *net_doc = cJSON_GetObjectItem(doc, "network");
    cJSON *devices = cJSON_GetObjectItem(doc, "devices");
    cJSON *remove = cJSON_GetObjectItem(doc, "remove_devices");
    cJSON *replace = cJSON_GetObjectItem(doc, "replace_devices");

    if (node_doc != NULL || net_doc != NULL) {
        cJSON *cfg = node_config_get();
        if (cfg == NULL) cfg = cJSON_CreateObject();

        if (node_doc != NULL) {
            result->node_changed = merge_section(cfg, "node", node_doc,
                                                 NODE_KEYS,
                                                 sizeof(NODE_KEYS) / sizeof(NODE_KEYS[0]));
        }
        if (net_doc != NULL) {
            result->network_changed = merge_section(cfg, "network", net_doc,
                                                    NETWORK_KEYS,
                                                    sizeof(NETWORK_KEYS) / sizeof(NETWORK_KEYS[0]));
        }

        esp_err_t err = node_config_set(cfg);
        cJSON_Delete(cfg);
        if (err != ESP_OK) {
            snprintf(result->error, sizeof(result->error),
                     "failed to persist node config: %s", esp_err_to_name(err));
            return err;
        }
    }

    /* ---- removals ------------------------------------------------------ */
    if (cJSON_IsArray(remove)) {
        cJSON *it = NULL;
        cJSON_ArrayForEach(it, remove) {
            if (cJSON_IsString(it)) {
                if (device_remove(it->valuestring) == ESP_OK) {
                    result->devices_removed++;
                    ESP_LOGI(TAG, "  removed device '%s'", it->valuestring);
                }
            }
        }
    }

    /* ---- device upserts ------------------------------------------------ */
    if (devices != NULL) {
        if (!cJSON_IsArray(devices)) {
            snprintf(result->error, sizeof(result->error), "'devices' must be a list");
            return ESP_ERR_INVALID_ARG;
        }

        cJSON *entry = NULL;
        cJSON_ArrayForEach(entry, devices) {
            bool added = false;
            char err[96] = {0};

            if (upsert_device(entry, &added, err, sizeof(err)) == ESP_OK) {
                if (added) result->devices_added++;
                else       result->devices_updated++;
            } else {
                result->devices_failed++;
                if (result->error[0] == '\0') {
                    snprintf(result->error, sizeof(result->error), "%s", err);
                }
                ESP_LOGE(TAG, "  %s", err);
            }
        }
    }

    /* ---- replace: drop devices that were not listed -------------------- */
    if (cJSON_IsTrue(replace)) {
        /* Collect the ids present in the document */
        const char *keep[64];
        size_t keep_n = 0;
        if (cJSON_IsArray(devices)) {
            cJSON *entry = NULL;
            cJSON_ArrayForEach(entry, devices) {
                const char *id = json_get_string(entry, "id", NULL);
                if (id != NULL && keep_n < sizeof(keep) / sizeof(keep[0])) {
                    keep[keep_n++] = id;
                }
            }
        }

        /* Walk backwards: device_remove() shifts the index array */
        for (size_t i = device_get_count(); i > 0; i--) {
            const device_t *dev = device_get_by_index(i - 1);
            if (dev == NULL) continue;

            bool listed = false;
            for (size_t k = 0; k < keep_n; k++) {
                if (strcmp(keep[k], dev->id) == 0) { listed = true; break; }
            }
            if (listed) continue;

            char id[sizeof(dev->id)];
            str_copy(id, sizeof(id), dev->id);

            if (device_remove(id) == ESP_OK) {
                result->devices_removed++;
                ESP_LOGI(TAG, "  removed unlisted device '%s'", id);
            }
        }
    }

    /* Persist regardless: a device that failed to init is still recorded so it
     * can be retried after the wiring is fixed. */
    if (devices != NULL || remove != NULL || cJSON_IsTrue(replace)) {
        device_manager_save();
    }

    result->reboot_recommended = result->network_changed;

    ESP_LOGI(TAG, "config applied: +%d ~%d -%d (failed %d)%s",
             result->devices_added, result->devices_updated, result->devices_removed,
             result->devices_failed,
             result->reboot_recommended ? ", reboot to apply network changes" : "");

    return ESP_OK;
}

esp_err_t config_apply_payload(const char *payload, config_apply_result_t *result,
                               char *err_buf, size_t err_buf_len)
{
    config_apply_result_t local = {0};
    if (result == NULL) result = &local;
    memset(result, 0, sizeof(*result));

    int err_line = 0;
    cJSON *doc = config_parse_document(payload, &err_line);
    if (doc == NULL) {
        snprintf(result->error, sizeof(result->error),
                 err_line > 0 ? "YAML syntax error at line %d" : "invalid YAML or JSON",
                 err_line);
        if (err_buf && err_buf_len) {
            snprintf(err_buf, err_buf_len, "%s", result->error);
        }
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = config_apply(doc, result);
    cJSON_Delete(doc);

    if (err != ESP_OK && err_buf && err_buf_len) {
        snprintf(err_buf, err_buf_len, "%s", result->error);
    }
    return err;
}

cJSON *config_export(void)
{
    cJSON *cfg = node_config_get();
    if (cfg == NULL) cfg = cJSON_CreateObject();

    cJSON *devices = cJSON_CreateArray();
    device_get_json_array(devices);
    cJSON_AddItemToObject(cfg, "devices", devices);

    return cfg;
}
