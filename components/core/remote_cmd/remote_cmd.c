/**
 * @file remote_cmd.c
 * @brief Remote Commander implementation
 */

#include "remote_cmd.h"
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

static const char *TAG = "remote_cmd";

/** @brief Maximum number of registered commands */
#define MAX_CMD_HANDLERS 16

/** @brief Command handler entry */
typedef struct {
    cmd_type_t type;
    cmd_handler_t handler;
    bool registered;
} cmd_handler_entry_t;

/** @brief Command handler table */
static cmd_handler_entry_t g_cmd_handlers[MAX_CMD_HANDLERS];

/** @brief Whether initialized */
static bool g_initialized = false;

// Built-in command handlers
static int cmd_reboot_handler(const char *params, char *result, size_t len) {
    (void)params;
    snprintf(result, len, "Rebooting...");
    esp_restart();
    return 0;  // Won't reach here
}

static int cmd_read_status_handler(const char *params, char *result, size_t len) {
    (void)params;
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_heap = esp_get_minimum_free_heap_size();

    snprintf(result, len,
        "{\"free_heap\":%u,\"min_heap\":%u,\"uptime\":%llu}",
        free_heap, min_heap,
        (unsigned long long)(esp_timer_get_time() / 1000000));
    return 0;
}

static int cmd_reset_config_handler(const char *params, char *result, size_t len) {
    (void)params;
    snprintf(result, len, "Configuration reset to defaults");
    // Call config_manager_reset() here
    return 0;
}

static int cmd_factory_reset_handler(const char *params, char *result, size_t len) {
    (void)params;
    snprintf(result, len, "Factory reset initiated");
    // Perform factory reset
    return 0;
}

static int cmd_read_log_handler(const char *params, char *result, size_t len) {
    (void)params;
    snprintf(result, len, "Log data (implement log_system_export)");
    return 0;
}

int remote_cmd_init(void) {
    if (g_initialized) {
        ESP_LOGW(TAG, "Remote command already initialized");
        return 0;
    }

    // Clear handler table
    memset(g_cmd_handlers, 0, sizeof(g_cmd_handlers));

    // Register built-in handlers
    cmd_register(CMD_REBOOT, cmd_reboot_handler);
    cmd_register(CMD_READ_STATUS, cmd_read_status_handler);
    cmd_register(CMD_RESET_CONFIG, cmd_reset_config_handler);
    cmd_register(CMD_FACTORY_RESET, cmd_factory_reset_handler);
    cmd_register(CMD_READ_LOG, cmd_read_log_handler);

    g_initialized = true;
    ESP_LOGI(TAG, "Remote command initialized");
    return 0;
}

int cmd_register(cmd_type_t type, cmd_handler_t handler) {
    if (handler == NULL) {
        return -1;
    }

    // Find slot
    int slot = -1;
    for (int i = 0; i < MAX_CMD_HANDLERS; i++) {
        if (!g_cmd_handlers[i].registered) {
            slot = i;
            break;
        }
        if (g_cmd_handlers[i].type == type) {
            // Update existing
            g_cmd_handlers[i].handler = handler;
            ESP_LOGI(TAG, "Updated handler for command type %d", type);
            return 0;
        }
    }

    if (slot < 0) {
        ESP_LOGE(TAG, "No free command handler slot");
        return -2;
    }

    g_cmd_handlers[slot].type = type;
    g_cmd_handlers[slot].handler = handler;
    g_cmd_handlers[slot].registered = true;

    ESP_LOGI(TAG, "Registered handler for command type %d", type);
    return 0;
}

int cmd_execute(const command_t *cmd, cmd_response_t *response) {
    if (cmd == NULL || response == NULL) {
        return -1;
    }

    ESP_LOGI(TAG, "Executing command: %s (type=%d)", cmd->cmd_id, cmd->type);

    // Find handler
    cmd_handler_t handler = NULL;
    for (int i = 0; i < MAX_CMD_HANDLERS; i++) {
        if (g_cmd_handlers[i].registered && g_cmd_handlers[i].type == cmd->type) {
            handler = g_cmd_handlers[i].handler;
            break;
        }
    }

    // Build response
    memset(response, 0, sizeof(cmd_response_t));
    strncpy(response->cmd_id, cmd->cmd_id, sizeof(response->cmd_id) - 1);
    response->timestamp = (uint32_t)(esp_timer_get_time() / 1000);

    if (handler == NULL) {
        response->code = -1;
        snprintf(response->message, sizeof(response->message),
                 "No handler for command type %d", cmd->type);
        snprintf(response->result, sizeof(response->result), "{}");
        ESP_LOGW(TAG, "No handler for command type %d", cmd->type);
        return -2;
    }

    // Execute handler
    int ret = handler(cmd->params, response->result, sizeof(response->result));
    if (ret == 0) {
        response->code = 0;
        snprintf(response->message, sizeof(response->message), "Success");
    } else {
        response->code = ret;
        snprintf(response->message, sizeof(response->message), "Error: %d", ret);
    }

    ESP_LOGI(TAG, "Command %s completed with code %d", cmd->cmd_id, response->code);
    return 0;
}

int cmd_response_publish(const cmd_response_t *response) {
    if (response == NULL) {
        return -1;
    }

    // This would publish to MQTT
    // For now, just log
    ESP_LOGI(TAG, "Command response: cmd_id=%s, code=%d, msg=%s",
             response->cmd_id, response->code, response->message);

    return 0;
}

const char* cmd_type_to_string(cmd_type_t type) {
    switch (type) {
    case CMD_REBOOT: return "REBOOT";
    case CMD_RESET_CONFIG: return "RESET_CONFIG";
    case CMD_FACTORY_RESET: return "FACTORY_RESET";
    case CMD_READ_SENSOR: return "READ_SENSOR";
    case CMD_CONTROL_ACTUATOR: return "CONTROL_ACTUATOR";
    case CMD_SET_LED: return "SET_LED";
    case CMD_READ_LOG: return "READ_LOG";
    case CMD_EXECUTE_TEST: return "EXECUTE_TEST";
    case CMD_READ_STATUS: return "READ_STATUS";
    case CMD_CUSTOM: return "CUSTOM";
    default: return "UNKNOWN";
    }
}
