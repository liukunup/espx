/**
 * @file wifi_prov.c
 * @brief Wi-Fi Provisioning Manager implementation
 *
 * Uses ESP-IDF network_provisioning component.
 */

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_event.h>
#include <nvs_flash.h>

#include <network_provisioning/manager.h>
#include <cJSON.h>
#include <network_provisioning/scheme_softap.h>
#if WIFI_PROV_TRANSPORT == WIFI_PROV_TRANSPORT_BLE
#include <network_provisioning/scheme_ble.h>
#endif

#include "qrcode.h"
#include "wifi_prov.h"
#include "node_config.h"

static const char *TAG = "wifi_prov";

/* ============================================
 * Security 2 salt/verifier (dev mode)
 * ============================================ */
#if WIFI_PROV_SECURITY_VERSION == 2 && WIFI_PROV_SEC2_DEV_MODE
/* Generated for username = "wifiprov", password = "abcd1234" */
static const char sec2_salt[] = {
    0x03, 0x6e, 0xe0, 0xc7, 0xbc, 0xb9, 0xed, 0xa8, 0x4c, 0x9e, 0xac, 0x97, 0xd9, 0x3d, 0xec, 0xf4
};

static const char sec2_verifier[] = {
    0x7c, 0x7c, 0x85, 0x47, 0x65, 0x08, 0x94, 0x6d, 0xd6, 0x36, 0xaf, 0x37, 0xd7, 0xe8, 0x91, 0x43,
    0x78, 0xcf, 0xfd, 0x61, 0x6c, 0x59, 0xd2, 0xf8, 0x39, 0x08, 0x12, 0x72, 0x38, 0xde, 0x9e, 0x24,
    0xa4, 0x70, 0x26, 0x1c, 0xdf, 0xa9, 0x03, 0xc2, 0xb2, 0x70, 0xe7, 0xb1, 0x32, 0x24, 0xda, 0x11,
    0x1d, 0x97, 0x18, 0xdc, 0x60, 0x72, 0x08, 0xcc, 0x9a, 0xc9, 0x0c, 0x48, 0x27, 0xe2, 0xae, 0x89,
    0xaa, 0x16, 0x25, 0xb8, 0x04, 0xd2, 0x1a, 0x9b, 0x3a, 0x8f, 0x37, 0xf6, 0xe4, 0x3a, 0x71, 0x2e,
    0xe1, 0x27, 0x86, 0x6e, 0xad, 0xce, 0x28, 0xff, 0x54, 0x46, 0x60, 0x1f, 0xb9, 0x96, 0x87, 0xdc,
    0x57, 0x40, 0xa7, 0xd4, 0x6c, 0xc9, 0x77, 0x54, 0xdc, 0x16, 0x82, 0xf0, 0xed, 0x35, 0x6a, 0xc4,
    0x70, 0xad, 0x3d, 0x90, 0xb5, 0x81, 0x94, 0x70, 0xd7, 0xbc, 0x65, 0xb2, 0xd5, 0x18, 0xe0, 0x2e,
    0xc3, 0xa5, 0xf9, 0x68, 0xdd, 0x64, 0x7b, 0xb8, 0xb7, 0x3c, 0x9c, 0xfc, 0x00, 0xd8, 0x71, 0x7e,
    0xb7, 0x9a, 0x7c, 0xb1, 0xb7, 0xc2, 0xc3, 0x18, 0x34, 0x29, 0x32, 0x43, 0x3e, 0x00, 0x99, 0xe9,
    0x82, 0x94, 0xe3, 0xd8, 0x2a, 0xb0, 0x96, 0x29, 0xb7, 0xdf, 0x0e, 0x5f, 0x08, 0x33, 0x40, 0x76,
    0x52, 0x91, 0x32, 0x00, 0x9f, 0x97, 0x2c, 0x89, 0x6c, 0x39, 0x1e, 0xc8, 0x28, 0x05, 0x44, 0x17,
    0x3f, 0x68, 0x02, 0x8a, 0x9f, 0x44, 0x61, 0xd1, 0xf5, 0xa1, 0x7e, 0x5a, 0x70, 0xd2, 0xc7, 0x23,
    0x81, 0xcb, 0x38, 0x68, 0xe4, 0x2c, 0x20, 0xbc, 0x40, 0x57, 0x76, 0x17, 0xbd, 0x08, 0xb8, 0x96,
    0xbc, 0x26, 0xeb, 0x32, 0x46, 0x69, 0x35, 0x05, 0x8c, 0x15, 0x70, 0xd9, 0x1b, 0xe9, 0xbe, 0xcc,
    0xa9, 0x38, 0xa6, 0x67, 0xf0, 0xad, 0x50, 0x13, 0x19, 0x72, 0x64, 0xbf, 0x52, 0xc2, 0x34, 0xe2,
    0x1b, 0x11, 0x79, 0x74, 0x72, 0xbd, 0x34, 0x5b, 0xb1, 0xe2, 0xfd, 0x66, 0x73, 0xfe, 0x71, 0x64,
    0x74, 0xd0, 0x4e, 0xbc, 0x51, 0x24, 0x19, 0x40, 0x87, 0x0e, 0x92, 0x40, 0xe6, 0x21, 0xe7, 0x2d,
    0x4e, 0x37, 0x76, 0x2f, 0x2e, 0xe2, 0x68, 0xc7, 0x89, 0xe8, 0x32, 0x13, 0x42, 0x06, 0x84, 0x84,
    0x53, 0x4a, 0xb3, 0x0c, 0x1b, 0x4c, 0x8d, 0x1c, 0x51, 0x97, 0x19, 0xab, 0xae, 0x77, 0xff, 0xdb,
    0xec, 0xf0, 0x10, 0x95, 0x34, 0x33, 0x6b, 0xcb, 0x3e, 0x84, 0x0f, 0xb9, 0xd8, 0x5f, 0xb8, 0xa0,
    0xb8, 0x55, 0x53, 0x3e, 0x70, 0xf7, 0x18, 0xf5, 0xce, 0x7b, 0x4e, 0xbf, 0x27, 0xce, 0xce, 0xa8,
    0xb3, 0xbe, 0x40, 0xc5, 0xc5, 0x32, 0x29, 0x3e, 0x71, 0x64, 0x9e, 0xde, 0x8c, 0xf6, 0x75, 0xa1,
    0xe6, 0xf6, 0x53, 0xc8, 0x31, 0xa8, 0x78, 0xde, 0x50, 0x40, 0xf7, 0x62, 0xde, 0x36, 0xb2, 0xba
};
#endif

