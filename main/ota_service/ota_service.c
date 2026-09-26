/**
 * @file ota_service.c
 * @brief Delta OTA service implementation
 *
 * Flow:
 *   1. GET patch from URL
 *   2. Read 64-byte patch header, verify magic + SHA256(current firmware)
 *   3. esp_ota_begin(next partition)
 *   4. Stream patch through esp_delta_ota -> write_cb -> esp_ota_write
 *   5. esp_delta_ota_finalize, esp_ota_end, esp_ota_set_boot_partition
 *   6. Reboot
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_ota_ops.h>
#include <esp_app_desc.h>
#include <esp_partition.h>
#include <esp_http_client.h>
#include <esp_delta_ota.h>
#include <cJSON.h>

#if __has_include("esp_crt_bundle.h")
#include "esp_crt_bundle.h"
#define ESPX_HAVE_CRT_BUNDLE 1
#else
#define ESPX_HAVE_CRT_BUNDLE 0
#endif

#include "ota_service.h"

static const char *TAG = "ota_service";

#define PATCH_HEADER_SIZE   64
#define DIGEST_SIZE         32
#define BUFFSIZE            1024
#define PATCH_MAGIC         0xfccdde10

/* Shared state (guarded by s_lock for the simple fields we expose) */
static ota_status_t s_status;
static volatile bool s_running = false;
static volatile bool s_cancel = false;
static TaskHandle_t s_task = NULL;
static ota_progress_cb_t s_progress_cb = NULL;
static void *s_progress_user = NULL;

/* Partition / handle state used by the callbacks */
static const esp_partition_t *s_src_partition = NULL;
static esp_ota_handle_t s_ota_handle = 0;

/**
 * @brief Per-update context passed to the delta callbacks
 *
 * Kept out of function-static storage so a second update cannot inherit the
 * chip-id verification state of a previous (possibly failed) run.
 */
typedef struct {
    bool chip_id_verified;
    int header_read;
    uint8_t header[sizeof(esp_image_header_t)];
} ota_ctx_t;

/* ============================================
 * Helpers
 * ============================================ */

static void set_state(ota_state_t state, const char *error)
{
    s_status.state = state;
    if (error) {
        strncpy(s_status.error, error, sizeof(s_status.error) - 1);
        s_status.error[sizeof(s_status.error) - 1] = '\0';
    }
    if (s_progress_cb) {
        s_progress_cb(&s_status, s_progress_user);
    }
}

