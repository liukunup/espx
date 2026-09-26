/**
 * @file at_service.c
 * @brief Serial AT command interface implementation
 *
 * Design notes
 * ------------
 * - Table driven: one entry per command, so adding a command does not touch the
 *   parser. Rows declare whether a query (?) and a test (?) form is allowed.
 * - The AT layer owns no configuration state. Mutations are delegated to
 *   config_apply(), device_write() and the MQTT client, which is the single
 *   entry point required by AGENT.md.
 * - Replies are written with a mutex so concurrent pushes (MQTT AT-style
 *   notifications are not implemented) cannot interleave mid-line.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <driver/uart.h>
#include <soc/uart_pins.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <cJSON.h>

#include "task_util.h"
#include "app_info.h"
#include "at_service.h"
#include "node_config.h"
#include "device_manager.h"
#include "device_type.h"
#include "config_apply.h"
#include "mqtt_client/espx_mqtt_client.h"
#include "ota_service/ota_service.h"
#include "test_mode/test_mode.h"
#include "net_services/time_sync.h"
#include "net_services/mdns_service.h"
#include "net_services/net_services.h"

static const char *TAG = "at";

#define AT_UART        ((uart_port_t)CONFIG_ESPX_AT_UART_NUM)

/* IO_MUX default pins of the AT UART (ESP32-S3). UART2 has none: it must be
 * given explicit pins with CONFIG_ESPX_AT_USE_DEFAULT_PINS disabled. */
#if CONFIG_ESPX_AT_UART_NUM == 0
#define AT_DEFAULT_TX_PIN  U0TXD_GPIO_NUM
#define AT_DEFAULT_RX_PIN  U0RXD_GPIO_NUM
#elif CONFIG_ESPX_AT_UART_NUM == 1
#define AT_DEFAULT_TX_PIN  U1TXD_GPIO_NUM
#define AT_DEFAULT_RX_PIN  U1RXD_GPIO_NUM
#else
#define AT_DEFAULT_TX_PIN  (U2TXD_GPIO_NUM)
#define AT_DEFAULT_RX_PIN  (U2RXD_GPIO_NUM)
#endif
#define AT_BUF_SIZE    512
#define AT_RX_BUF      1024
#define AT_TX_BUF      1024
#define AT_MAX_ARGS    8

static bool s_running = false;
static TaskHandle_t s_task = NULL;
static SemaphoreHandle_t s_tx_lock = NULL;

/* -------------------------------------------------------------------------- */
/* reply helpers                                                              */
/* -------------------------------------------------------------------------- */

static void at_write(const char *text)
{
    if (s_tx_lock) xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    uart_write_bytes(AT_UART, text, strlen(text));
    if (s_tx_lock) xSemaphoreGive(s_tx_lock);
}

static void at_printf(const char *fmt, ...)
{
    char buf[AT_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    at_write(buf);
}

/** ESP-AT success reply */
static void reply_ok(void)          { at_write("\r\nOK\r\n"); }
/** ESP-AT failure reply */
static void reply_error(void)       { at_write("\r\nERROR\r\n"); }
/** Intermediate result line, e.g. +CWJAP:"ssid" */
static void reply_line(const char *fmt, ...)
{
    char buf[AT_BUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    at_write("\r\n");
    at_write(buf);
}

/* -------------------------------------------------------------------------- */
/* argument helpers                                                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Split an argument list on commas, honouring double quotes
 *
 * AT+CWJAP="my,ssid","pass" must yield two arguments, not three.
 *
 * @return number of arguments
 */
static int split_args(char *input, char *argv[], int max)
{
    int n = 0;
    char *p = input;

    while (p && *p && n < max) {
        while (*p == ' ') p++;
        if (*p == '\0') break;

        if (*p == '"') {
            p++;
            argv[n++] = p;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1]) {
                    /* keep escapes resolvable by unescape() */
                    p += 2;
                    continue;
                }
                p++;
            }
            if (*p == '"') {
                *p = '\0';
                p++;
            }
        } else {
            argv[n++] = p;
            while (*p && *p != ',') p++;
            if (*p == ',') {
                *p = '\0';
                p++;
            } else {
                break;
            }
        }

        while (*p == ' ') p++;
        if (*p == ',') p++;
    }

    return n;
}

