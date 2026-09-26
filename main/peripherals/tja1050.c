/**
 * @file tja1050.c
 * @brief TJA1050 CAN (TWAI) transceiver driver
 *
 * Uses ESP-IDF v6.x esp_twai.h driver (new on-chip TWAI driver).
 * Device type name: "can"
 */

#include <string.h>
#include <stdlib.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>

#include <esp_twai.h>
#include <esp_twai_onchip.h>

#include "tja1050.h"
#include "device_manager.h"
#include "event_bus.h"

static const char *TAG = "tja1050";

/* RX ring buffer size */
#define TJA1050_RX_BUF_SIZE  16

typedef struct {
    twai_node_handle_t node;
    gpio_num_t tx_gpio;
    gpio_num_t rx_gpio;
    uint32_t bitrate;
    int tx_queue_size;
    /* Ring buffer for received frames */
    twai_frame_t rx_buf[TJA1050_RX_BUF_SIZE];
    int rx_head;   /* next write slot */
    int rx_tail;   /* next read slot */
    int rx_count;  /* number of frames in buffer */
    bool initialized;
} tja1050_data_t;

/* Lock for rx buffer (accessed from tick in shared task) */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static portMUX_TYPE s_spinlock = portMUX_INITIALIZER_UNLOCKED;

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

    cJSON *br = cJSON_GetObjectItem(config, "bitrate");
    cJSON *txq = cJSON_GetObjectItem(config, "tx_queue_size");
    if (cJSON_IsNumber(br))  data->bitrate = (uint32_t)br->valueint;
    if (cJSON_IsNumber(txq)) data->tx_queue_size = txq->valueint;

    twai_onchip_node_config_t node_cfg = {
        .io_cfg.tx = data->tx_gpio,
        .io_cfg.rx = data->rx_gpio,
        .bit_timing.bitrate = data->bitrate,
        .bit_timing.sp_permill = 0,  /* use default ~80% */
        .tx_queue_depth = (uint32_t)data->tx_queue_size,
        .intr_priority = 0,
    };

    esp_err_t err = twai_new_node_onchip(&node_cfg, &data->node);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_new_node_onchip failed: %s", esp_err_to_name(err));
        free(data);
        return err;
    }

    err = twai_node_enable(data->node);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_node_enable failed: %s", esp_err_to_name(err));
        twai_node_delete(data->node);
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
    free(data);
    dev->driver_data = NULL;
    return ESP_OK;
}

static esp_err_t tja1050_read(device_t *dev, cJSON *value)
{
    tja1050_data_t *data = dev->driver_data;
    if (!data) return ESP_ERR_INVALID_STATE;

    portENTER_CRITICAL(&s_spinlock);
    if (data->rx_count > 0) {
        twai_frame_t *f = &data->rx_buf[data->rx_tail];
        cJSON_AddNumberToObject(value, "id", f->header.id);
        cJSON_AddBoolToObject(value, "ext", f->header.ide);
        cJSON_AddBoolToObject(value, "rtr", f->header.rtr);
        cJSON_AddNumberToObject(value, "dlc", f->header.dlc);
        cJSON_AddNumberToObject(value, "timestamp", (double)f->header.timestamp);

        cJSON *arr = cJSON_CreateArray();
        for (int i = 0; i < f->header.dlc && i < (int)f->buffer_len; i++) {
            cJSON_AddItemToArray(arr, cJSON_CreateNumber(f->buffer[i]));
        }
        cJSON_AddItemToObject(value, "data", arr);

        data->rx_tail = (data->rx_tail + 1) % TJA1050_RX_BUF_SIZE;
        data->rx_count--;
    }
    portEXIT_CRITICAL(&s_spinlock);

    return ESP_OK;
}

static esp_err_t tja1050_write(device_t *dev, const cJSON *value)
{
    tja1050_data_t *data = dev->driver_data;
    if (!data || !data->initialized) return ESP_ERR_INVALID_STATE;

    cJSON *id_n  = cJSON_GetObjectItem(value, "id");
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
        .id = (uint32_t)id_n->valueint,
        .dlc = (uint16_t)dlc,
        .ide = cJSON_IsTrue(ext_n) ? 1 : 0,
        .rtr = cJSON_IsTrue(rtr_n) ? 1 : 0,
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

    twai_frame_t rx_frame;
    rx_frame.buffer = malloc(64);
    if (!rx_frame.buffer) return ESP_OK;
    rx_frame.buffer_len = 64;

    /* Poll for received frames */
    while (twai_node_receive_from_isr(data->node, &rx_frame) == ESP_OK) {
        portENTER_CRITICAL(&s_spinlock);
        if (data->rx_count < TJA1050_RX_BUF_SIZE) {
            /* Copy frame into ring buffer (buffer pointer replaced with our own) */
            data->rx_buf[data->rx_head] = rx_frame;
            /* Copy data into buffer since the frame borrows our allocated buffer */
            memcpy(data->rx_buf[data->rx_head].buffer,
                   rx_frame.buffer, rx_frame.header.dlc);
            data->rx_buf[data->rx_head].buffer_len = rx_frame.header.dlc;
            data->rx_head = (data->rx_head + 1) % TJA1050_RX_BUF_SIZE;
            data->rx_count++;
            /* Allocate a new buffer for the next potential frame */
            rx_frame.buffer = malloc(64);
            if (!rx_frame.buffer) break;
            rx_frame.buffer_len = 64;
        }
        portEXIT_CRITICAL(&s_spinlock);

        ESP_LOGD(TAG, "RX id=0x%lX dlc=%u", (unsigned long)rx_frame.header.id,
                 (unsigned)rx_frame.header.dlc);
    }

    free(rx_frame.buffer);
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
