/**
 * @file test_mode.c
 * @brief Manufacturing Test Mode implementation
 *
 * Interactive UART console for hardware bring-up and self-test.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <cJSON.h>

#include "test_mode.h"
#include "device_type.h"
#include "device_manager.h"
#include "node_config.h"
#include "event_bus.h"
#include "peripherals.h"
#include "mfg_provision.h"

static const char *TAG = "test_mode";

#define CONSOLE_LINE_MAX 256

/* ============================================
 * Trigger
 * ============================================ */

esp_err_t test_mode_check_trigger(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << TEST_MODE_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    vTaskDelay(pdMS_TO_TICKS(50));  /* debounce */

    int level = gpio_get_level((gpio_num_t)TEST_MODE_GPIO);
    ESP_LOGI(TAG, "Test mode trigger GPIO%d level=%d", TEST_MODE_GPIO, level);

    return (level == 0) ? ESP_OK : ESP_FAIL;
}

/* ============================================
 * Console helpers
 * ============================================ */

static void print_help(void)
{
    printf("\nCommands:\n");
    printf("  help                     Show this help\n");
    printf("  types                    List available device types\n");
    printf("  list                     List configured devices\n");
    printf("  add <id> <type> [json]   Add a device (json config optional)\n");
    printf("  del <id>                 Delete a device\n");
    printf("  read <id>                Read device value\n");
    printf("  write <id> <json>        Write device value\n");
    printf("  test [id|all]            Run self-test (default: all)\n");
    printf("  report                   Show last test report\n");
    printf("  reset                    Erase all device configuration\n");
    printf("  mfg show                 Show factory data status\n");
    printf("  mfg set <json>           Write factory data\n");
    printf("  mfg apply                Apply factory data now\n");
    printf("  mfg clear                Erase factory data\n");
    printf("  exit                     Reboot\n");
    printf("\n");
}

static void cmd_types(void)
{
    size_t count = 0;
    const device_type_t *types = device_type_get_all(&count);

    printf("\n%-16s %-40s %s\n", "TYPE", "DESCRIPTION", "CAPS");
    printf("--------------------------------------------------------------------------------\n");
    for (size_t i = 0; i < count; i++) {
        char caps[32] = "";
        if (types[i].capabilities & DEVICE_CAPABILITY_READ)    strcat(caps, "R");
        if (types[i].capabilities & DEVICE_CAPABILITY_WRITE)   strcat(caps, "W");
        if (types[i].capabilities & DEVICE_CAPABILITY_NOTIFY)  strcat(caps, "N");
        if (types[i].capabilities & DEVICE_CAPABILITY_PERIODIC) strcat(caps, "P");

        printf("%-16s %-40s %s\n", types[i].name, types[i].description, caps);

        cJSON *cfg = cJSON_CreateObject();
        if (types[i].get_default_config) {
            types[i].get_default_config(cfg);
        }
        char *s = cJSON_PrintUnformatted(cfg);
        printf("%-16s default: %s\n", "", s ? s : "{}");
        free(s);
        cJSON_Delete(cfg);
    }
    printf("\n");
}

static void cmd_list(void)
{
    size_t count;
    const device_t *devices = device_get_all(&count);

    printf("\n%-20s %-14s %-6s %-8s\n", "ID", "TYPE", "EN", "INIT");
    printf("--------------------------------------------------------\n");
    for (size_t i = 0; i < count; i++) {
        printf("%-20s %-14s %-6s %-8s\n",
               devices[i].id, devices[i].type->name,
               devices[i].enabled ? "yes" : "no",
               devices[i].initialized ? "yes" : "no");
    }
    printf("\n%d device(s)\n\n", (int)count);
}

