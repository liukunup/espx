/**
 * @file ota_service.c
 * @brief ESP OTA Service 封装层实现
 * 
 * 使用 ESP-IDF esp_https_ota 组件
 */

#include "ota_service.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_https_ota.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "ota_service";

/** @brief OTA 状态 */
static volatile ota_service_state_t g_ota_state = OTA_SERVICE_STATE_IDLE;

/** @brief 进度百分比 */
static volatile int g_progress = 0;

/** @brief 上次错误 */
static volatile int g_last_error = 0;

/** @brief 目标版本 */
static char g_target_version[32] = {0};

/** @brief 进度回调 */
static ota_service_progress_callback_t g_progress_callback = NULL;
static void *g_user_data = NULL;

/** @brief 发送状态更新 */
static void notify_progress(void) {
    if (g_progress_callback != NULL) {
        ota_service_status_t status = {
            .progress_percent = g_progress,
            .state = g_ota_state,
            .last_error = g_last_error,
        };
        if (g_target_version[0] != '\0') {
            strncpy(status.target_version, g_target_version, sizeof(status.target_version) - 1);
        }
        g_progress_callback(&status, g_user_data);
    }
}

esp_err_t ota_service_init(const ota_service_config_t *config) {
    (void)config;  // 配置暂未使用
    
    if (g_ota_state != OTA_SERVICE_STATE_IDLE) {
        ESP_LOGW(TAG, "OTA service already initialized");
        return ESP_OK;
    }
    
    g_ota_state = OTA_SERVICE_STATE_IDLE;
    g_progress = 0;
    g_last_error = 0;
    memset(g_target_version, 0, sizeof(g_target_version));
    
    ESP_LOGI(TAG, "OTA service initialized");
    ESP_LOGI(TAG, "Current firmware: %s", ota_service_get_current_version());
    
    return ESP_OK;
}

esp_err_t ota_service_deinit(void) {
    if (g_ota_state == OTA_SERVICE_STATE_DOWNLOADING ||
        g_ota_state == OTA_SERVICE_STATE_WRITING) {
        ESP_LOGW(TAG, "Cannot deinit while OTA in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    g_ota_state = OTA_SERVICE_STATE_IDLE;
    g_progress_callback = NULL;
    g_user_data = NULL;
    
    ESP_LOGI(TAG, "OTA service deinitialized");
    
    return ESP_OK;
}

esp_err_t ota_service_register_callback(ota_service_progress_callback_t callback, void *user_data) {
    g_progress_callback = callback;
    g_user_data = user_data;
    return ESP_OK;
}

ota_service_state_t ota_service_get_state(void) {
    return g_ota_state;
}

int ota_service_get_progress(void) {
    return g_progress;
}

const char* ota_service_get_current_version(void) {
    const esp_app_desc_t *app_desc = esp_app_get_description();
    if (app_desc == NULL) {
        return "unknown";
    }
    return app_desc->version;
}

const char* ota_service_get_target_version(void) {
    return g_target_version[0] != '\0' ? g_target_version : "unknown";
}

/** @brief HTTPS OTA 事件处理 */
static esp_err_t https_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGD(TAG, "HTTPS OTA: HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD(TAG, "HTTPS OTA: HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTPS OTA: HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTPS OTA: HTTP_EVENT_ON_HEADER, key=%s, value=%s",
                     evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTPS OTA: HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTPS OTA: HTTP_EVENT_ON_FINISH");
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD(TAG, "HTTPS OTA: HTTP_EVENT_DISCONNECTED");
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t ota_service_start(const char *url) {
    if (url == NULL) {
        ESP_LOGE(TAG, "URL is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (g_ota_state != OTA_SERVICE_STATE_IDLE) {
        ESP_LOGW(TAG, "OTA already in progress");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Starting HTTPS OTA from: %s", url);
    
    g_ota_state = OTA_SERVICE_STATE_DOWNLOADING;
    g_progress = 0;
    notify_progress();
    
    // 配置 HTTP 客户端
    esp_http_client_config_t http_config = {
        .url = url,
        .event_handler = https_event_handler,
        .timeout_ms = 30000,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
    };
    
    // 配置 HTTPS OTA
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };
    
    // 开始 HTTPS OTA
    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_https_ota_begin failed: %s", esp_err_to_name(err));
        g_ota_state = OTA_SERVICE_STATE_FAILED;
        g_last_error = err;
        notify_progress();
        return err;
    }
    
    // 获取目标版本信息
    const esp_app_desc_t *app_desc = esp_https_ota_get_app_desc(ota_handle);
    if (app_desc != NULL) {
        strncpy(g_target_version, app_desc->version, sizeof(g_target_version) - 1);
        ESP_LOGI(TAG, "Target firmware version: %s", app_desc->version);
    }
    
    g_ota_state = OTA_SERVICE_STATE_VERIFYING;
    notify_progress();
    
    // 下载并验证
    while (1) {
        err = esp_https_ota_perform(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }
        
        // 获取下载进度
        int dl_size = esp_https_ota_get_image_len_read(ota_handle);
        ESP_LOGD(TAG, "Downloaded %d bytes", dl_size);
        
        // 更新进度 (估算)
        g_progress = (dl_size > 0) ? 50 : 0;
        g_ota_state = OTA_SERVICE_STATE_DOWNLOADING;
        notify_progress();
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // 检查下载结果
    if (esp_https_ota_is_complete_data_received(ota_handle) != true) {
        ESP_LOGE(TAG, "Complete data was not received");
        err = ESP_FAIL;
    } else {
        err = esp_https_ota_finish(ota_handle);
    }
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
        g_ota_state = OTA_SERVICE_STATE_FAILED;
        g_last_error = err;
        g_progress = 0;
        notify_progress();
        return err;
    }
    
    g_ota_state = OTA_SERVICE_STATE_REBOOTING;
    g_progress = 100;
    notify_progress();
    
    ESP_LOGI(TAG, "OTA update successful, rebooting...");
    
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    
    return ESP_OK;  // 不会到达
}

esp_err_t ota_service_start_http(const char *url) {
    // HTTP OTA 实际上使用相同的 esp_https_ota 组件
    // 只要 URL 是 http:// 就会使用 HTTP
    return ota_service_start(url);
}

esp_err_t ota_service_rollback(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);

    if (running == NULL || update == NULL) {
        ESP_LOGE(TAG, "Failed to get partitions");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Rolling back from %s to %s", running->label, update->label);

    esp_err_t err = esp_ota_set_boot_partition(update);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
        return err;
    }

    g_ota_state = OTA_SERVICE_STATE_IDLE;
    ESP_LOGI(TAG, "Rollback scheduled, rebooting...");

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();

    return ESP_OK;
}

esp_err_t ota_service_commit(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Committing OTA on partition: %s", running->label);

    // 分区已经是启动分区，只需要标记完成
    g_ota_state = OTA_SERVICE_STATE_IDLE;
    g_progress = 0;
    memset(g_target_version, 0, sizeof(g_target_version));

    ESP_LOGI(TAG, "OTA committed successfully");

    return ESP_OK;
}
