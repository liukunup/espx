/**
 * @file tja1050.c
 * @brief TJA1050 CAN (TWAI) transceiver driver
 *
 * Uses ESP-IDF v6.x esp_twai.h driver (new on-chip TWAI driver).
 * RX is ISR-driven via on_rx_done callback + FreeRTOS queue.
 * Device type name: "can"
 */

#include <string.h>
#include <stdlib.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <esp_twai.h>
#include <esp_twai_onchip.h>

#include "tja1050.h"
#include "device_manager.h"
#include "event_bus.h"

static const char *TAG = "tja1050";

#define TJA1050_RX_BUF_SIZE  16
#define TJA1050_FRAME_SIZE   64   /* max CAN FD data bytes, classic CAN uses ≤8 */

typedef struct {
    twai_node_handle_t node;
    gpio_num_t tx_gpio;
    gpio_num_t rx_gpio;
    uint32_t bitrate;
    int tx_queue_size;
    QueueHandle_t rx_queue;       /* QueueHandle_t for received frames */
    bool initialized;
} tja1050_data_t;

/* ISR callback: copies received frame into FreeRTOS queue */
static bool tja1050_on_rx_done(twai_node_handle_t node,
                                 const twai_rx_done_event_data_t *edata,
                                 void *user_ctx)
{
    (void)node;
    (void)edata;
    tja1050_data_t *data = user_ctx;
    if (!data || !data->rx_queue) return false;

    twai_frame_t rx_frame;
    rx_frame.buffer = malloc(TJA1050_FRAME_SIZE);
    if (!rx_frame.buffer) return false;
    rx_frame.buffer_len = TJA1050_FRAME_SIZE;

    /* Receive the actual frame (called from ISR context) */
    esp_err_t err = twai_node_receive_from_isr(node, &rx_frame);
    if (err == ESP_OK) {
        /* Try to copy header and data into a fixed-size struct for the queue */
        /* We allocate our own buffer to hold data since twai_frame_t borrows our malloc'd buffer */
        uint8_t *buf = malloc(TJA1050_FRAME_SIZE);
        if (buf) {
            size_t copy_len = rx_frame.header.dlc < TJA1050_FRAME_SIZE
                              ? rx_frame.header.dlc : TJA1050_FRAME_SIZE;
            memcpy(buf, rx_frame.buffer, copy_len);
            twai_frame_t *q_frame = malloc(sizeof(twai_frame_t));
            if (q_frame) {
                q_frame->header = rx_frame.header;
                q_frame->buffer = buf;
                q_frame->buffer_len = copy_len;
                q_frame->tx_queue_priority = 0;
                BaseType_t higher_priority_task_woken = pdFALSE;
                xQueueSendFromISR(data->rx_queue, &q_frame, &higher_priority_task_woken);
                return higher_priority_task_woken == pdTRUE;
            }
            free(buf);
        }
        free(rx_frame.buffer);
    } else {
        free(rx_frame.buffer);
    }
    return false;
}

