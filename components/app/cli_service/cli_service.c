/**
 * @file cli_service.c
 * @brief ESP CLI Service 封装层实现
 * 
 * 使用 ESP-IDF esp_console 组件
 */

#include "cli_service.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_vfs_dev.h"
#include "esp_vfs_fat.h"
#include "driver/uart.h"
#include "linenoise/linenoise.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "cli_service";

/** @brief CLI 任务句柄 */
static TaskHandle_t g_cli_task_handle = NULL;

/** @brief REPL 是否运行 */
static volatile bool g_repl_running = false;

// ============================================================================
// 内置命令实现
// ============================================================================

/** @brief 设备信息命令 */
static int cmd_device_info(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    printf("\n=== Device Information ===\n");
    printf("Firmware: ESP32 IoT v1.0.0\n");
    printf("Build: %s %s\n", __DATE__, __TIME__);
    printf("Chip: ESP32-S3\n");
    printf("============================\n\n");
    
    return 0;
}

/** @brief 内存信息命令 */
static int cmd_memory(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    printf("\n=== Memory Information ===\n");
    
    extern char _start_heap, _end_heap;
    printf("Heap: %d bytes\n", (int)(&_end_heap - &_start_heap));
    
    printf("Free Heap: %d bytes\n", esp_get_free_heap_size());
    printf("Min Free Heap: %d bytes\n", esp_get_minimum_free_heap_size());
    
    printf("=========================\n\n");
    
    return 0;
}

/** @brief Wi-Fi 状态命令 */
static int cmd_wifi_status(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    printf("\n=== Wi-Fi Status ===\n");
    
    // TODO: 集成 wifi_service 获取状态
    printf("Status: Not implemented yet\n");
    
    printf("====================\n\n");
    
    return 0;
}

/** @brief 系统重启命令 */
static int cmd_reboot(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    printf("\nRebooting...\n");
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();
    
    return 0;
}

/** @brief 帮助命令 */
static int cmd_help(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    printf("\n=== Available Commands ===\n");
    printf("device_info  - Show device information\n");
    printf("memory       - Show memory information\n");
    printf("wifi_status  - Show Wi-Fi status\n");
    printf("reboot       - Reboot the device\n");
    printf("help         - Show this help\n");
    printf("=========================\n\n");
    
    return 0;
}

// ============================================================================
// 内置命令表
// ============================================================================

static const cli_command_t g_builtin_commands[] = {
    {
        .name = "device_info",
        .help = "Show device information",
        .hint = NULL,
        .handler = cmd_device_info,
    },
    {
        .name = "memory",
        .help = "Show memory information",
        .hint = NULL,
        .handler = cmd_memory,
    },
    {
        .name = "wifi_status",
        .help = "Show Wi-Fi status",
        .hint = NULL,
        .handler = cmd_wifi_status,
    },
    {
        .name = "reboot",
        .help = "Reboot the device",
        .hint = NULL,
        .handler = cmd_reboot,
    },
    {
        .name = "help",
        .help = "Show available commands",
        .hint = NULL,
        .handler = cmd_help,
    },
};

#define NUM_BUILTIN_COMMANDS (sizeof(g_builtin_commands) / sizeof(g_builtin_commands[0]))

// ============================================================================
// CLI 服务实现
// ============================================================================

esp_err_t cli_service_init(const cli_service_config_t *config) {
    if (g_cli_task_handle != NULL) {
        ESP_LOGW(TAG, "CLI service already initialized");
        return ESP_OK;
    }
    
    const cli_service_config_t default_config = {
        .enable_repl = true,
        .task_stack_size = 4096,
        .task_priority = 5,
    };
    
    if (config == NULL) {
        config = &default_config;
    }
    
    ESP_LOGI(TAG, "Initializing CLI service...");
    
    // 初始化 LINENOISE
    linenoiseSetCompletionCallback(NULL);
    linenoiseSetHintsCallback(NULL);
    
    // 设置 UART
    /* Drain stdout before reconfiguring it */
    fflush(stdout);
    fsync(fileno(stdout));
    
    /* Disable buffering on stdin */
    setvbuf(stdin, NULL, _IONBF, 0);
    
    /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
    esp_vfs_dev_uart_set_rx_line_endings(ESP_LINE_ENDINGS_CR);
    /* Move the caret to the beginning of the next line on '\n' */
    esp_vfs_dev_uart_set_tx_line_endings(ESP_LINE_ENDINGS_CRLF);
    
    /* Configure UART. Note that REF_TICK is used for compatibility with both 26mhz and
     * 40mhz crystal settings */
    uart_config_t uart_config = {
        .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));
    
    /* Install UART driver for interrupt-driven reads and writes */
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0));
    
    /* Tell VFS to use UART driver */
    esp_vfs_dev_uart_use_driver(UART_NUM_0);
    
    /* Initialize the console */
    esp_console_config_t console_config = {
        .max_cmdline_args = 8,
        .max_cmdline_length = 256,
    };
    ESP_ERROR_CHECK(esp_console_init(&console_config));
    
    /* Register built-in commands */
    for (size_t i = 0; i < NUM_BUILTIN_COMMANDS; i++) {
        const esp_console_cmd_t cmd = {
            .command = g_builtin_commands[i].name,
            .help = g_builtin_commands[i].help,
            .hint = g_builtin_commands[i].hint,
            .func = g_builtin_commands[i].handler,
        };
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmd));
    }
    
    /* Register help command */
    ESP_ERROR_CHECK(esp_console_register_help_command());
    
    ESP_LOGI(TAG, "CLI service initialized");
    
    return ESP_OK;
}