static void fail(const char *fmt, ...)
{
    char msg[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    ESP_LOGE(TAG, "OTA failed: %s", msg);
    set_state(OTA_STATE_FAILED, msg);
}

/* ============================================
 * Delta OTA callbacks
 * ============================================ */

static esp_err_t delta_read_cb(uint8_t *buf_p, size_t size, int src_offset, void *user_data)
{
    if (size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_partition_read(s_src_partition, src_offset, buf_p, size);
}

static esp_err_t delta_write_cb(const uint8_t *buf_p, size_t size, void *user_data)
{
    ota_ctx_t *ctx = (ota_ctx_t *)user_data;

    if (size == 0 || ctx == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t hdr_len = sizeof(esp_image_header_t);
    size_t index = 0;

    if (!ctx->chip_id_verified) {
        if ((size_t)ctx->header_read + size <= hdr_len) {
            memcpy(ctx->header + ctx->header_read, buf_p, size);
            ctx->header_read += size;
            return ESP_OK;
        }

        index = hdr_len - (size_t)ctx->header_read;
        memcpy(ctx->header + ctx->header_read, buf_p, index);

        const esp_image_header_t *header = (const esp_image_header_t *)ctx->header;
        if (header->chip_id != CONFIG_IDF_FIRMWARE_CHIP_ID) {
            ESP_LOGE(TAG, "Patch targets chip id %d, expected %d",
                     header->chip_id, CONFIG_IDF_FIRMWARE_CHIP_ID);
            return ESP_ERR_INVALID_VERSION;
        }
        ctx->chip_id_verified = true;

        esp_err_t err = esp_ota_write(s_ota_handle, ctx->header, hdr_len);
        if (err != ESP_OK) {
            return err;
        }
    }

    return esp_ota_write(s_ota_handle, buf_p + index, size - index);
}

/* ============================================
 * Patch header verification
 * ============================================ */

static bool verify_patch_header(const uint8_t *hdr, const char **reason)
{
    uint32_t magic;
    memcpy(&magic, hdr, sizeof(magic));

    if (magic != PATCH_MAGIC) {
        *reason = "invalid patch magic";
        return false;
    }

    const uint8_t *digest = hdr + 4;
    uint8_t sha256[DIGEST_SIZE] = {0};

    esp_err_t err = esp_partition_get_sha256(s_src_partition, sha256);
    if (err != ESP_OK) {
        *reason = "cannot hash running firmware";
        return false;
    }

    if (memcmp(sha256, digest, DIGEST_SIZE) != 0) {
        *reason = "patch was built for a different firmware version";
        return false;
    }

    return true;
}

/* ============================================
 * OTA task
 * ============================================ */

static void ota_task(void *arg)
{
    esp_err_t err;
    const char *reason = NULL;
    char *url = (char *)arg;
    char *buf = NULL;
    esp_http_client_handle_t client = NULL;
    ota_ctx_t ctx = {0};

    s_status.progress = 0;
    s_status.bytes_read = 0;
    s_status.total_size = -1;
    s_status.error[0] = '\0';

    const esp_app_desc_t *app = esp_app_get_description();
    if (app) {
        strncpy(s_status.running_version, app->version,
                sizeof(s_status.running_version) - 1);
    }

    strncpy(s_status.url, url, sizeof(s_status.url) - 1);
    s_status.url[sizeof(s_status.url) - 1] = '\0';

    buf = malloc(BUFFSIZE + 1);
    if (buf == NULL) {
        fail("out of memory");
        goto cleanup;
    }

    /* --- Partitions --- */
    s_src_partition = esp_ota_get_running_partition();
    const esp_partition_t *dst = esp_ota_get_next_update_partition(NULL);

    if (s_src_partition == NULL || dst == NULL) {
        fail("cannot resolve partitions");
        goto cleanup;
    }

    ESP_LOGI(TAG, "Source partition: %s, destination: %s",
             s_src_partition->label, dst->label);

    /* --- HTTP client --- */
    set_state(OTA_STATE_CONNECTING, NULL);

    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
        .buffer_size = BUFFSIZE,
    };
#if ESPX_HAVE_CRT_BUNDLE
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
#endif

    client = esp_http_client_init(&cfg);
    if (client == NULL) {
        fail("HTTP client init failed");
        goto cleanup;
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        fail("HTTP open failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    int content_len = esp_http_client_fetch_headers(client);
    if (content_len > 0) {
        s_status.total_size = content_len;
    }

    if (esp_http_client_get_status_code(client) != 200) {
        fail("HTTP %d", esp_http_client_get_status_code(client));
        goto cleanup;
    }

    /* --- Begin OTA --- */
    err = esp_ota_begin(dst, OTA_SIZE_UNKNOWN, &s_ota_handle);
    if (err != ESP_OK) {
        fail("esp_ota_begin: %s", esp_err_to_name(err));
        goto cleanup;
    }

    /* --- Delta OTA handle --- */
    set_state(OTA_STATE_VERIFYING, NULL);

    int read = esp_http_client_read(client, buf, PATCH_HEADER_SIZE);
    if (read != PATCH_HEADER_SIZE) {
        fail("short patch header (%d bytes)", read);
        goto abort_ota;
    }

    if (!verify_patch_header((const uint8_t *)buf, &reason)) {
        fail("%s", reason);
        goto abort_ota;
    }
    ESP_LOGI(TAG, "Patch header verified against running firmware");

    esp_delta_ota_cfg_t dcfg = {
        .read_cb_with_user_data = delta_read_cb,
        .write_cb_with_user_data = delta_write_cb,
        .user_data = &ctx,
    };

    esp_delta_ota_handle_t dh = esp_delta_ota_init(&dcfg);
    if (dh == NULL) {
        fail("esp_delta_ota_init failed");
        goto abort_ota;
    }

    /* --- Stream patch --- */
    set_state(OTA_STATE_DOWNLOADING, NULL);

    while (1) {
        if (s_cancel) {
            fail("cancelled by user");
            esp_delta_ota_deinit(dh);
            goto abort_ota;
        }

        read = esp_http_client_read(client, buf, BUFFSIZE);
        if (read < 0) {
            fail("read error");
            esp_delta_ota_deinit(dh);
            goto abort_ota;
        }

        if (read == 0) {
            if (esp_http_client_is_complete_data_received(client)) {
                break;
            }
            if (errno == ECONNRESET || errno == ENOTCONN) {
                fail("connection reset");
                esp_delta_ota_deinit(dh);
                goto abort_ota;
            }
            continue;
        }

        if (esp_delta_ota_feed_patch(dh, (const uint8_t *)buf, read) != ESP_OK) {
            fail("patch application failed");
            esp_delta_ota_deinit(dh);
            goto abort_ota;
        }

        s_status.bytes_read += read;
        if (s_status.total_size > 0) {
            int p = (int)((int64_t)s_status.bytes_read * 100 / s_status.total_size);
            s_status.progress = p > 100 ? 100 : p;
        }
        if (s_progress_cb) {
            s_progress_cb(&s_status, s_progress_user);
        }
    }

    /* --- Finalize --- */
    set_state(OTA_STATE_APPLYING, NULL);

    err = esp_delta_ota_finalize(dh);
    if (err != ESP_OK) {
        fail("esp_delta_ota_finalize: %s", esp_err_to_name(err));
        esp_delta_ota_deinit(dh);
        goto abort_ota;
    }
    esp_delta_ota_deinit(dh);

    err = esp_ota_end(s_ota_handle);
    if (err != ESP_OK) {
        fail("esp_ota_end: %s", esp_err_to_name(err));
        goto cleanup;
    }

    err = esp_ota_set_boot_partition(dst);
    if (err != ESP_OK) {
        fail("esp_ota_set_boot_partition: %s", esp_err_to_name(err));
        goto cleanup;
    }

    ESP_LOGI(TAG, "OTA applied successfully, rebooting into %s", dst->label);
    s_status.progress = 100;
    set_state(OTA_STATE_REBOOTING, NULL);

    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        client = NULL;
    }
    free(buf);
    free(url);

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return;

abort_ota:
    esp_ota_abort(s_ota_handle);

cleanup:
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    free(buf);
    free(url);
    s_running = false;
    s_cancel = false;
    s_task = NULL;
    vTaskDelete(NULL);
}

/* ============================================
 * Public API
 * ============================================ */

esp_err_t ota_service_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = OTA_STATE_IDLE;
    s_status.total_size = -1;
    s_running = false;
    s_cancel = false;

    /* Populate the running version up front. The OTA task also sets it, but a
     * client polling /api/ota/status before any update would otherwise see an
     * empty string instead of the firmware it is talking to. */
    const esp_app_desc_t *app = esp_app_get_description();
    if (app) {
        strncpy(s_status.running_version, app->version,
                sizeof(s_status.running_version) - 1);
    }

    ESP_LOGI(TAG, "OTA service initialized (delta OTA)");
    return ESP_OK;
}

esp_err_t ota_service_start(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_running) {
        return ESP_ERR_INVALID_STATE;
    }

    s_running = true;
    s_cancel = false;

    char *url_copy = strdup(url);
    if (url_copy == NULL) {
        s_running = false;
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(ota_task, "ota_task", 8192, url_copy, 5, &s_task) != pdPASS) {
        free(url_copy);
        s_running = false;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA started: %s", url);
    return ESP_OK;
}

esp_err_t ota_service_cancel(void)
{
    if (!s_running) {
        return ESP_OK;
    }
    s_cancel = true;
    ESP_LOGW(TAG, "OTA cancel requested");
    return ESP_OK;
}

esp_err_t ota_service_get_status(ota_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *status = s_status;
    return ESP_OK;
}

bool ota_service_is_running(void)
{
    return s_running;
}

void ota_service_set_progress_cb(ota_progress_cb_t cb, void *user_data)
{
    s_progress_cb = cb;
    s_progress_user = user_data;
}

esp_err_t ota_service_mark_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    esp_err_t err;

    if (running == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    err = esp_ota_get_state_partition(running, &state);
    if (err != ESP_OK) {
        return err;
    }

    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Firmware marked valid, rollback cancelled");
        } else {
            ESP_LOGE(TAG, "Failed to mark valid: %s", esp_err_to_name(err));
        }
        return err;
    }

    return ESP_OK;
}
