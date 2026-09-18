/**
 * @file actuator_manager.c
 * @brief Actuator Manager implementation
 */

#include "actuator_manager.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "actuator_mgr";

/** @brief Maximum number of actuators */
#define MAX_ACTUATORS 16

/** @brief Actuator entry */
typedef struct {
    char name[32];
    actuator_type_t type;
    actuator_set_fn set_fn;
    int min_value;
    int max_value;
    int current_value;
    bool registered;
} actuator_entry_t;

/** @brief Actuator table */
static actuator_entry_t g_actuators[MAX_ACTUATORS];

/** @brief Whether initialized */
static bool g_initialized = false;

int actuator_manager_init(void) {
    if (g_initialized) {
        ESP_LOGW(TAG, "Actuator manager already initialized");
        return 0;
    }

    memset(g_actuators, 0, sizeof(g_actuators));
    g_initialized = true;

    ESP_LOGI(TAG, "Actuator manager initialized");
    return 0;
}

int actuator_register(const actuator_config_t *config) {
    if (config == NULL || config->name == NULL || config->set_fn == NULL) {
        return -1;
    }

    // Find slot or existing
    int slot = -1;
    for (int i = 0; i < MAX_ACTUATORS; i++) {
        if (!g_actuators[i].registered) {
            slot = i;
            break;
        }
        if (strcmp(g_actuators[i].name, config->name) == 0) {
            // Update existing
            g_actuators[i].type = config->type;
            g_actuators[i].set_fn = config->set_fn;
            g_actuators[i].min_value = config->min_value;
            g_actuators[i].max_value = config->max_value;
            ESP_LOGI(TAG, "Updated actuator: %s", config->name);
            return 0;
        }
    }

    if (slot < 0) {
        ESP_LOGE(TAG, "No free actuator slot");
        return -2;
    }

    actuator_entry_t *a = &g_actuators[slot];
    strncpy(a->name, config->name, sizeof(a->name) - 1);
    a->name[sizeof(a->name) - 1] = '\0';
    a->type = config->type;
    a->set_fn = config->set_fn;
    a->min_value = config->min_value;
    a->max_value = config->max_value;
    a->current_value = 0;
    a->registered = true;

    ESP_LOGI(TAG, "Registered actuator: %s (type=%d)", config->name, config->type);
    return 0;
}

int actuator_set(const char *name, int value) {
    if (name == NULL) {
        return -1;
    }

    for (int i = 0; i < MAX_ACTUATORS; i++) {
        if (g_actuators[i].registered && strcmp(g_actuators[i].name, name) == 0) {
            actuator_entry_t *a = &g_actuators[i];

            // Clamp value
            if (value < a->min_value) value = a->min_value;
            if (value > a->max_value) value = a->max_value;

            if (a->set_fn == NULL) {
                return -2;
            }

            int ret = a->set_fn(value);
            if (ret == 0) {
                a->current_value = value;
                ESP_LOGD(TAG, "Set %s to %d", name, value);
            }

            return ret;
        }
    }

    return -3;  // Not found
}

int actuator_get(const char *name, int *value) {
    if (name == NULL || value == NULL) {
        return -1;
    }

    for (int i = 0; i < MAX_ACTUATORS; i++) {
        if (g_actuators[i].registered && strcmp(g_actuators[i].name, name) == 0) {
            *value = g_actuators[i].current_value;
            return 0;
        }
    }

    return -2;  // Not found
}
