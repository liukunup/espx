/**
 * @file wifi_service.h
 * @brief ESP Wi-Fi Service 封装层
 * 
 * 提供统一的 Wi-Fi 连接和配网接口，支持:
 * - STA 模式连接
 * - AP 配网模式 (SoftAP)
 * - Wi-Fi 事件通知
 */

#pragma once

#include "esp_err.h"
#include "esp_netif_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wi-Fi 服务状态
 */
typedef enum {
    WIFI_SERVICE_STATE_IDLE = 0,
    WIFI_SERVICE_STATE_CONNECTING,
    WIFI_SERVICE_STATE_CONNECTED,
    WIFI_SERVICE_STATE_DISCONNECTED,
    WIFI_SERVICE_STATE_PROVISIONING,
    WIFI_SERVICE_STATE_FAILED
} wifi_service_state_t;

/**
 * @brief Wi-Fi 服务事件类型
 */
typedef enum {
    WIFI_SERVICE_EVENT_CONNECTED,
    WIFI_SERVICE_EVENT_DISCONNECTED,
    WIFI_SERVICE_EVENT_GOT_IP,
    WIFI_SERVICE_EVENT_PROVISIONING_START,
    WIFI_SERVICE_EVENT_PROVISIONING_SUCCESS,
    WIFI_SERVICE_EVENT_PROVISIONING_FAILED,
} wifi_service_event_type_t;

/**
 * @brief Wi-Fi 服务事件结构
 */
typedef struct {
    wifi_service_event_type_t type;
    void *user_data;
} wifi_service_event_t;

/**
 * @brief Wi-Fi 服务事件回调
 */
typedef void (*wifi_service_event_callback_t)(const wifi_service_event_t *event, void *user_data);

/**
 * @brief Wi-Fi 服务配置
 */
typedef struct {
    const char *ssid;        /**< Wi-Fi SSID (STA 模式) */
    const char *password;    /**< Wi-Fi 密码 */
    uint32_t connect_timeout_ms;  /**< 连接超时 */
} wifi_service_config_t;

/**
 * @brief Wi-Fi 服务句柄
 */
typedef struct wifi_service_s *wifi_service_handle_t;

/**
 * @brief 初始化 Wi-Fi 服务
 * @return ESP_OK 成功
 */
esp_err_t wifi_service_init(void);

/**
 * @brief 反初始化 Wi-Fi 服务
 * @return ESP_OK 成功
 */
esp_err_t wifi_service_deinit(void);

/**
 * @brief 连接到 Wi-Fi 网络 (STA 模式)
 * @param ssid SSID
 * @param password 密码
 * @return ESP_OK 成功
 */
esp_err_t wifi_service_connect(const char *ssid, const char *password);

/**
 * @brief 断开 Wi-Fi 连接
 * @return ESP_OK 成功
 */
esp_err_t wifi_service_disconnect(void);

/**
 * @brief 开始配网模式 (SoftAP)
 * @param pop Proof of Possession (可选, NULL 表示无)
 * @return ESP_OK 成功
 */
esp_err_t wifi_service_start_provisioning(const char *pop);

/**
 * @brief 停止配网模式
 * @return ESP_OK 成功
 */
esp_err_t wifi_service_stop_provisioning(void);

/**
 * @brief 注册事件回调
 * @param callback 回调函数
 * @param user_data 用户数据
 * @return ESP_OK 成功
 */
esp_err_t wifi_service_register_callback(wifi_service_event_callback_t callback, void *user_data);

/**
 * @brief 获取当前状态
 * @return Wi-Fi 服务状态
 */
wifi_service_state_t wifi_service_get_state(void);

/**
 * @brief 获取 netif 句柄
 * @return esp_netif_t* 或 NULL
 */
esp_netif_t *wifi_service_get_netif(void);

/**
 * @brief 检查是否已配网
 * @return true 已配网
 */
bool wifi_service_is_provisioned(void);

#ifdef __cplusplus
}
#endif
