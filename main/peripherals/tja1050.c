/**
 * @file tja1050.c
 * @brief TJA1050 CAN (TWAI) transceiver driver
 *
 * Uses ESP32-S3 native TWAI (CAN 2.0) controller.
 * Device type name: "can"
 */

#include <string.h>
#include <stdlib.h>
#include <driver/gpio.h>
#include <driver/twai.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>

#include "tja1050.h"
#include "device_manager.h"
#include "event_bus.h"

static const char *TAG = "tja1050";

typedef struct {
    gpio_num_t tx_gpio;
    gpio_num_t rx_gpio;
    uint32_t bitrate;
    int tx_queue_size;
    int rx_queue_size;
    QueueHandle_t rx_queue;
    bool initialized;
} tja1050_data_t;

static esp_err_t tja1050_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *tx_gpio_node = cJSON_GetObjectItem(config, "tx_gpio");
    cJSON *rx_gpio_node = cJSON_GetObjectItem(config, "rx_gpio");

    if (!cJSON_IsNumber(tx_gpio_node) || !cJSON_IsNumber(rx_gpio_node)) {
        return ESP_ERR_INVALID_ARG;
    }

    tja1050_data_t *data = calloc(1, sizeof(tja1050_data_t));
    if (!data) {
        return ESP_ERR_NO_MEM;
    }

    data->tx_gpio = (gpio_num_t)tx_gpio_node->valueint;
    data->rx_gpio = (gpio_num_t)rx_gpio_node->valueint;
    data->bitrate = 500000;
    data->tx_queue_size = 5;
    data->rx_queue_size = 10;

    cJSON *bitrate_node = cJSON_GetObjectItem(config, "bitrate");
    cJSON *txq = cJSON_GetObjectItem(config, "tx_queue_size");
    cJSON *rxq = cJSON_GetObjectItem(config, "rx_queue_size");

    if (cJSON_IsNumber(bitrate_node)) data->bitrate = bitrate_node->valueint;
    if (cJSON_IsNumber(txq)) data->tx_queue_size = txq->valueint;
    if (cJSON_IsNumber(rxq)) data->rx_queue_size = rxq->valueint;

    data->rx_queue = xQueueCreate(data->rx_queue_size, sizeof(twai_message_t));
    if (!data->rx_queue) {
        free(data);
        return ESP_ERR_NO_MEM;
    }

    /* Select timing config based on bitrate */
    twai_timing_config_t t_config;
    switch (data->bitrate) {
    case 125000:
        t_config = TWAI_TIMING_CONFIG_125KBITS();
        break;
    case 250000:
        t_config = TWAI_TIMING_CONFIG_250KBITS();
        break;
    case 1000000:
        t_config = TWAI_TIMING_CONFIG_1MBITS();
        break;
    case 500000:
    default:
        t_config = TWAI_TIMING_CONFIG_500KBITS();
        break;
    }

    twai_general_config_t g_config = {
        .mode = TWAI_MODE_NORMAL,
        .tx_io = data->tx_gpio,
        .rx_io = data->rx_gpio,
        .clkout_io = GPIO_NUM_NC,
        .bus_off_io = GPIO_NUM_NC,
        .tx_queue_len = data->tx_queue_size,
        .rx_queue_len = data->rx_queue_size,
        .alerts_enabled = TWAI_ALERT_NONE,
        .clkout_divider = 0,
    };

    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g_config, &t_config, &f_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI driver install failed: %s", esp_err_to_name(err));
        vQueueDelete(data->rx_queue);
        free(data);
        return err;
    }

    err = twai_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "TWAI start failed: %s", esp_err_to_name(err));
        twai_driver_uninstall();
        vQueueDelete(data->rx_queue);
        free(data);
        return err;
    }

    data->initialized = true;
    dev->driver_data = data;
    ESP_LOGI(TAG, "TJA1050 initialized: TX=GPIO%d RX=GPIO%d %lu bps",
             data->tx_gpio, data->rx_gpio, (unsigned long)data->bitrate);
    return ESP_OK;
}

static esp_err_t tja1050_deinit(device_t *dev)
{
    tja1050_data_t *data = dev->driver_data;
    if (!data) return ESP_OK;

    if (data->initialized) {
        twai_stop();
        twai_driver_uninstall();
        data->initialized = false;
    }
    if (data->rx_queue) {
        vQueueDelete(data->rx_queue);
    }
    free(data);
    dev->driver_data = NULL;
    return ESP_OK;
}