#define PROV_QR_VERSION         "v1"
#define PROV_TRANSPORT_SOFTAP   "softap"
#define PROV_TRANSPORT_BLE      "ble"
#define QRCODE_BASE_URL         "https://espressif.github.io/esp-jumpstart/qrcode.html"

/* Signal Wi-Fi connect on this event-group */
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_EVENT    BIT0

static bool s_initialized = false;
static bool s_provisioned = false;
static wifi_prov_event_handler_t *s_app_handler = NULL;

/* ============================================
 * Helpers
 * ============================================ */

static void notify_app(wifi_prov_event_t event, void *event_data)
{
    if (s_app_handler && s_app_handler->event_cb) {
        s_app_handler->event_cb(event, event_data, s_app_handler->user_data);
    }
}

/**
 * @brief Provisioning service name
 *
 * SoftAP: the SSID of the raised access point (prefix + last 3 MAC bytes).
 * BLE   : the advertised device name.
 */
static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac);
#if WIFI_PROV_TRANSPORT == WIFI_PROV_TRANSPORT_SOFTAP
    snprintf(service_name, max, "%s%02X%02X%02X",
             WIFI_PROV_SOFTAP_SSID_PREFIX, mac[3], mac[4], mac[5]);
#else
    snprintf(service_name, max, "%s%02X%02X%02X",
             "ESPX_", mac[3], mac[4], mac[5]);
#endif
}

