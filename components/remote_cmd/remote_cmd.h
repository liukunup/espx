/**
 * @file remote_cmd.h
 * @brief Remote Commander component for command execution
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Command type enumeration
 */
typedef enum {
    CMD_REBOOT = 1,
    CMD_RESET_CONFIG,
    CMD_FACTORY_RESET,
    CMD_READ_SENSOR,
    CMD_CONTROL_ACTUATOR,
    CMD_SET_LED,
    CMD_READ_LOG,
    CMD_EXECUTE_TEST,
    CMD_READ_STATUS,
    CMD_CUSTOM
} cmd_type_t;

/**
 * @brief Command structure
 */
typedef struct {
    char cmd_id[32];
    cmd_type_t type;
    char params[256];
    uint32_t timestamp;
    uint32_t timeout_ms;
} command_t;

/**
 * @brief Command response structure
 */
typedef struct {
    char cmd_id[32];
    int code;
    char message[128];
    char result[512];
    uint32_t timestamp;
} cmd_response_t;

/**
 * @brief Command handler callback
 */
typedef int (*cmd_handler_t)(const char *params, char *result, size_t len);

/**
 * @brief Initialize remote command system
 *
 * @return 0 on success, negative on error
 */
int remote_cmd_init(void);

/**
 * @brief Register a command handler
 *
 * @param type Command type
 * @param handler Handler function
 * @return 0 on success, negative on error
 */
int cmd_register(cmd_type_t type, cmd_handler_t handler);

/**
 * @brief Execute a command
 *
 * @param cmd Command to execute
 * @param response Command response
 * @return 0 on success, negative on error
 */
int cmd_execute(const command_t *cmd, cmd_response_t *response);

/**
 * @brief Publish command response to MQTT
 *
 * @param response Command response
 * @return 0 on success, negative on error
 */
int cmd_response_publish(const cmd_response_t *response);

/**
 * @brief Get command type name as string
 *
 * @param type Command type
 * @return String representation
 */
const char* cmd_type_to_string(cmd_type_t type);

#ifdef __cplusplus
}
#endif