static esp_err_t tja1050_read(device_t *dev, cJSON *value)
{
    tja1050_data_t *data = dev->driver_data;
    if (!data) return ESP_ERR_INVALID_STATE;

    twai_message_t msg;
    if (xQueueReceive(data->rx_queue, &msg, 0) == pdTRUE) {
        cJSON_AddNumberToObject(value, "id", msg.identifier);
        cJSON_AddBoolToObject(value, "ext", msg.extd);
        cJSON_AddBoolToObject(value, "rtr", msg.rtr);
        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < msg.data_length_code; i++) {
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(msg.data[i]));
        }
        cJSON_AddItemToObject(value, "data", arr);
    }
    return ESP_OK;
}

static esp_err_t tja1050_write(device_t *dev, const cJSON *value)
{
    tja1050_data_t *data = dev->driver_data;
    if (!data || !data->initialized) return ESP_ERR_INVALID_STATE;

    cJSON *id_node = cJSON_GetObjectItem(value, "id");
    cJSON *data_node = cJSON_GetObjectItem(value, "data");
    cJSON *ext_node = cJSON_GetObjectItem(value, "ext");
    cJSON *rtr_node = cJSON_GetObjectItem(value, "rtr");

    if (!cJSON_IsNumber(id_node)) return ESP_ERR_INVALID_ARG;

    twai_message_t msg = {
        .identifier = (uint32_t)id_node->valueint,
        .extd = cJSON_IsTrue(ext_node),
        .rtr = cJSON_IsTrue(rtr_node),
        .data_length_code = 0,
    };

    if (cJSON_IsArray(data_node)) {
        int dlc = cJSON_GetArraySize(data_node);
        if (dlc > 8) return ESP_ERR_INVALID_ARG;
        for (int i = 0; i < dlc; i++) {
            cJSON *item = cJSON_GetArrayItem(data_node, i);
            if (cJSON_IsNumber(item)) {
                msg.data[i] = (uint8_t)item->valueint;
            }
        }
        msg.data_length_code = (uint8_t)dlc;
    }

    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(100));
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "TX id=0x%lX dlc=%u", (unsigned long)msg.identifier, msg.data_length_code);
    }
    return err;
}

static esp_err_t tja1050_tick(device_t *dev)
{
    tja1050_data_t *data = dev->driver_data;
    if (!data || !data->initialized) return ESP_OK;

    twai_message_t msg;
    esp_err_t err;
    while ((err = twai_receive(&msg, 0)) == ESP_OK) {
        if (uxQueueSpacesAvailable(data->rx_queue) > 0) {
            xQueueSend(data->rx_queue, &msg, 0);
            ESP_LOGD(TAG, "RX id=0x%lX dlc=%u", (unsigned long)msg.identifier, msg.data_length_code);
        }
    }
    return ESP_OK;
}

static esp_err_t tja1050_default_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "tx_gpio", 6);
    cJSON_AddNumberToObject(config, "rx_gpio", 7);
    cJSON_AddNumberToObject(config, "bitrate", 500000);
    cJSON_AddNumberToObject(config, "tx_queue_size", 5);
    cJSON_AddNumberToObject(config, "rx_queue_size", 10);
    return ESP_OK;
}

static esp_err_t tja1050_validate_config(const cJSON *config)
{
    cJSON *tx = cJSON_GetObjectItem(config, "tx_gpio");
    cJSON *rx = cJSON_GetObjectItem(config, "rx_gpio");
    cJSON *br = cJSON_GetObjectItem(config, "bitrate");

    if (!cJSON_IsNumber(tx) || !cJSON_IsNumber(rx)) return ESP_ERR_INVALID_ARG;
    if (tx->valueint < 0 || tx->valueint > 48 || rx->valueint < 0 || rx->valueint > 48) {
        return ESP_ERR_INVALID_ARG;
    }
    if (cJSON_IsNumber(br)) {
        int b = br->valueint;
        if (b != 125000 && b != 250000 && b != 500000 && b != 1000000) {
            return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_OK;
}

static const device_type_t tja1050_driver = {
    .name = "can",
    .description = "TJA1050 CAN (TWAI) transceiver",
    .capabilities = DEVICE_CAPABILITY_READ | DEVICE_CAPABILITY_WRITE | DEVICE_CAPABILITY_NOTIFY,
    .init = tja1050_init,
    .deinit = tja1050_deinit,
    .read = tja1050_read,
    .write = tja1050_write,
    .tick = tja1050_tick,
    .get_default_config = tja1050_default_config,
    .validate_config = tja1050_validate_config,
};

esp_err_t tja1050_driver_register(void)
{
    return device_type_register(&tja1050_driver);
}
