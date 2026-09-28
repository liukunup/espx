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
#include <nvs.h>
#include <nvs_flash.h>
#include <cJSON.h>

#include "task_util.h"
#include "app_info.h"
#include "test_mode.h"
#include "device_type.h"
#include "device_manager.h"
#include "node_config.h"
#include "event_bus.h"
#include "peripherals.h"
#include "mfg_provision.h"
#include "config_apply.h"
#include "str_utils.h"

static const char *TAG = "test_mode";

#define CONSOLE_LINE_MAX 256

/* NVS flag namespace/key (shared with node_config namespace) */
#define TM_NVS_NAMESPACE   "espx_node"
#define TM_NVS_KEY_REQUEST "test_request"

/* ============================================
 * Test-mode request flag
 * ============================================ */

esp_err_t test_mode_request(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(TM_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_u8(nvs, TM_NVS_KEY_REQUEST, 1);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    if (err == ESP_OK) {
        ESP_LOGW(TAG, "Test mode requested for next boot");
    }
    return err;
}

esp_err_t test_mode_clear_request(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(TM_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(nvs, TM_NVS_KEY_REQUEST);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static bool take_request_flag(void)
{
    nvs_handle_t nvs;
    if (nvs_open(TM_NVS_NAMESPACE, NVS_READONLY, &nvs) != ESP_OK) {
        return false;
    }

    uint8_t flag = 0;
    esp_err_t err = nvs_get_u8(nvs, TM_NVS_KEY_REQUEST, &flag);
    nvs_close(nvs);

    if (err != ESP_OK || flag == 0) {
        return false;
    }

    test_mode_clear_request();
    return true;
}

/* ============================================
 * Trigger
 * ============================================ */

esp_err_t test_mode_check_trigger(void)
{
    if (take_request_flag()) {
        ESP_LOGW(TAG, "Test mode requested via NVS flag");
        return ESP_OK;
    }

#if TEST_MODE_GPIO >= 0
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
    ESP_LOGI(TAG, "Test-mode trigger GPIO%d level=%d", TEST_MODE_GPIO, level);

    if (level == 0) {
        return ESP_OK;
    }
#else
    ESP_LOGI(TAG, "Test-mode trigger GPIO disabled (set CONFIG_MFG_TEST_GPIO)");
#endif

    return ESP_FAIL;
}

/* ============================================
 * Long-press watchdog
 * ============================================ */

static void longpress_task(void *arg)
{
    const gpio_num_t btn = (gpio_num_t)TEST_MODE_BOOT_GPIO;
    bool triggered = false;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << btn),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    int held_ms = 0;

    while (1) {
        if (!triggered && gpio_get_level(btn) == 0) {
            held_ms += 100;
            if (held_ms >= TEST_MODE_LONGPRESS_MS) {
                triggered = true;
                ESP_LOGW(TAG, "BOOT held %d ms -> entering test mode", held_ms);
                test_mode_request();
                vTaskDelay(pdMS_TO_TICKS(250));
                esp_restart();
            }
        } else if (!triggered) {
            held_ms = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

esp_err_t test_mode_start_longpress_watchdog(void)
{
    if (TEST_MODE_BOOT_GPIO < 0) {
        return ESP_OK;
    }

    BaseType_t ok = /* Internal-RAM stack: it writes the NVS test-mode request flag. */
    xTaskCreate(longpress_task, "tm_longpress", 3072, NULL, 2, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to start long-press watchdog");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Hold BOOT (GPIO%d) for %d ms to enter test mode",
             TEST_MODE_BOOT_GPIO, TEST_MODE_LONGPRESS_MS);
    return ESP_OK;
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
    printf("  cfg <json|yaml>          Apply a configuration document\n");
    printf("  cfg show                 Print the current configuration\n");
    printf("  exit                     Reboot\n");
    printf("\n");
}

static void cmd_types(void)
{
    printf("\n%-16s %-40s %s\n", "TYPE", "DESCRIPTION", "CAPS");
    printf("--------------------------------------------------------------------------------\n");

    for (size_t i = 0; i < device_type_count(); i++) {
        const device_type_t *t = device_type_get_by_index(i);
        if (t == NULL) continue;

        char caps[32] = "";
        if (t->capabilities & DEVICE_CAPABILITY_READ)     strcat(caps, "R");
        if (t->capabilities & DEVICE_CAPABILITY_WRITE)    strcat(caps, "W");
        if (t->capabilities & DEVICE_CAPABILITY_NOTIFY)   strcat(caps, "N");
        if (t->capabilities & DEVICE_CAPABILITY_PERIODIC) strcat(caps, "P");

        printf("%-16s %-40s %s\n", t->name, t->description, caps);

        cJSON *cfg = cJSON_CreateObject();
        if (t->get_default_config) {
            t->get_default_config(cfg);
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
    printf("\n%-20s %-16s %-6s %-8s\n", "ID", "TYPE", "EN", "INIT");
    printf("------------------------------------------------------------------------\n");

    for (size_t i = 0; i < device_get_count(); i++) {
        const device_t *dev = device_get_by_index(i);
        if (dev == NULL) continue;
        printf("%-20s %-16s %-6s %-8s\n",
               dev->id, dev->type->name,
               dev->enabled ? "yes" : "no",
               dev->initialized ? "yes" : "no");
    }
    printf("\n%d device(s)\n\n", (int)device_get_count());
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
        for (size_t i = 0; i < device_get_count(); i++) {
            const device_t *dev = device_get_by_index(i);
            if (dev) test_one_device(dev);
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

    /* Remove from the end: device_remove() shifts the index array. */
    while (device_get_count() > 0) {
        const device_t *dev = device_get_by_index(device_get_count() - 1);
        if (dev == NULL) break;

        char id[sizeof(dev->id)];
        str_copy(id, sizeof(id), dev->id);
        device_remove(id);
    }

    device_manager_save();
    printf("Done. %d device(s) remain.\n", (int)device_get_count());
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

/**
 * @brief Read one line from the console
 *
 * Accumulates characters explicitly instead of using fgets(): the ESP-IDF
 * console VFS may return short reads, in which case fgets() hands back a
 * partial line and each character is treated as its own command.
 *
 * @return true when a complete line was assembled (without the newline)
 */
static bool console_read_line(char *line, size_t max)
{
    static size_t pos = 0;
    int c;

    while ((c = fgetc(stdin)) != EOF) {
        if (c == '\r') {
            continue;                       /* ignore CR (CRLF terminals) */
        }
        if (c == '\n') {
            line[pos] = '\0';
            pos = 0;
            return true;
        }
        if (c == 0x08 || c == 0x7F) {       /* backspace / delete */
            if (pos > 0) pos--;
            continue;
        }
        if (pos < max - 1) {
            line[pos++] = (char)c;
        }
    }

    /* No data available yet: fall back to the caller's poll delay. */
    return false;
}

static void cmd_cfg(char *args)
{
    if (args == NULL || args[0] == '\0') {
        printf("Usage: cfg <json|yaml>   |   cfg show\n");
        return;
    }

    if (strcmp(args, "show") == 0) {
        config_apply_result_t res;
        (void)res;
        cJSON *cfg = node_config_get();
        char *s = cfg ? cJSON_Print(cfg) : NULL;
        printf("%s\n", s ? s : "<none>");
        free(s);
        if (cfg) cJSON_Delete(cfg);

        printf("devices:\n");
        for (size_t i = 0; i < device_get_count(); i++) {
            const device_t *d = device_get_by_index(i);
            if (d) printf("  %-16s %-16s %s\n", d->id, d->type->name,
                          d->initialized ? "ok" : "not init");
        }
        return;
    }

    config_apply_result_t res;
    char err[128] = {0};
    esp_err_t rc = config_apply_payload(args, &res, err, sizeof(err));

    printf("apply: %s  (+%d ~%d -%d failed %d)%s%s%s\n",
           rc == ESP_OK ? "OK" : esp_err_to_name(rc),
           res.devices_added, res.devices_updated, res.devices_removed, res.devices_failed,
           res.reboot_recommended ? "  reboot required" : "",
           (res.error[0] ? "  error: " : ""), res.error);
    if (rc != ESP_OK && err[0]) printf("  %s\n", err);
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
    printf(" Firmware : %s\n", app_version());
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

    printf("espx-test> ");
    fflush(stdout);

    while (1) {
        if (!console_read_line(line, sizeof(line))) {
            /* No complete line yet. Do NOT reprint the prompt here: that
             * spams it every poll interval. */
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (line[0] == '\0') {
            printf("espx-test> ");
            fflush(stdout);
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
        } else if (strcmp(cmd, "cfg") == 0) {
            cmd_cfg(rest ? rest : empty);
        } else if (strcmp(cmd, "exit") == 0) {
            printf("Rebooting...\n");
            vTaskDelay(pdMS_TO_TICKS(200));
            esp_restart();
        } else {
            printf("Unknown command: '%s' (try 'help')\n", cmd);
        }

        printf("espx-test> ");
        fflush(stdout);
    }
}