static void cmd_add(char *args)
{
    char *id = strtok(args, " \t");
    char *type = strtok(NULL, " \t");
    char *json = strtok(NULL, "");

    if (id == NULL || type == NULL) {
        printf("Usage: add <id> <type> [json]\n");
        return;
    }

    cJSON *config = NULL;
    if (json != NULL) {
        config = cJSON_Parse(json);
        if (config == NULL) {
            printf("Invalid JSON config\n");
            return;
        }
    }

    esp_err_t err = device_add(id, type, config);
    if (err == ESP_OK) {
        device_manager_save();
        printf("Added '%s' (%s)\n", id, type);
    } else {
        printf("Failed: %s\n", esp_err_to_name(err));
    }

    if (config) cJSON_Delete(config);
}

static void cmd_del(char *args)
{
    char *id = strtok(args, " \t");
    if (id == NULL) {
        printf("Usage: del <id>\n");
        return;
    }

    esp_err_t err = device_remove(id);
    if (err == ESP_OK) {
        device_manager_save();
        printf("Deleted '%s'\n", id);
    } else {
        printf("Failed: %s\n", esp_err_to_name(err));
    }
}

static void cmd_read(char *args)
{
    char *id = strtok(args, " \t");
    if (id == NULL) {
        printf("Usage: read <id>\n");
        return;
    }

    cJSON *value = cJSON_CreateObject();
    esp_err_t err = device_read(id, value);
    if (err == ESP_OK) {
        char *s = cJSON_PrintUnformatted(value);
        printf("%s: %s\n", id, s ? s : "{}");
        free(s);
    } else {
        printf("Failed: %s\n", esp_err_to_name(err));
    }
    cJSON_Delete(value);
}

static void cmd_write(char *args)
{
    char *id = strtok(args, " \t");
    char *json = strtok(NULL, "");

    if (id == NULL || json == NULL) {
        printf("Usage: write <id> <json>\n");
        return;
    }

    cJSON *value = cJSON_Parse(json);
    if (value == NULL) {
        printf("Invalid JSON\n");
        return;
    }

    esp_err_t err = device_write(id, value);
    printf("%s: %s\n", id, err == ESP_OK ? "OK" : esp_err_to_name(err));

    cJSON_Delete(value);
}

/* ============================================
 * Self-test
 * ============================================ */

typedef struct {
    int total;
    int passed;
    int failed;
} test_summary_t;

static test_summary_t s_summary;