/** Resolve \r \n \t \" \\ inside a quoted argument, in place */
static void unescape(char *s)
{
    char *w = s;
    for (char *r = s; *r; r++) {
        if (*r == '\\' && r[1]) {
            r++;
            switch (*r) {
            case 'n': *w++ = '\n'; break;
            case 'r': *w++ = '\r'; break;
            case 't': *w++ = '\t'; break;
            case '"': *w++ = '"'; break;
            case '\\': *w++ = '\\'; break;
            default: *w++ = *r; break;
            }
        } else {
            *w++ = *r;
        }
    }
    *w = '\0';
}

static bool at_streq(const char *a, const char *b)
{
    return strcasecmp(a, b) == 0;
}

/* -------------------------------------------------------------------------- */
/* commands                                                                   */
/* -------------------------------------------------------------------------- */

static void cmd_test_at(char *args)
{
    reply_ok();
}

static void cmd_gmr(char *args)
{
    const esp_app_desc_t *app = esp_app_get_description();
    reply_line("AT version:%s", AT_SERVICE_VERSION);
    reply_line("SDK version:%s", esp_get_idf_version());
    reply_line("Firmware version:%s", app ? app->version : app_version());
    reply_line("Compile time:%s %s", __DATE__, __TIME__);
    reply_ok();
}

