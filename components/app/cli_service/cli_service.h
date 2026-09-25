/**
 * @file cli_service.h
 * @brief ESP CLI Service 封装层
 * 
 * 提供命令行接口，支持:
 * - 内置命令 (device_info, wifi_status, ota, diag)
 * - 可扩展的命令注册
 * - UART REPL 模式
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief CLI 命令处理函数类型
 */
typedef int (*cli_cmd_handler_t)(int argc, char **argv);

/**
 * @brief CLI 命令结构
 */
typedef struct {
    const char *name;      /**< 命令名称 */
    const char *help;     /**< 帮助文本 */
    const char *hint;     /**< 用法提示 */
    cli_cmd_handler_t handler;  /**< 处理函数 */
} cli_command_t;

/**
 * @brief CLI 服务配置
 */
typedef struct {
    bool enable_repl;     /**< 启用 REPL 模式 */
    uint32_t task_stack_size;  /**< CLI 任务栈大小 */
    uint32_t task_priority;     /**< CLI 任务优先级 */
} cli_service_config_t;

/**
 * @brief 初始化 CLI 服务
 * @param config 配置 (NULL 使用默认)
 * @return ESP_OK 成功
 */
esp_err_t cli_service_init(const cli_service_config_t *config);

/**
 * @brief 反初始化 CLI 服务
 * @return ESP_OK 成功
 */
esp_err_t cli_service_deinit(void);

/**
 * @brief 注册命令
 * @param cmd 命令结构
 * @return ESP_OK 成功
 */
esp_err_t cli_service_register_command(const cli_command_t *cmd);

/**
 * @brief 运行 CLI (阻塞)
 * @return ESP_OK 成功
 */
esp_err_t cli_service_run(void);

/**
 * @brief 启动 CLI 任务 (非阻塞)
 * @return ESP_OK 成功
 */
esp_err_t cli_service_start(void);

#ifdef __cplusplus
}
#endif
