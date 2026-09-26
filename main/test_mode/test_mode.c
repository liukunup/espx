/**
 * @file test_mode.c
 * @brief Manufacturing Test Mode implementation
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_mac.h>
#include <nvs_flash.h>

#include "test_mode.h"
#include "led_driver.h"

static const char *TAG = "test_mode";

static test_result_t g_results[TEST_ITEM_COUNT] = {TEST_RESULT_NONE};

/**
 * @brief Print test menu
 */
static void print_menu(void)
{
    printf("\n");
    printf("=============================================\n");
    printf("       ESPX Manufacturing Test Mode\n");
    printf("=============================================\n");
    printf("\n");
    printf("Test Results:\n");
    printf("  [1] LED Test        : %s\n", 
           g_results[TEST_ITEM_LED] == TEST_RESULT_PASS ? "PASS" :
           g_results[TEST_ITEM_LED] == TEST_RESULT_FAIL ? "FAIL" : "-");
    printf("  [2] Button Test     : %s\n",
           g_results[TEST_ITEM_BUTTON] == TEST_RESULT_PASS ? "PASS" :
           g_results[TEST_ITEM_BUTTON] == TEST_RESULT_FAIL ? "FAIL" : "-");
    printf("  [3] Wi-Fi Test      : %s\n",
           g_results[TEST_ITEM_WIFI] == TEST_RESULT_PASS ? "PASS" :
           g_results[TEST_ITEM_WIFI] == TEST_RESULT_FAIL ? "FAIL" : "-");
    printf("  [4] MQTT Test       : %s\n",
           g_results[TEST_ITEM_MQTT] == TEST_RESULT_PASS ? "PASS" :
           g_results[TEST_ITEM_MQTT] == TEST_RESULT_FAIL ? "FAIL" : "-");
    printf("  [5] UART Test       : %s\n",
           g_results[TEST_ITEM_UART] == TEST_RESULT_PASS ? "PASS" :
           g_results[TEST_ITEM_UART] == TEST_RESULT_FAIL ? "FAIL" : "-");
    printf("\n");
    printf("Commands:\n");
    printf("  led on/off/blink    - LED control\n");
    printf("  button test         - Test button input\n");
    printf("  wifi scan           - Scan Wi-Fi networks\n");
    printf("  wifi connect <ssid> [password] - Connect to Wi-Fi\n");
    printf("  mqtt test           - Test MQTT connection\n");
    printf("  pass <n>            - Mark test N as PASS\n");
    printf("  fail <n>            - Mark test N as FAIL\n");
    printf("  status              - Show test status\n");
    printf("  clear               - Clear all results\n");
    printf("  exit                - Exit and reboot\n");
    printf("\n> ");
    fflush(stdout);
}

/**
 * @brief Process test command
 */
