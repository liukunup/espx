/**
 * @file ota_service.h
 * @brief ESP OTA Service 封装层
 * 
 * 提供 OTA 更新接口，支持:
 * - HTTPS OTA
 * - HTTP OTA
 * - 进度回调
 * - 版本检查
 */

#pragma once

#include "esp_err.h"
#include "stdint.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA 服务状态
 */
typedef enum {
    OTA_SERVICE_STATE_IDLE = 0,
    OTA_SERVICE_STATE_DOWNLOADING,
    OTA_SERVICE_STATE_VERIFYING,
    OTA_SERVICE_STATE_WRITING,
    OTA_SERVICE_STATE_REBOOTING,
    OTA_SERVICE_STATE_FAILED
} ota_service_state_t;

/**
 * @brief OTA 服务状态结构
 */
typedef struct {
    int progress_percent;          /**< 进度百分比 */
    ota_service_state_t state;     /**< 当前状态 */
    int last_error;                /**< 上次错误码 */
    char target_version[32];       /**< 目标版本 */
} ota_service_status_t;

/**
 * @brief OTA 进度回调
 */
typedef void (*ota_service_progress_callback_t)(const ota_service_status_t *status, void *user_data);

/**
 * @brief OTA 服务配置
 */
typedef struct {
    uint32_t timeout_ms;          /**< 下载超时 */
    bool enable_rollback;          /**< 启用回滚 */
    const char *ca_cert_pem;      /**< CA 证书 (用于 HTTPS) */
} ota_service_config_t;

/**
 * @brief 初始化 OTA 服务
 * @param config 配置 (NULL 使用默认)
 * @return ESP_OK 成功
 */
esp_err_t ota_service_init(const ota_service_config_t *config);

/**
 * @brief 反初始化 OTA 服务
 * @return ESP_OK 成功
 */
esp_err_t ota_service_deinit(void);

/**
 * @brief 开始 OTA 更新 (HTTPS)
 * @param url 固件 URL
 * @return ESP_OK 成功
 */
esp_err_t ota_service_start(const char *url);

/**
 * @brief 开始 OTA 更新 (HTTP)
 * @param url 固件 URL
 * @return ESP_OK 成功
 */
esp_err_t ota_service_start_http(const char *url);

/**
 * @brief 注册进度回调
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return ESP_OK 成功
 */
esp_err_t ota_service_register_callback(ota_service_progress_callback_t callback, void *user_data);

/**
 * @brief 获取当前状态
 * @return OTA 服务状态
 */
ota_service_state_t ota_service_get_state(void);

/**
 * @brief 获取进度
 * @return 进度百分比 (0-100)
 */
int ota_service_get_progress(void);

/**
 * @brief 获取当前固件版本
 * @return 版本字符串
 */
const char* ota_service_get_current_version(void);

/**
 * @brief 获取目标固件版本
 * @return 版本字符串
 */
const char* ota_service_get_target_version(void);

/**
 * @brief 回滚到上一版本
 * @return ESP_OK 成功
 */
esp_err_t ota_service_rollback(void);

/**
 * @brief 确认更新
 * @return ESP_OK 成功
 */
esp_err_t ota_service_commit(void);

#ifdef __cplusplus
}
#endif