#if WIFI_PROV_SECURITY_VERSION == 2
static esp_err_t get_sec2_salt(const char **salt, uint16_t *salt_len)
{
#if WIFI_PROV_SEC2_DEV_MODE
    *salt = sec2_salt;
    *salt_len = sizeof(sec2_salt);
    return ESP_OK;
#else
    return ESP_FAIL;
#endif
}

static esp_err_t get_sec2_verifier(const char **verifier, uint16_t *verifier_len)
{
#if WIFI_PROV_SEC2_DEV_MODE
    *verifier = sec2_verifier;
    *verifier_len = sizeof(sec2_verifier);
    return ESP_OK;
#else
    return ESP_FAIL;
#endif
}
#endif /* WIFI_PROV_SECURITY_VERSION == 2 */

/* ============================================
 * Event handler
 * ============================================ */

static void prov_event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == NETWORK_PROV_EVENT) {
        switch (event_id) {
        case NETWORK_PROV_START:
            ESP_LOGI(TAG, "Provisioning started");
            break;

        case NETWORK_PROV_WIFI_CRED_RECV: {
            wifi_sta_config_t *cfg = (wifi_sta_config_t *)event_data;
            ESP_LOGI(TAG, "Received credentials: SSID=%.*s",
                     (int)sizeof(cfg->ssid), (const char *)cfg->ssid);
            notify_app(WIFI_PROV_EVENT_WIFI_CRED_RECV, event_data);
            break;
        }

        case NETWORK_PROV_WIFI_CRED_FAIL: {
            network_prov_wifi_sta_fail_reason_t *reason =
                (network_prov_wifi_sta_fail_reason_t *)event_data;
            ESP_LOGE(TAG, "Provisioning failed: %s",
                     (*reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR) ?
                     "auth error" : "AP not found");
#if WIFI_PROV_RESET_ON_FAILURE
            network_prov_mgr_reset_wifi_sm_state_on_failure();
#endif
            notify_app(WIFI_PROV_EVENT_WIFI_CRED_FAIL, event_data);
            break;
        }

        case NETWORK_PROV_WIFI_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning successful");
            notify_app(WIFI_PROV_EVENT_WIFI_CRED_SUCCESS, event_data);
            break;

        case NETWORK_PROV_END:
            ESP_LOGI(TAG, "Provisioning ended");
            notify_app(WIFI_PROV_EVENT_PROVISIONING_END, event_data);
            network_prov_mgr_deinit();
            break;

        default:
            break;
        }
    } else if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "Disconnected, reconnecting...");
            notify_app(WIFI_PROV_EVENT_DISCONNECTED, event_data);
            esp_wifi_connect();
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_EVENT);
        notify_app(WIFI_PROV_EVENT_CONNECTED, event_data);
    }
}

/* ============================================
 * QR code
 * ============================================ */

static void wifi_prov_print_qr(const char *name, const char *username,
                               const char *pop, const char *transport)
{
    if (!name || !transport) {
        ESP_LOGW(TAG, "Cannot generate QR code, missing data");
        return;
    }

    /* The PoP is only embedded when explicitly enabled: a printed QR code
     * containing the shared secret defeats the point of the secret. */
#if WIFI_PROV_SHOW_POP_IN_QR
    const char *qr_pop = pop;
#else
    const char *qr_pop = NULL;
#endif

    char payload[200] = {0};
    if (qr_pop) {
        snprintf(payload, sizeof(payload),
                 "{\"ver\":\"%s\",\"name\":\"%s\",\"username\":\"%s\",\"pop\":\"%s\",\"transport\":\"%s\"}",
                 PROV_QR_VERSION, name, username ? username : "", qr_pop, transport);
    } else {
        snprintf(payload, sizeof(payload),
                 "{\"ver\":\"%s\",\"name\":\"%s\",\"transport\":\"%s\",\"network\":\"wifi\"}",
                 PROV_QR_VERSION, name, transport);
    }

#if WIFI_PROV_SHOW_QR
    ESP_LOGI(TAG, "Scan this QR code for provisioning:");
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    esp_qrcode_generate(&cfg, payload);
#endif

    ESP_LOGI(TAG, "Or open: %s?data=%s", QRCODE_BASE_URL, payload);
}