static void cmd_rst(char *args)
{
    at_printf("\r\nOK\r\n");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

static void cmd_restore(char *args)
{
    /* ESP-AT semantics: wipe user configuration and reboot. */
    at_printf("\r\nOK\r\n");

    size_t n = device_get_count();
    for (size_t i = n; i > 0; i--) {
        const device_t *d = device_get_by_index(i - 1);
        if (d) {
            char id[sizeof(d->id)];
            strncpy(id, d->id, sizeof(id) - 1);
            id[sizeof(id) - 1] = '\0';
            device_remove(id);
        }
    }
    device_manager_save();

    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

static void cmd_cwmode(char *args)
{
    if (args == NULL || at_streq(args, "?")) {
        wifi_mode_t mode;
        if (esp_wifi_get_mode(&mode) != ESP_OK) {
            reply_error();
            return;
        }
        int m = (mode == WIFI_MODE_STA) ? 1 : (mode == WIFI_MODE_AP) ? 2 : 3;
        reply_line("+CWMODE:%d", m);
        reply_ok();
        return;
    }

    wifi_mode_t mode;
    switch (atoi(args)) {
    case 1: mode = WIFI_MODE_STA; break;
    case 2: mode = WIFI_MODE_AP; break;
    case 3: mode = WIFI_MODE_APSTA; break;
    default:
        /* 0 (null mode) is not something this node needs; reject it clearly
         * rather than silently accepting an invalid value. */
        reply_error();
        return;
    }

    esp_wifi_set_mode(mode);
    reply_ok();
}

static void cmd_cwjap(char *args)
{
    if (args == NULL || *args == '\0') {
        /* Query: report the current association. */
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            reply_line("+CWJAP:\"%s\",%02x:%02x:%02x:%02x:%02x:%02x,%d",
                       (char *)ap.ssid,
                       ap.bssid[0], ap.bssid[1], ap.bssid[2],
                       ap.bssid[3], ap.bssid[4], ap.bssid[5],
                       ap.rssi);
            reply_ok();
        } else {
            reply_error();
        }
        return;
    }

    char *argv[AT_MAX_ARGS] = {0};
    int n = split_args(args, argv, AT_MAX_ARGS);
    if (n < 1 || argv[0][0] == '\0') {
        reply_error();
        return;
    }
    unescape(argv[0]);
    if (n > 1) unescape(argv[1]);

    /* Join through the same configuration path the Web UI and MQTT use, then
     * reboot so the station comes up with it (matching AT+CWJAP behaviour of
     * persisting the credentials). */
    cJSON *doc = cJSON_CreateObject();
    cJSON *net = cJSON_AddObjectToObject(doc, "network");
    cJSON_AddStringToObject(net, "wifi_ssid", argv[0]);
    cJSON_AddStringToObject(net, "wifi_password", (n > 1) ? argv[1] : "");

    config_apply_result_t r;
    char err[96] = {0};
    esp_err_t rc = config_apply_payload(cJSON_PrintUnformatted(doc), &r, err, sizeof(err));
    cJSON_Delete(doc);

    if (rc != ESP_OK) {
        at_printf("\r\n+CWJAP:FAIL,%s\r\n", err[0] ? err : "config");
        reply_error();
        return;
    }

    reply_line("WIFI CONNECTED");
    reply_line("WIFI GOT IP");
    reply_ok();

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void cmd_cwqap(char *args)
{
    esp_wifi_disconnect();
    reply_ok();
}

static void cmd_cifsr(char *args)
{
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == NULL) {
        reply_error();
        return;
    }

    esp_netif_ip_info_t ip;
    if (esp_netif_get_ip_info(netif, &ip) != ESP_OK || ip.ip.addr == 0) {
        reply_error();
        return;
    }

    uint8_t mac[6] = {0};
    esp_wifi_get_mac(WIFI_IF_STA, mac);

    reply_line("+CIFSR:STAIP,\"" IPSTR "\"", IP2STR(&ip.ip));
    reply_line("+CIFSR:STAMAC,\"%02x:%02x:%02x:%02x:%02x:%02x\"",
               mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    reply_ok();
}

static void cmd_systime(char *args)
{
    char iso[32];
    if (time_sync_iso8601(iso, sizeof(iso)) == ESP_OK) {
        reply_line("+SYSTIME:%s", iso);
        reply_line("+SYSTIME:EPOCH,%lld", (long long)time_sync_epoch());
        reply_ok();
    } else {
        reply_line("+SYSTIME:NOT_SYNCED");
        reply_line("+SYSTIME:SERVER,%s", time_sync_server());
        reply_line("+SYSTIME:IP_STATUS,%s",
                   net_services_time_ready() ? "ready" : "not_ready");
        reply_error();
    }
}

static void cmd_hostname(char *args)
{
    if (mdns_service_is_running()) {
        reply_line("+HOSTNAME:%s", mdns_service_fqdn());
        reply_ok();
    } else {
        reply_error();
    }
}

static void cmd_gmr_id(char *args)
{
    reply_line("+ID:%s", node_config_get_device_id());
    reply_line("+NAME:%s", node_config_get_name());
    reply_ok();
}

/* --- generic configuration channel ---------------------------------------- */

static void cmd_cfg(char *args)
{
    if (args == NULL || *args == '\0' || at_streq(args, "?")) {
        cJSON *cfg = config_export();
        char *text = cfg ? cJSON_PrintUnformatted(cfg) : NULL;
        if (text) {
            reply_line("+CFG:%s", text);
            free(text);
            cJSON_Delete(cfg);
            reply_ok();
        } else {
            reply_error();
        }
        return;
    }

    /* Accept a YAML or JSON configuration document, exactly like the Web API
     * and MQTT paths. */
    unescape(args);

    config_apply_result_t r;
    char err[96] = {0};
    esp_err_t rc = config_apply_payload(args, &r, err, sizeof(err));

    reply_line("+CFG:added,%d", r.devices_added);
    reply_line("+CFG:updated,%d", r.devices_updated);
    reply_line("+CFG:removed,%d", r.devices_removed);
    if (r.devices_failed) reply_line("+CFG:failed,%d", r.devices_failed);
    if (r.reboot_recommended) reply_line("+CFG:REBOOT_REQUIRED");
    if (rc != ESP_OK || r.devices_failed) {
        if (err[0]) reply_line("+CFG:ERROR,%s", err);
        else if (r.error[0]) reply_line("+CFG:ERROR,%s", r.error);
        reply_error();
        return;
    }
    reply_ok();
}

/* --- devices -------------------------------------------------------------- */

static void cmd_dev(char *args)
{
    if (args == NULL || *args == '\0' || at_streq(args, "?")) {
        for (size_t i = 0; i < device_get_count(); i++) {
            const device_t *d = device_get_by_index(i);
            if (!d) continue;
            reply_line("+DEV:\"%s\",\"%s\",%d,%d", d->id, d->type->name,
                       d->enabled, d->initialized);
        }
        reply_ok();
        return;
    }

    char *argv[AT_MAX_ARGS] = {0};
    int n = split_args(args, argv, AT_MAX_ARGS);
    if (n < 1) {
        reply_error();
        return;
    }
    unescape(argv[0]);

    if (n == 1) {
        /* read */
        cJSON *value = cJSON_CreateObject();
        if (device_read(argv[0], value) == ESP_OK) {
            char *t = cJSON_PrintUnformatted(value);
            reply_line("+DEV:\"%s\",%s", argv[0], t ? t : "null");
            free(t);
            cJSON_Delete(value);
            reply_ok();
        } else {
            cJSON_Delete(value);
            reply_error();
        }
        return;
    }

    /* write: AT+DEV="id",<json> */
    unescape(argv[1]);
    cJSON *value = cJSON_Parse(argv[1]);
    if (value == NULL) {
        reply_error();
        return;
    }
    esp_err_t err = device_write(argv[0], value);
    cJSON_Delete(value);
    if (err == ESP_OK) reply_ok(); else reply_error();
}

static void cmd_devlist(char *args)
{
    size_t n = 0;
    for (size_t i = 0; i < device_type_count(); i++) {
        const device_type_t *t = device_type_get_by_index(i);
        if (!t) continue;
        cJSON *cfg = cJSON_CreateObject();
        if (t->get_default_config) t->get_default_config(cfg);
        char *c = cJSON_PrintUnformatted(cfg);
        reply_line("+DEVTYPE:\"%s\",\"%s\",%s", t->name, t->description, c ? c : "{}");
        free(c);
        cJSON_Delete(cfg);
        n++;
    }
    if (n == 0) reply_line("+DEVTYPE:NONE");
    reply_ok();
}

/* --- MQTT ----------------------------------------------------------------- */

static void cmd_mqttconn(char *args)
{
    if (args == NULL || *args == '\0' || at_streq(args, "?")) {
        cJSON *cfg = config_export();
        cJSON *net = cfg ? cJSON_GetObjectItem(cfg, "network") : NULL;
        cJSON *broker = net ? cJSON_GetObjectItem(net, "mqtt_broker") : NULL;
        reply_line("+MQTTCONN:%d", mqtt_client_is_connected());
        if (cJSON_IsString(broker)) reply_line("+MQTTCONN:BROKER,%s", broker->valuestring);
        cJSON_Delete(cfg);
        reply_ok();
        return;
    }

    char *argv[AT_MAX_ARGS] = {0};
    int n = split_args(args, argv, AT_MAX_ARGS);
    if (n < 1) { reply_error(); return; }
    unescape(argv[0]);

    /* Build the broker URI: AT+MQTTCONN="host"[,port[,user,pass]] */
    char uri[192];
    if (n >= 2) {
        snprintf(uri, sizeof(uri), "mqtt://%s:%s", argv[0], argv[1]);
    } else if (strstr(argv[0], "://")) {
        snprintf(uri, sizeof(uri), "%s", argv[0]);
    } else {
        snprintf(uri, sizeof(uri), "mqtt://%s:1883", argv[0]);
    }

    cJSON *doc = cJSON_CreateObject();
    cJSON *net = cJSON_AddObjectToObject(doc, "network");
    cJSON_AddStringToObject(net, "mqtt_broker", uri);
    if (n >= 4) {
        unescape(argv[2]);
        unescape(argv[3]);
        cJSON_AddStringToObject(net, "mqtt_username", argv[2]);
        cJSON_AddStringToObject(net, "mqtt_password", argv[3]);
    }

    char *text = cJSON_PrintUnformatted(doc);
    config_apply_result_t r;
    char err[96] = {0};
    esp_err_t rc = config_apply_payload(text, &r, err, sizeof(err));
    free(text);
    cJSON_Delete(doc);

    if (rc != ESP_OK) {
        if (err[0]) reply_line("+MQTTCONN:ERROR,%s", err);
        reply_error();
        return;
    }

    reply_line("+MQTTCONN:BROKER,%s", uri);
    reply_line("+MQTTCONN:REBOOT_REQUIRED");
    reply_ok();
}

static void cmd_mqttpub(char *args)
{
    char *argv[AT_MAX_ARGS] = {0};
    int n = split_args(args, argv, AT_MAX_ARGS);
    if (n < 2) { reply_error(); return; }

    unescape(argv[0]);
    unescape(argv[1]);

    if (!mqtt_client_is_connected()) {
        reply_line("+MQTTPUB:NOT_CONNECTED");
        reply_error();
        return;
    }

    /* The AT topic is absolute; the client API takes a subtopic under the
     * configured prefix unless the caller passes a full path. Publish verbatim. */
    esp_err_t err = mqtt_client_publish_absolute(argv[0], argv[1], strlen(argv[1]), 1, false);
    if (err == ESP_OK) reply_ok(); else reply_error();
}

static void cmd_mqttsub(char *args)
{
    char *argv[AT_MAX_ARGS] = {0};
    int n = split_args(args, argv, AT_MAX_ARGS);
    if (n < 1) { reply_error(); return; }
    unescape(argv[0]);

    int qos = (n >= 2) ? atoi(argv[1]) : 1;
    esp_err_t err = mqtt_client_subscribe(argv[0], qos);
    if (err == ESP_OK) reply_ok(); else reply_error();
}

static void cmd_mqttunsub(char *args)
{
    if (args == NULL || *args == '\0') { reply_error(); return; }
    unescape(args);
    esp_err_t err = mqtt_client_unsubscribe(args);
    if (err == ESP_OK) reply_ok(); else reply_error();
}

/* --- OTA ------------------------------------------------------------------ */

static void cmd_otastart(char *args)
{
    if (args == NULL || *args == '\0') { reply_error(); return; }

    char *argv[AT_MAX_ARGS] = {0};
    int n = split_args(args, argv, AT_MAX_ARGS);
    if (n < 1) { reply_error(); return; }
    unescape(argv[0]);

    esp_err_t err = ota_service_start(argv[0]);
    if (err == ESP_OK) {
        reply_line("+OTASTART:OK");
        reply_line("+OTASTART:HINT,use AT+OTASTATUS? to follow progress");
        reply_ok();
    } else {
        reply_line("+OTASTART:ERROR,%s", esp_err_to_name(err));
        reply_error();
    }
}

static void cmd_otastatus(char *args)
{
    ota_status_t st;
    ota_service_get_status(&st);
    static const char *names[] = {
        "IDLE", "CONNECTING", "DOWNLOADING", "VERIFYING",
        "APPLYING", "REBOOTING", "SUCCESS", "FAILED"
    };
    reply_line("+OTASTATUS:%s,%d%%",
               names[st.state <= OTA_STATE_FAILED ? st.state : OTA_STATE_FAILED],
               st.progress);
    reply_line("+OTASTATUS:BYTES,%d,%d", st.bytes_read, st.total_size);
    if (st.error[0]) reply_line("+OTASTATUS:ERROR,%s", st.error);
    reply_ok();
}

/* --- mode / misc ---------------------------------------------------------- */

static void cmd_testmode(char *args)
{
    at_write("\r\nOK\r\n");
    test_mode_request();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
}

static void cmd_help(char *args)
{
    reply_line("+HELP:AT");
    reply_line("+HELP:AT+GMR");
    reply_line("+HELP:AT+ID");
    reply_line("+HELP:AT+RST");
    reply_line("+HELP:AT+RESTORE");
    reply_line("+HELP:AT+CWMODE=<1|2|3>");
    reply_line("+HELP:AT+CWJAP=\"<ssid>\",\"<pass>\"");
    reply_line("+HELP:AT+CWQAP");
    reply_line("+HELP:AT+CIFSR");
    reply_line("+HELP:AT+SYSTIME?");
    reply_line("+HELP:AT+HOSTNAME?");
    reply_line("+HELP:AT+CFG=[<yaml|json>]");
    reply_line("+HELP:AT+DEV=[\"<id>\"[,<json>]]");
    reply_line("+HELP:AT+DEVTYPE?");
    reply_line("+HELP:AT+MQTTCONN=[\"<host>\"[,port[,user,pass]]]");
    reply_line("+HELP:AT+MQTTPUB=\"<topic>\",\"<data>\"");
    reply_line("+HELP:AT+MQTTSUB=\"<topic>\"[,qos]");
    reply_line("+HELP:AT+MQTTUNSUB=\"<topic>\"");
    reply_line("+HELP:AT+OTASTART=\"<url>\"");
    reply_line("+HELP:AT+OTASTATUS?");
    reply_line("+HELP:AT+TESTMODE");
    reply_line("+HELP:AT+HELP?");
    reply_ok();
}

/* -------------------------------------------------------------------------- */
/* table                                                                      */
/* -------------------------------------------------------------------------- */

typedef struct {
    const char *name;          /* without the "AT" prefix, upper case */
    void (*handler)(char *args);
} at_cmd_t;

static const at_cmd_t s_commands[] = {
    { "",           cmd_test_at },
    { "+GMR",       cmd_gmr },
    { "+ID",        cmd_gmr_id },
    { "+RST",       cmd_rst },
    { "+RESTORE",   cmd_restore },
    { "+CWMODE",    cmd_cwmode },
    { "+CWJAP",     cmd_cwjap },
    { "+CWQAP",     cmd_cwqap },
    { "+CIFSR",     cmd_cifsr },
    { "+SYSTIME",   cmd_systime },
    { "+HOSTNAME",  cmd_hostname },
    { "+CFG",       cmd_cfg },
    { "+DEV",       cmd_dev },
    { "+DEVTYPE",   cmd_devlist },
    { "+MQTTCONN",  cmd_mqttconn },
    { "+MQTTPUB",   cmd_mqttpub },
    { "+MQTTSUB",   cmd_mqttsub },
    { "+MQTTUNSUB", cmd_mqttunsub },
    { "+OTASTART",  cmd_otastart },
    { "+OTASTATUS", cmd_otastatus },
    { "+TESTMODE",  cmd_testmode },
    { "+HELP",      cmd_help },
};

#define AT_CMD_COUNT (sizeof(s_commands) / sizeof(s_commands[0]))

/**
 * @brief Dispatch one command line
 *
 * Accepts the "AT" prefix case-insensitively and strips a trailing "?" so
 * queries reach the handler with args == NULL.
 */
static void at_dispatch(char *line)
{
    /* Strip leading whitespace */
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '\0') return;

    if (toupper((unsigned char)line[0]) != 'A' ||
        toupper((unsigned char)line[1]) != 'T') {
        reply_error();
        return;
    }

    char *p = line + 2;
    while (*p == ' ') p++;

    /* Separate the command name from its arguments at '=' or ':' */
    char *args = NULL;
    char *q = p;
    while (*q && *q != '=' && *q != ':' && *q != '?') q++;
    if (*q == '=' || *q == ':') {
        *q = '\0';
        args = q + 1;
    } else if (*q == '?') {
        *q = '\0';
        args = NULL;
    }

    /* Trim trailing spaces of the name */
    char *end = p + strlen(p);
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';

    for (size_t i = 0; i < AT_CMD_COUNT; i++) {
        if (at_streq(p, s_commands[i].name)) {
            s_commands[i].handler(args);
            return;
        }
    }

    at_printf("\r\n+ERROR:unsupported command \"%s\"\r\n", p);
    reply_error();
}

/* -------------------------------------------------------------------------- */
/* RX task                                                                    */
/* -------------------------------------------------------------------------- */

static void at_task(void *arg)
{
    char line[AT_BUF_SIZE];
    size_t len = 0;

    at_write("\r\nready\r\n");

    while (s_running) {
        uint8_t ch;
        int n = uart_read_bytes(AT_UART, &ch, 1, pdMS_TO_TICKS(100));
        if (n <= 0) continue;

        /* ESP-AT terminates with CR+LF; accept either on its own too, and
         * ignore a bare LF that follows a CR. */
        if (ch == '\r' || ch == '\n') {
            if (len == 0) continue;
            line[len] = '\0';
            at_dispatch(line);
            len = 0;
            continue;
        }

        /* Backspace for interactive terminals */
        if (ch == 0x08 || ch == 0x7F) {
            if (len > 0) len--;
            continue;
        }

        if (len < sizeof(line) - 1) {
            line[len++] = (char)ch;
        } else {
            /* Overlong line: drop it rather than executing a truncated
             * command, which could apply a half-written configuration. */
            len = 0;
            at_printf("\r\n+ERROR:line too long\r\n");
            reply_error();
        }
    }

    s_task = NULL;
    vTaskDelete(NULL);
}

/* -------------------------------------------------------------------------- */
/* public                                                                     */
/* -------------------------------------------------------------------------- */

esp_err_t at_service_start(void)
{
    if (s_running) {
        return ESP_OK;
    }

    if (s_tx_lock == NULL) {
        s_tx_lock = xSemaphoreCreateMutex();
        if (s_tx_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    uart_config_t cfg = {
        .baud_rate = CONFIG_ESPX_AT_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(AT_UART, AT_RX_BUF, AT_TX_BUF, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(AT_UART, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Pin routing.
     *
     * uart_set_pin() is what attaches the UART's internal signals to pads. It is
     * NOT optional when using the "default" pins: out of reset GPIO17/18 are
     * plain GPIO, not UART1, and skipping the call leaves UART1 driving nothing
     * at all -- the port then looks dead (zero bytes on the wire) while the log
     * cheerfully reports which pins it intended to use.
     *
     * Passing the UART's own IO_MUX pins selects the direct IO_MUX path; passing
     * other pins routes through the GPIO matrix. Both go through this call.
     *
     * The one case that must not call it is when AT shares the console's UART:
     * the console already configured those pads, and re-pinning would move the
     * log output onto the AT pins.
     */
#if (CONFIG_ESPX_AT_UART_NUM == CONFIG_ESP_CONSOLE_UART_NUM) && \
    defined(CONFIG_ESP_CONSOLE_UART_DEFAULT)
    ESP_LOGW(TAG, "AT shares the console UART%d; keeping its pins %d/%d. Log "
                  "output will interleave with AT replies -- manual debugging only.",
             CONFIG_ESPX_AT_UART_NUM,
             CONFIG_ESP_CONSOLE_UART_TX_GPIO, CONFIG_ESP_CONSOLE_UART_RX_GPIO);
#else
    {
#if defined(CONFIG_ESPX_AT_USE_DEFAULT_PINS)
    #if (CONFIG_ESPX_AT_UART_NUM == 2)
        #error "UART2 has no IO_MUX pins: disable ESPX_AT_USE_DEFAULT_PINS and set explicit TX/RX GPIOs."
    #endif
        const int tx_pin = AT_DEFAULT_TX_PIN;
        const int rx_pin = AT_DEFAULT_RX_PIN;
#else
        const int tx_pin = CONFIG_ESPX_AT_UART_TX_GPIO;
        const int rx_pin = CONFIG_ESPX_AT_UART_RX_GPIO;
#endif
        err = uart_set_pin(AT_UART, tx_pin, rx_pin,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "uart_set_pin(%d, %d) failed: %s",
                     tx_pin, rx_pin, esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "AT pins routed: UART%d TX=%d RX=%d%s",
                 CONFIG_ESPX_AT_UART_NUM, tx_pin, rx_pin,
                 (tx_pin == AT_DEFAULT_TX_PIN && rx_pin == AT_DEFAULT_RX_PIN)
                     ? " (IO_MUX defaults)" : "");
    }
#endif

    s_running = true;
    if (/* Internal-RAM stack: AT+CFG writes NVS. See task_util.h. */
    xTaskCreate(at_task, "at_service", 6144, NULL, 5, &s_task) != pdPASS) {
        s_running = false;
        ESP_LOGE(TAG, "failed to start the AT task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "AT service ready on UART%d, %d baud",
             CONFIG_ESPX_AT_UART_NUM, CONFIG_ESPX_AT_UART_BAUD);
    return ESP_OK;
}

esp_err_t at_service_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }
    s_running = false;
    vTaskDelay(pdMS_TO_TICKS(300));
    uart_driver_delete(AT_UART);
    ESP_LOGI(TAG, "AT service stopped");
    return ESP_OK;
}

bool at_service_is_running(void)
{
    return s_running;
}
