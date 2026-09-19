/**
 * @file sensor_manager.c
 * @brief Sensor Manager implementation
 */

#include "sensor_manager.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "sensor_mgr";

/** @brief Maximum number of sensors */
#define MAX_SENSORS 16

/** @brief Sensor entry */
typedef struct {
    char name[32];
    sensor_type_t type;
    sensor_read_fn read_fn;
    char unit[8];
    float calibration_offset;
    float calibration_scale;
    float last_value;
    uint32_t last_read;
    bool valid;
    bool registered;
} sensor_entry_t;

/** @brief Sensor table */
static sensor_entry_t g_sensors[MAX_SENSORS];

/** @brief Whether initialized */
static bool g_initialized = false;

int sensor_manager_init(void) {
    if (g_initialized) {
        ESP_LOGW(TAG, "Sensor manager already initialized");
        return 0;
    }

    memset(g_sensors, 0, sizeof(g_sensors));
    g_initialized = true;

    ESP_LOGI(TAG, "Sensor manager initialized");
    return 0;
}

int sensor_register(const sensor_config_t *config) {
    if (config == NULL || config->name[0] == '\0' || config->read_fn == NULL) {
        return -1;
    }

    // Find slot or existing sensor
    int slot = -1;
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (!g_sensors[i].registered) {
            slot = i;
            break;
        }
        if (strcmp(g_sensors[i].name, config->name) == 0) {
            // Update existing
            g_sensors[i].type = config->type;
            g_sensors[i].read_fn = config->read_fn;
            strncpy(g_sensors[i].unit, config->unit, sizeof(g_sensors[i].unit) - 1);
            g_sensors[i].calibration_offset = config->calibration_offset;
            g_sensors[i].calibration_scale = config->calibration_scale;
            ESP_LOGI(TAG, "Updated sensor: %s", config->name);
            return 0;
        }
    }

    if (slot < 0) {
        ESP_LOGE(TAG, "No free sensor slot");
        return -2;
    }

    sensor_entry_t *s = &g_sensors[slot];
    strncpy(s->name, config->name, sizeof(s->name) - 1);
    s->name[sizeof(s->name) - 1] = '\0';
    s->type = config->type;
    s->read_fn = config->read_fn;
    strncpy(s->unit, config->unit, sizeof(s->unit) - 1);
    s->unit[sizeof(s->unit) - 1] = '\0';
    s->calibration_offset = config->calibration_offset;
    s->calibration_scale = config->calibration_scale > 0 ? config->calibration_scale : 1.0f;
    s->valid = false;
    s->registered = true;

    ESP_LOGI(TAG, "Registered sensor: %s (type=%d)", config->name, config->type);
    return 0;
}

int sensor_read(const char *name, float *value) {
    if (name == NULL || value == NULL) {
        return -1;
    }

    for (int i = 0; i < MAX_SENSORS; i++) {
        if (g_sensors[i].registered && strcmp(g_sensors[i].name, name) == 0) {
            sensor_entry_t *s = &g_sensors[i];

            if (s->read_fn == NULL) {
                return -2;
            }

            int ret = s->read_fn(value);
            if (ret == 0) {
                // Apply calibration
                *value = (*value * s->calibration_scale) + s->calibration_offset;
                s->last_value = *value;
                s->last_read = (uint32_t)(esp_timer_get_time() / 1000);
                s->valid = true;
            } else {
                s->valid = false;
            }

            return ret;
        }
    }

    return -3;  // Not found
}

int sensor_read_all(sensor_data_t *data, int max_count, int *actual_count) {
    if (data == NULL || max_count <= 0 || actual_count == NULL) {
        return -1;
    }

    int count = 0;
    for (int i = 0; i < MAX_SENSORS && count < max_count; i++) {
        if (g_sensors[i].registered) {
            sensor_data_t *d = &data[count];
            strncpy(d->name, g_sensors[i].name, sizeof(d->name) - 1);
            d->name[sizeof(d->name) - 1] = '\0';
            d->type = g_sensors[i].type;
            d->value = g_sensors[i].last_value;
            strncpy(d->unit, g_sensors[i].unit, sizeof(d->unit) - 1);
            d->unit[sizeof(d->unit) - 1] = '\0';
            d->last_read = g_sensors[i].last_read;
            d->valid = g_sensors[i].valid;

            // Read current value
            sensor_read(g_sensors[i].name, &d->value);

            count++;
        }
    }

    *actual_count = count;
    return 0;
}

int sensor_get_config(const char *name, sensor_config_t *config) {
    if (name == NULL || config == NULL) {
        return -1;
    }

    for (int i = 0; i < MAX_SENSORS; i++) {
        if (g_sensors[i].registered && strcmp(g_sensors[i].name, name) == 0) {
            strncpy(config->name, g_sensors[i].name, sizeof(config->name) - 1);
            config->type = g_sensors[i].type;
            config->read_fn = g_sensors[i].read_fn;
            strncpy(config->unit, g_sensors[i].unit, sizeof(config->unit) - 1);
            config->calibration_offset = g_sensors[i].calibration_offset;
            config->calibration_scale = g_sensors[i].calibration_scale;
            return 0;
        }
    }

    return -2;  // Not found
}

int sensor_set_calibration(const char *name, float offset, float scale) {
    if (name == NULL) {
        return -1;
    }

    for (int i = 0; i < MAX_SENSORS; i++) {
        if (g_sensors[i].registered && strcmp(g_sensors[i].name, name) == 0) {
            g_sensors[i].calibration_offset = offset;
            g_sensors[i].calibration_scale = scale > 0 ? scale : 1.0f;
            ESP_LOGI(TAG, "Set calibration for %s: offset=%.3f, scale=%.3f",
                     name, offset, scale);
            return 0;
        }
    }

    return -2;  // Not found
}