/**
 * @brief Optional custom provisioning endpoint
 */
static esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf,
                                          ssize_t inlen, uint8_t **outbuf,
                                          ssize_t *outlen, void *priv_data)
{
    if (inbuf) {
        ESP_LOGI(TAG, "Received custom data: %.*s", (int)inlen, (const char *)inbuf);
    }

    const char *response = "SUCCESS";
    *outbuf = (uint8_t *)strdup(response);
    if (*outbuf == NULL) {
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(response) + 1;
    return ESP_OK;
}

/**
 * @brief Connect using credentials pre-provisioned by factory data
 *
 * A factory-programmed unit should come up on the plant network without an
 * operator running SoftAP provisioning. If node_config carries
 * network.wifi_ssid (and password), configure the station directly.
 *
 * @return true when credentials were found and the station was started
 */
static bool start_with_preconfigured_credentials(void)
{
    char ssid[33] = {0};
    char password[65] = {0};

    cJSON *cfg = node_config_get();
    if (cfg == NULL) {
        return false;
    }

    cJSON *net = cJSON_GetObjectItem(cfg, "network");
    if (cJSON_IsObject(net)) {
        cJSON *v = cJSON_GetObjectItem(net, "wifi_ssid");
        if (cJSON_IsString(v) && v->valuestring[0] != '\0') {
            strncpy(ssid, v->valuestring, sizeof(ssid) - 1);
        }
        v = cJSON_GetObjectItem(net, "wifi_password");
        if (cJSON_IsString(v)) {
            strncpy(password, v->valuestring, sizeof(password) - 1);
        }
    }
    cJSON_Delete(cfg);

    if (ssid[0] == '\0') {
        return false;
    }

    ESP_LOGI(TAG, "Using pre-provisioned Wi-Fi credentials for SSID '%s'", ssid);

    wifi_config_t wc = {0};
    strncpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid) - 1);
    if (password[0] != '\0') {
        strncpy((char *)wc.sta.password, password, sizeof(wc.sta.password) - 1);
        wc.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    } else {
        wc.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &prov_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());

    return true;
}

/* ============================================
 * Public API
 * ============================================ */

esp_err_t wifi_prov_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    /* NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* TCP/IP + event loop */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_wifi_event_group = xEventGroupCreate();

    /* Register event handlers */
    ESP_ERROR_CHECK(esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID,
                                                &prov_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT,
                                                ESP_EVENT_ANY_ID, &prov_event_handler, NULL));

    /* Wi-Fi netifs */
    esp_netif_create_default_wifi_sta();
#if WIFI_PROV_TRANSPORT == WIFI_PROV_TRANSPORT_SOFTAP
    /* Only the SoftAP transport needs an AP netif. The BLE transport creates no
     * access point at all, so the device has no IP until it joins Wi-Fi. */
    esp_netif_create_default_wifi_ap();
#endif

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    s_initialized = true;
    ESP_LOGI(TAG, "Wi-Fi provisioning initialized");
    return ESP_OK;
}