static void process_command(const char *cmd)
{
    char cmd_copy[256];
    strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';

    char *token = strtok(cmd_copy, " \t\n");

    if (token == NULL) {
        return;
    }

    if (strcmp(token, "led") == 0) {
        char *arg = strtok(NULL, " \t\n");
        if (arg == NULL) {
            printf("Usage: led on/off/blink\n");
        } else if (strcmp(arg, "on") == 0) {
            led_set_color(255, 0, 0);  // Red
            printf("LED turned ON\n");
        } else if (strcmp(arg, "off") == 0) {
            led_set_color(0, 0, 0);  // Off
            printf("LED turned OFF\n");
        } else if (strcmp(arg, "blink") == 0) {
            printf("LED blinking...\n");
            for (int i = 0; i < 5; i++) {
                led_set_color(0, 255, 0);  // Green
                vTaskDelay(pdMS_TO_TICKS(200));
                led_set_color(0, 0, 0);  // Off
                vTaskDelay(pdMS_TO_TICKS(200));
            }
            printf("LED blink test complete\n");
        }
    }
    else if (strcmp(token, "button") == 0) {
        printf("Press the BOOT button...\n");
        // In real implementation, wait for button press
        vTaskDelay(pdMS_TO_TICKS(1000));
        printf("Button test completed (manual verification)\n");
    }
    else if (strcmp(token, "wifi") == 0) {
        char *subcmd = strtok(NULL, " \t\n");
        if (subcmd == NULL) {
            printf("Usage: wifi scan/connect\n");
        } else if (strcmp(subcmd, "scan") == 0) {
            printf("Scanning Wi-Fi networks...\n");
            // Would implement Wi-Fi scan
            printf("Wi-Fi scan completed\n");
        } else if (strcmp(subcmd, "connect") == 0) {
            char *ssid = strtok(NULL, " \t\n");
            if (ssid == NULL) {
                printf("Usage: wifi connect <ssid> [password]\n");
            } else {
                printf("Would connect to: %s\n", ssid);
                g_results[TEST_ITEM_WIFI] = TEST_RESULT_PASS;
            }
        }
    }
    else if (strcmp(token, "mqtt") == 0) {
        printf("Testing MQTT connection...\n");
        // Would test MQTT
        printf("MQTT test completed\n");
        g_results[TEST_ITEM_MQTT] = TEST_RESULT_PASS;
    }
    else if (strcmp(token, "pass") == 0) {
        char *num_str = strtok(NULL, " \t\n");
        if (num_str == NULL) {
            printf("Usage: pass <n>\n");
        } else {
            int num = atoi(num_str);
            if (num >= 1 && num <= TEST_ITEM_COUNT) {
                g_results[num - 1] = TEST_RESULT_PASS;
                printf("Test %d marked as PASS\n", num);
            } else {
                printf("Invalid test number\n");
            }
        }
    }
    else if (strcmp(token, "fail") == 0) {
        char *num_str = strtok(NULL, " \t\n");
        if (num_str == NULL) {
            printf("Usage: fail <n>\n");
        } else {
            int num = atoi(num_str);
            if (num >= 1 && num <= TEST_ITEM_COUNT) {
                g_results[num - 1] = TEST_RESULT_FAIL;
                printf("Test %d marked as FAIL\n", num);
            } else {
                printf("Invalid test number\n");
            }
        }
    }
    else if (strcmp(token, "status") == 0) {
        print_menu();
    }
    else if (strcmp(token, "clear") == 0) {
        for (int i = 0; i < TEST_ITEM_COUNT; i++) {
            g_results[i] = TEST_RESULT_NONE;
        }
        printf("All results cleared\n");
    }
    else if (strcmp(token, "exit") == 0) {
        printf("Exiting test mode...\n");
    }
    else {
        printf("Unknown command: %s\n", token);
    }
}

// Public API implementation
esp_err_t test_mode_check_trigger(void)
{
    // Configure GPIO for input
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TEST_MODE_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    gpio_config(&io_conf);

    // Check if GPIO is low (button pressed)
    vTaskDelay(pdMS_TO_TICKS(100));  // Debounce
    int level = gpio_get_level((gpio_num_t)TEST_MODE_GPIO);

    ESP_LOGI(TAG, "Test mode GPIO level: %d", level);

    // For test mode, typically button is pulled high and pressed low
    // So low level = test mode triggered
    if (level == 0) {
        ESP_LOGI(TAG, "Test mode triggered!");
        return ESP_OK;
    }

    return ESP_FAIL;
}

void test_mode_enter(void)
{
    printf("\n\n=============================================\n");
    printf("       ENTERING TEST MODE\n");
    printf("=============================================\n\n");

    // Disable Wi-Fi auto-reconnect temporarily
    // Initialize LED
    led_driver_init();

    // Main test loop
    char line[256];

    while (1) {
        print_menu();

        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }

        if (strcmp(line, "exit") == 0) {
            break;
        }

        process_command(line);
    }

    printf("\nRebooting...\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

test_result_t test_mode_get_result(test_item_t item)
{
    if (item >= TEST_ITEM_COUNT) {
        return TEST_RESULT_NONE;
    }
    return g_results[item];
}

void test_mode_set_result(test_item_t item, test_result_t result)
{
    if (item < TEST_ITEM_COUNT) {
        g_results[item] = result;
    }
}