static void test_one_device(const device_t *dev)
{
    printf("  [TEST] %-20s (%s) ... ", dev->id, dev->type->name);

    s_summary.total++;

    if (!dev->enabled) {
        printf("SKIP (disabled)\n");
        s_summary.total--;
        return;
    }

    if (!dev->initialized) {
        printf("FAIL (not initialized)\n");
        s_summary.failed++;
        return;
    }

    const device_type_t *type = dev->type;

    /* Read test */
    if (type->capabilities & DEVICE_CAPABILITY_READ) {
        cJSON *value = cJSON_CreateObject();
        esp_err_t err = device_read(dev->id, value);
        if (err != ESP_OK) {
            printf("FAIL (read: %s)\n", esp_err_to_name(err));
            s_summary.failed++;
            cJSON_Delete(value);
            return;
        }
        char *s = cJSON_PrintUnformatted(value);
        printf("PASS read=%s ", s ? s : "{}");
        free(s);
        cJSON_Delete(value);
    }

    /* Write test - toggle and restore for output devices */
    if (type->capabilities & DEVICE_CAPABILITY_WRITE) {
        if (strcmp(type->name, "relay") == 0) {
            cJSON *on = cJSON_CreateBool(true);
            esp_err_t e1 = device_write(dev->id, on);
            cJSON_Delete(on);
            vTaskDelay(pdMS_TO_TICKS(300));
            cJSON *off = cJSON_CreateBool(false);
            esp_err_t e2 = device_write(dev->id, off);
            cJSON_Delete(off);

            if (e1 != ESP_OK || e2 != ESP_OK) {
                printf("FAIL (write)\n");
                s_summary.failed++;
                return;
            }
            printf("toggled ");
        } else if (strcmp(type->name, "ws2812") == 0) {
            /* Brief red flash */
            cJSON *all = cJSON_CreateObject();
            cJSON *rgb = cJSON_AddObjectToObject(all, "all");
            cJSON_AddNumberToObject(rgb, "r", 32);
            cJSON_AddNumberToObject(rgb, "g", 0);
            cJSON_AddNumberToObject(rgb, "b", 0);
            esp_err_t e1 = device_write(dev->id, all);
            cJSON_Delete(all);
            vTaskDelay(pdMS_TO_TICKS(400));

            cJSON *off = cJSON_CreateObject();
            cJSON *offrgb = cJSON_AddObjectToObject(off, "all");
            cJSON_AddNumberToObject(offrgb, "r", 0);
            cJSON_AddNumberToObject(offrgb, "g", 0);
            cJSON_AddNumberToObject(offrgb, "b", 0);
            esp_err_t e2 = device_write(dev->id, off);
            cJSON_Delete(off);

            if (e1 != ESP_OK || e2 != ESP_OK) {
                printf("FAIL (write)\n");
                s_summary.failed++;
                return;
            }
            printf("flashed ");
        } else if (strcmp(type->name, "shiftreg_595") == 0) {
            /* Shift a walking 1 pattern across all outputs */
            cJSON *val = cJSON_CreateObject();
            cJSON *arr = cJSON_AddArrayToObject(val, "bytes");
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(0x01));
            esp_err_t e1 = device_write(dev->id, val);
            cJSON_Delete(val);
            vTaskDelay(pdMS_TO_TICKS(300));

            cJSON *off = cJSON_CreateObject();
            cJSON *offarr = cJSON_AddArrayToObject(off, "bytes");
            cJSON_AddItemToArray(offarr, cJSON_CreateNumber(0x00));
            esp_err_t e2 = device_write(dev->id, off);
            cJSON_Delete(off);

            if (e1 != ESP_OK || e2 != ESP_OK) {
                printf("FAIL (write)\n");
                s_summary.failed++;
                return;
            }
            printf("shifted ");
        }
    }

    /* Button: report current state */
    if (strcmp(type->name, "button") == 0) {
        cJSON *value = cJSON_CreateObject();
        if (device_read(dev->id, value) == ESP_OK) {
            cJSON *pressed = cJSON_GetObjectItem(value, "pressed");
            printf("pressed=%s ", cJSON_IsTrue(pressed) ? "yes" : "no");
        }
        cJSON_Delete(value);
    }

    printf("PASS\n");
    s_summary.passed++;
}

static void cmd_test(char *args)
{
    char *target = strtok(args, " \t");

    memset(&s_summary, 0, sizeof(s_summary));

    printf("\nRunning hardware self-test...\n\n");

    if (target != NULL && strcmp(target, "all") != 0) {
        const device_t *dev = device_get(target);
        if (dev == NULL) {
            printf("Device '%s' not found\n", target);
            return;
        }
        test_one_device(dev);
    } else {
        size_t count;
        const device_t *devices = device_get_all(&count);
        for (size_t i = 0; i < count; i++) {
            test_one_device(&devices[i]);
        }
    }

    printf("\nResult: %d passed, %d failed, %d total\n",
           s_summary.passed, s_summary.failed, s_summary.total);
    printf("%s\n\n", s_summary.failed == 0 ? "*** ALL TESTS PASSED ***" : "*** TESTS FAILED ***");
}

static void cmd_report(void)
{
    printf("\nLast test report: %d passed, %d failed, %d total\n\n",
           s_summary.passed, s_summary.failed, s_summary.total);
}

static void cmd_reset(void)
{
    printf("Erasing device configuration...\n");

    size_t count;
    const device_t *devices = device_get_all(&count);

    /* Remove from the end to avoid index shifting issues */
    while (device_get_count() > 0) {
        const device_t *dev = device_get_all(&count);
        if (count == 0) break;
        char id[32];
        strncpy(id, dev[count - 1].id, sizeof(id) - 1);
        id[sizeof(id) - 1] = '\0';
        device_remove(id);
    }

    device_manager_save();
    printf("Done. 0 devices remain.\n");
}