esp_err_t wifi_prov_start(wifi_prov_event_handler_t *event_handler)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_app_handler = event_handler;

    /* Provisioning manager configuration */
    network_prov_mgr_config_t config = {
#if WIFI_PROV_RESET_ON_FAILURE
        .network_prov_wifi_conn_cfg = {
            .wifi_conn_attempts = WIFI_PROV_CONNECTION_COUNT,
        },
#endif
#if WIFI_PROV_TRANSPORT == WIFI_PROV_TRANSPORT_BLE
        .scheme = network_prov_scheme_ble,
        .scheme_event_handler = NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
#else
        .scheme = network_prov_scheme_softap,
        .scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE,
#endif
    };

    ESP_ERROR_CHECK(network_prov_mgr_init(config));

    /* Check if already provisioned */
    bool provisioned = false;
    ESP_ERROR_CHECK(network_prov_mgr_is_wifi_provisioned(&provisioned));
    s_provisioned = provisioned;

    /* Factory-programmed credentials take precedence over interactive
     * provisioning: no operator needed on the production line. */
    if (!provisioned && start_with_preconfigured_credentials()) {
        network_prov_mgr_deinit();
        return ESP_OK;
    }

    if (!provisioned) {
        ESP_LOGI(TAG, "Starting provisioning");

        char service_name[16];
        get_device_service_name(service_name, sizeof(service_name));

#if WIFI_PROV_SECURITY_VERSION == 2
        network_prov_security_t security = NETWORK_PROV_SECURITY_2;

#if WIFI_PROV_SEC2_DEV_MODE
        const char *username = WIFI_PROV_SEC2_USERNAME;
        const char *pop = WIFI_PROV_SEC2_PASSWORD;
#else
        const char *username = NULL;
        const char *pop = NULL;
#endif

        network_prov_security2_params_t sec2_params = {0};
        ESP_ERROR_CHECK(get_sec2_salt(&sec2_params.salt, &sec2_params.salt_len));
        ESP_ERROR_CHECK(get_sec2_verifier(&sec2_params.verifier, &sec2_params.verifier_len));
        const void *sec_params = &sec2_params;

#elif WIFI_PROV_SECURITY_VERSION == 1
        network_prov_security_t security = NETWORK_PROV_SECURITY_1;
        const char *username = NULL;
        const char *pop = WIFI_PROV_POP;
        const void *sec_params = (const void *)pop;
#else
        network_prov_security_t security = NETWORK_PROV_SECURITY_0;
        const char *username = NULL;
        const char *pop = NULL;
        const void *sec_params = NULL;
#endif

        const char *service_key = NULL;

#if WIFI_PROV_TRANSPORT == WIFI_PROV_TRANSPORT_BLE
        uint8_t custom_service_uuid[] = {
            0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
            0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
        };
        network_prov_scheme_ble_set_service_uuid(custom_service_uuid);
#endif

        network_prov_mgr_endpoint_create("custom-data");

#if WIFI_PROV_REPROVISIONING
        network_prov_mgr_disable_auto_stop(1000);
#endif

        ESP_ERROR_CHECK(network_prov_mgr_start_provisioning(security, sec_params,
                                                             service_name, service_key));
        network_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler, NULL);

#if WIFI_PROV_TRANSPORT == WIFI_PROV_TRANSPORT_BLE
        wifi_prov_print_qr(service_name, username, pop, PROV_TRANSPORT_BLE);
#else
        wifi_prov_print_qr(service_name, username, pop, PROV_TRANSPORT_SOFTAP);
#endif

        ESP_LOGI(TAG, "Provisioning service started. Connect and configure Wi-Fi.");
    } else {
        ESP_LOGI(TAG, "Already provisioned, starting Wi-Fi STA");

        ESP_ERROR_CHECK(network_prov_mgr_deinit());

        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                    &prov_event_handler, NULL));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    return ESP_OK;
}

void wifi_prov_wait_for_connection(void)
{
    if (s_wifi_event_group == NULL) {
        return;
    }
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_EVENT, true, true,
                        portMAX_DELAY);
}

void wifi_prov_deinit(void)
{
    /* Manager is de-initialized automatically on NETWORK_PROV_END */
    ESP_LOGI(TAG, "Wi-Fi provisioning deinitialized");
}

void wifi_prov_reset(void)
{
    ESP_LOGW(TAG, "Resetting provisioning state");

    /* The provisioning manager is de-initialised once provisioning ends, so
     * these calls only make sense while it is still alive. */
    if (!s_initialized) {
        ESP_LOGE(TAG, "Cannot reset: provisioning not initialized");
        return;
    }

    esp_err_t err = network_prov_mgr_reset_wifi_provisioning();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "reset_wifi_provisioning failed: %s", esp_err_to_name(err));
        return;
    }

    err = network_prov_mgr_reset_wifi_sm_state_for_reprovision();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "reset state machine failed: %s", esp_err_to_name(err));
    }
}

bool wifi_prov_is_provisioned(void)
{
    return s_provisioned;
}