esp_err_t cli_service_deinit(void) {
    if (g_cli_task_handle != NULL) {
        vTaskDelete(g_cli_task_handle);
        g_cli_task_handle = NULL;
    }
    
    esp_console_deinit();
    
    ESP_LOGI(TAG, "CLI service deinitialized");
    
    return ESP_OK;
}

esp_err_t cli_service_register_command(const cli_command_t *cmd) {
    if (cmd == NULL || cmd->name == NULL || cmd->handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const esp_console_cmd_t esp_cmd = {
        .command = cmd->name,
        .help = cmd->help,
        .hint = cmd->hint,
        .func = cmd->handler,
    };
    
    return esp_console_cmd_register(&esp_cmd);
}

esp_err_t cli_service_run(void) {
    g_repl_running = true;
    
    /* Prompt to be printed before each line.
     * This can be customized, made blank, or dynamically set */
    const char *prompt = "esp32> ";
    
    ESP_LOGI(TAG, "CLI REPL started. Type 'help' for available commands.");
    
    /* Figure out if the terminal supports escape sequences */
    int probe_status = linenoiseProbe();
    if (probe_status) {
        /* zero indicates success */
        ESP_LOGI(TAG, "\n"
                  "Your terminal application does not support escape sequences.\n"
                  "Line editing and history features are disabled.\n"
                  "Please use an interactive terminal:\n"
                  "idf.py monitor, or\n"
                  "minicom, or\n"
                  "screen, etc.");
        linenoiseAllowEmpty(true);
    }
    
    while (g_repl_running) {
        /* Get a line using linenoise.
         * The line is returned when ENTER is pressed.
         * If empty, skip processing */
        char *line = linenoise(prompt);
        if (line == NULL) {
            continue;
        }
        
        /* Add the command to the history if not empty */
        if (strlen(line) > 0) {
            linenoiseHistoryAdd(line);
        }
        
        /* Try to run the command */
        int ret = esp_console_run(line, &esp_ret_val);
        if (ret < 0) {
            /* esp_console_run returns -1 on parse failure */
            ESP_LOGE(TAG, "Command not found or parse error");
        } else if (ret == 0) {
            /* Command was successful */
            if (esp_ret_val != 0) {
                ESP_LOGI(TAG, "Command returned %d", esp_ret_val);
            }
        } else {
            /* Command returned non-zero */
            ESP_LOGI(TAG, "Command returned 0x%x", esp_ret_val);
        }
        
        /* linenoise allocates line buffer on heap, free it */
        linenoiseFree(line);
    }
    
    return ESP_OK;
}

/** @brief CLI 任务函数 */
static void cli_task(void *params) {
    ESP_LOGI(TAG, "CLI task started");
    
    cli_service_run();
    
    ESP_LOGI(TAG, "CLI task ended");
    vTaskDelete(NULL);
}

esp_err_t cli_service_start(void) {
    if (g_cli_task_handle != NULL) {
        ESP_LOGW(TAG, "CLI task already running");
        return ESP_ERR_INVALID_STATE;
    }
    
    BaseType_t ret = xTaskCreatePinnedToCore(
        cli_task,
        "cli",
        4096,
        NULL,
        5,
        &g_cli_task_handle,
        0  // Pin to core 0
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create CLI task");
        return ESP_FAIL;
    }
    
    return ESP_OK;
}