static void cmd_mfg(char *args)
{
    char *sub = strtok(args, " \t");

    if (sub == NULL) {
        printf("Usage: mfg show|set <json>|apply|clear\n");
        return;
    }

    if (strcmp(sub, "show") == 0) {
        printf("Factory data present: %s\n", mfg_provision_has_data() ? "yes" : "no");
    } else if (strcmp(sub, "set") == 0) {
        char *json = strtok(NULL, "");
        if (json == NULL) {
            printf("Usage: mfg set <json>\n");
            return;
        }
        esp_err_t err = mfg_provision_write(json);
        printf("Write: %s\n", err == ESP_OK ? "OK" : esp_err_to_name(err));
    } else if (strcmp(sub, "apply") == 0) {
        esp_err_t err = mfg_provision_load();
        printf("Apply: %s\n", err == ESP_OK ? "OK" : esp_err_to_name(err));
    } else if (strcmp(sub, "clear") == 0) {
        esp_err_t err = mfg_provision_clear();
        printf("Clear: %s\n", err == ESP_OK ? "OK" : esp_err_to_name(err));
    } else {
        printf("Unknown mfg subcommand: %s\n", sub);
    }
}

/* ============================================
 * Entry point
 * ============================================ */

void test_mode_enter(void)
{
    printf("\n\n");
    printf("================================================\n");
    printf("        ESPX MANUFACTURING TEST MODE\n");
    printf("================================================\n");
    printf(" Firmware : %s\n", CONFIG_FIRMWARE_VERSION);
    printf(" Build    : %s %s\n", __DATE__, __TIME__);
    printf(" Exit     : type 'exit' to reboot\n");
    printf("================================================\n");

    /* Minimal init: NVS + device registry + peripherals (no network) */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    node_config_init();
    node_config_load();
    event_bus_init();
    device_type_registry_init();
    peripherals_register_all();
    device_manager_init();
    device_manager_load();
    mfg_provision_init();

    memset(&s_summary, 0, sizeof(s_summary));

    printf("\nLoaded %d device(s). Type 'help' for commands.\n", (int)device_get_count());

    char line[CONSOLE_LINE_MAX];

    while (1) {
        printf("espx-test> ");
        fflush(stdout);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        /* Strip trailing newline / CR */
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }

        if (len == 0) {
            continue;
        }

        /* Parse command + args */
        char *cmd = strtok(line, " \t");
        if (cmd == NULL) continue;

        char *rest = strtok(NULL, "");  /* remainder, may be NULL */
        char empty[1] = "";

        if (strcmp(cmd, "help") == 0) {
            print_help();
        } else if (strcmp(cmd, "types") == 0) {
            cmd_types();
        } else if (strcmp(cmd, "list") == 0) {
            cmd_list();
        } else if (strcmp(cmd, "add") == 0) {
            cmd_add(rest ? rest : empty);
        } else if (strcmp(cmd, "del") == 0) {
            cmd_del(rest ? rest : empty);
        } else if (strcmp(cmd, "read") == 0) {
            cmd_read(rest ? rest : empty);
        } else if (strcmp(cmd, "write") == 0) {
            cmd_write(rest ? rest : empty);
        } else if (strcmp(cmd, "test") == 0) {
            cmd_test(rest ? rest : empty);
        } else if (strcmp(cmd, "report") == 0) {
            cmd_report();
        } else if (strcmp(cmd, "reset") == 0) {
            cmd_reset();
        } else if (strcmp(cmd, "mfg") == 0) {
            cmd_mfg(rest ? rest : empty);
        } else if (strcmp(cmd, "exit") == 0) {
            printf("Rebooting...\n");
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        } else {
            printf("Unknown command: '%s' (try 'help')\n", cmd);
        }
    }
}