static esp_err_t tja1050_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) return ESP_ERR_INVALID_ARG;

    cJSON *tx_gpio_n = cJSON_GetObjectItem(config, "tx_gpio");
    cJSON *rx_gpio_n = cJSON_GetObjectItem(config, "rx_gpio");
    if (!cJSON_IsNumber(tx_gpio_n) || !cJSON_IsNumber(rx_gpio_n)) {
        return ESP_ERR_INVALID_ARG;
    }

    tja1050_data_t *data = calloc(1, sizeof(tja1050_data_t));
    if (!data) return ESP_ERR_NO_MEM;

    data->tx_gpio = (gpio_num_t)tx_gpio_n->valueint;
    data->rx_gpio = (gpio_num_t)rx_gpio_n->valueint;
    data->bitrate = 500000;
    data->tx_queue_size = 5;

    cJSON *br  = cJSON_GetObjectItem(config, "bitrate");
    cJSON *txq = cJSON_GetObjectItem(config, "tx_queue_size");
    if (cJSON_IsNumber(br))  data->bitrate = (uint32_t)br->valueint;
    if (cJSON_IsNumber(txq)) data->tx_queue_size = txq->valueint;

    /* Create queue before registering callbacks */
    data->rx_queue = xQueueCreate(TJA1050_RX_BUF_SIZE, sizeof(twai_frame_t *));
    if (!data->rx_queue) {
        free(data);
        return ESP_ERR_NO_MEM;
    }

    twai_onchip_node_config_t node_cfg = {
        .io_cfg.tx = data->tx_gpio,
        .io_cfg.rx = data->rx_gpio,
        .bit_timing.bitrate = data->bitrate,
        .bit_timing.sp_permill = 0,  /* use default ~80% sampling point */
        .tx_queue_depth = (uint32_t)data->tx_queue_size,
        .intr_priority = 0,
    };

    esp_err_t err = twai_new_node_onchip(&node_cfg, &data->node);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_new_node_onchip failed: %s", esp_err_to_name(err));
        vQueueDelete(data->rx_queue);
        free(data);
        return err;
    }

    /* Register RX callback */
    twai_event_callbacks_t cbs = {
        .on_rx_done = tja1050_on_rx_done,
    };
    err = twai_node_register_event_callbacks(data->node, &cbs, data);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "twai_node_register_event_callbacks failed: %s", esp_err_to_name(err));
        /* Non-fatal — RX will be unavailable but TX still works */
    }

    err = twai_node_enable(data->node);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_node_enable failed: %s", esp_err_to_name(err));
        twai_node_delete(data->node);
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

    if (data->initialized && data->node) {
        twai_node_disable(data->node);
        twai_node_delete(data->node);
        data->initialized = false;
    }
    if (data->rx_queue) {
        /* Drain and free queued frames */
        twai_frame_t *f;
        while (xQueueReceive(data->rx_queue, &f, 0) == pdTRUE) {
            free(f->buffer);
            free(f);
        }
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

    twai_frame_t *f = NULL;
    if (xQueueReceive(data->rx_queue, &f, 0) == pdTRUE && f != NULL) {
        cJSON_AddNumberToObject(value, "id", f->header.id);
        cJSON_AddBoolToObject(value, "ext", f->header.ide);
        cJSON_AddBoolToObject(value, "rtr", f->header.rtr);
        cJSON_AddNumberToObject(value, "dlc", f->header.dlc);
        cJSON_AddNumberToObject(value, "timestamp", (double)f->header.timestamp);

        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < (int)f->buffer_len; i++) {
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(f->buffer[i]));
        }
        cJSON_AddItemToObject(value, "data", arr);

        free(f->buffer);
        free(f);
    }
    return ESP_OK;
}

static esp_err_t tja1050_write(device_t *dev, const cJSON *value)
{
    tja1050_data_t *data = dev->driver_data;
    if (!data || !data->initialized) return ESP_ERR_INVALID_STATE;

    cJSON *id_n   = cJSON_GetObjectItem(value, "id");
    cJSON *data_n = cJSON_GetObjectItem(value, "data");
    cJSON *ext_n  = cJSON_GetObjectItem(value, "ext");
    cJSON *rtr_n  = cJSON_GetObjectItem(value, "rtr");

    if (!cJSON_IsNumber(id_n)) return ESP_ERR_INVALID_ARG;

    uint8_t tx_buf[8] = {0};
    size_t dlc = 0;

    if (cJSON_IsArray(data_n)) {
        dlc = cJSON_GetArraySize(data_n);
        if (dlc > 8) return ESP_ERR_INVALID_ARG;
        for (size_t i = 0; i < dlc; i++) {
            cJSON *item = cJSON_GetArrayItem(data_n, i);
            if (cJSON_IsNumber(item)) tx_buf[i] = (uint8_t)item->valueint;
        }
    }

    twai_frame_header_t hdr = {
        .id   = (uint32_t)id_n->valueint,
        .dlc  = (uint16_t)dlc,
        .ide  = cJSON_IsTrue(ext_n) ? 1 : 0,
        .rtr  = cJSON_IsTrue(rtr_n) ? 1 : 0,
    };

    twai_frame_t frame = {
        .header = hdr,
        .buffer = tx_buf,
        .buffer_len = sizeof(tx_buf),
        .tx_queue_priority = 128,
    };

    esp_err_t err = twai_node_transmit(data->node, &frame, pdMS_TO_TICKS(100));
    if (err == ESP_OK) {
        ESP_LOGD(TAG, "TX id=0x%lX dlc=%u", (unsigned long)hdr.id, (unsigned)dlc);
    }
    return err;
}

static esp_err_t tja1050_tick(device_t *dev)
{
    tja1050_data_t *data = dev->driver_data;
    if (!data || !data->initialized) return ESP_OK;
    /* Frames arrive via ISR callback; tick just drains the queue.
     * Invalidate any stale entries by draining the queue during read(). */
    return ESP_OK;
}

static esp_err_t tja1050_default_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "tx_gpio", 6);
    cJSON_AddNumberToObject(config, "rx_gpio", 7);
    cJSON_AddNumberToObject(config, "bitrate", 500000);
    cJSON_AddNumberToObject(config, "tx_queue_size", 5);
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
