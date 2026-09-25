/**
 * @file ir_rx.c
 * @brief IR Receiver Driver Implementation
 */

#include "ir_rx.h"
#include "driver/gpio.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_types.h"
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ir_rx";

/** @brief IR timing constants (microseconds) */
#define IR_NEC_HDR_MARK     9000
#define IR_NEC_HDR_SPACE    4500
#define IR_NEC_BIT_MARK     560
#define IR_NEC_ONE_SPACE    1690
#define IR_NEC_ZERO_SPACE   560
#define IR_NEC_RPT_MARK     9000
#define IR_NEC_RPT_SPACE    2250

#define IR_RC5_HALF_BIT     889
#define IR_SONY_HDR_MARK    2400
#define IR_SONY_HDR_SPACE   600
#define IR_SONY_BIT_MARK    1200
#define IR_SONY_ONE_SPACE   1200
#define IR_SONY_ZERO_SPACE  600

/** @brief Tolerance for timing comparison (percentage) */
#define IR_TOLERANCE 25

/** @brief Queue size for commands */
#define IR_COMMAND_QUEUE_SIZE 16

/** @brief Internal handle structure */
struct ir_rx_handle_s {
    int8_t gpio_num;
    uint32_t idle_timeout_us;
    
    rmt_channel_handle_t rmt_channel;
    QueueHandle_t cmd_queue;
    ir_rx_callback_t callback;
    void *user_data;
    
    bool running;
    bool initialized;
};

/**
 * @brief Check if value is within tolerance of expected
 */
static bool within_tolerance(uint32_t value, uint32_t expected) {
    uint32_t diff = (value > expected) ? (value - expected) : (expected - value);
    uint32_t tolerance = (expected * IR_TOLERANCE) / 100;
    return diff <= tolerance;
}

/**
 * @brief Decode NEC protocol
 */
static int decode_nec(const rmt_symbol_word_t *symbols, size_t count, ir_command_t *cmd) {
    if (count < 68) return -1;  // NEC: 4 bytes + start + stop
    
    size_t index = 0;
    
    // Check header
    if (!within_tolerance(symbols[index++].duration0, IR_NEC_HDR_MARK)) return -1;
    if (!within_tolerance(symbols[index++].duration0, IR_NEC_HDR_SPACE)) return -1;
    
    // Decode address (8 bits) + inverse address (8 bits)
    uint8_t addr = 0, addr_inv = 0;
    for (int i = 0; i < 8; i++) {
        if (!within_tolerance(symbols[index++].duration0, IR_NEC_BIT_MARK)) return -1;
        uint32_t space = symbols[index++].duration0;
        if (within_tolerance(space, IR_NEC_ONE_SPACE)) {
            addr |= (1 << i);
        } else if (within_tolerance(space, IR_NEC_ZERO_SPACE)) {
            // Already 0
        } else {
            return -1;
        }
    }
    for (int i = 0; i < 8; i++) {
        if (!within_tolerance(symbols[index++].duration0, IR_NEC_BIT_MARK)) return -1;
        uint32_t space = symbols[index++].duration0;
        if (within_tolerance(space, IR_NEC_ONE_SPACE)) {
            addr_inv |= (1 << i);
        }
    }
    
    // Verify address
    if ((addr & addr_inv) != 0) {
        return -1;  // Address mismatch
    }
    
    // Decode command (8 bits) + inverse command (8 bits)
    uint8_t cmd_val = 0, cmd_inv = 0;
    for (int i = 0; i < 8; i++) {
        if (!within_tolerance(symbols[index++].duration0, IR_NEC_BIT_MARK)) return -1;
        uint32_t space = symbols[index++].duration0;
        if (within_tolerance(space, IR_NEC_ONE_SPACE)) {
            cmd_val |= (1 << i);
        } else if (!within_tolerance(space, IR_NEC_ZERO_SPACE)) {
            return -1;
        }
    }
    for (int i = 0; i < 8; i++) {
        if (!within_tolerance(symbols[index++].duration0, IR_NEC_BIT_MARK)) return -1;
        uint32_t space = symbols[index++].duration0;
        if (within_tolerance(space, IR_NEC_ONE_SPACE)) {
            cmd_inv |= (1 << i);
        }
    }
    
    // Verify command
    if ((cmd_val & cmd_inv) != 0) {
        return -1;  // Command mismatch
    }
    
    cmd->protocol = IR_PROTOCOL_NEC;
    cmd->address = addr;
    cmd->command = cmd_val;
    
    return 0;
}

/**
 * @brief Decode RC5 protocol
 */
static int decode_rc5(const rmt_symbol_word_t *symbols, size_t count, ir_command_t *cmd) {
    if (count < 14) return -1;
    
    // RC5 uses Manchester encoding with half-bit timing
    uint16_t data = 0;
    bool last_level = false;
    int bit_index = 0;
    
    for (size_t i = 0; i < count && bit_index < 14; i++) {
        uint32_t duration = symbols[i].duration0;
        
        if (within_tolerance(duration, IR_RC5_HALF_BIT)) {
            bool level = symbols[i].level0;
            
            if (i == 0) {
                last_level = level;
            } else {
                if (level != last_level) {
                    // Transition detected - this is a bit boundary
                    if (bit_index > 0) {
                        data <<= 1;
                        data |= (last_level ? 1 : 0);
                    }
                    last_level = level;
                }
            }
        }
    }
    
    // Parse RC5: S, S, T, A4, A3, A2, A1, A0, C5, C4, C3, C2, C1, C0
    // First two bits should be 1
    if ((data & 0x3000) != 0x3000) {
        return -1;
    }
    
    uint8_t toggle = (data >> 11) & 1;
    uint8_t address = (data >> 6) & 0x1F;
    uint8_t command = data & 0x3F;
    
    // Handle extended commands (bit 12 = 1)
    if (data & 0x1000) {
        command |= 0x40;  // Extended bit
    }
    
    cmd->protocol = IR_PROTOCOL_RC5;
    cmd->address = address;
    cmd->command = command;
    
    return 0;
}

/**
 * @brief RMT receive callback
 */
static bool IRAM_ATTR ir_rx_done_callback(rmt_channel_handle_t channel, rmt_rx_done_event_data_t *edata, void *user_data) {
    ir_rx_handle_t handle = (ir_rx_handle_t)user_data;
    
    if (handle == NULL || edata == NULL) {
        return false;
    }
    
    BaseType_t high_task_wakeup = pdFALSE;
    
    // Decode the received data
    ir_command_t cmd = {
        .protocol = IR_PROTOCOL_UNKNOWN,
        .address = 0,
        .command = 0,
        .timestamp = esp_log_timestamp()
    };
    
    // Try NEC first (most common)
    if (decode_nec(edata->received_symbols, edata->num_symbols, &cmd) != 0) {
        // Try RC5
        if (decode_rc5(edata->received_symbols, edata->num_symbols, &cmd) != 0) {
            ESP_LOGD(TAG, "Unknown IR protocol, %d symbols", edata->num_symbols);
        }
    }
    
    if (cmd.protocol != IR_PROTOCOL_UNKNOWN) {
        // Send to queue
        if (xQueueSendFromISR(handle->cmd_queue, &cmd, &high_task_wakeup) != pdTRUE) {
            ESP_LOGW(TAG, "Command queue full");
        }
        
        // Call callback if registered
        if (handle->callback != NULL) {
            handle->callback(&cmd, handle->user_data);
        }
    }
    
    return high_task_wakeup == pdTRUE;
}

ir_rx_handle_t ir_rx_create(const ir_rx_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    
    ir_rx_handle_t handle = (ir_rx_handle_t)calloc(1, sizeof(struct ir_rx_handle_s));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return NULL;
    }
    
    handle->gpio_num = config->gpio_num;
    handle->idle_timeout_us = config->idle_timeout_us > 0 ? config->idle_timeout_us : 50000;
    handle->cmd_queue = xQueueCreate(IR_COMMAND_QUEUE_SIZE, sizeof(ir_command_t));
    handle->callback = NULL;
    handle->user_data = NULL;
    handle->running = false;
    handle->initialized = false;
    
    if (handle->cmd_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create command queue");
        free(handle);
        return NULL;
    }
    
    ESP_LOGI(TAG, "IR RX created: GPIO%d, timeout=%lu us",
             config->gpio_num, handle->idle_timeout_us);
    
    return handle;
}

void ir_rx_delete(ir_rx_handle_t handle) {
    if (handle == NULL) return;
    
    ir_rx_stop(handle);
    
    if (handle->cmd_queue != NULL) {
        vQueueDelete(handle->cmd_queue);
    }
    
    if (handle->initialized && handle->rmt_channel) {
        rmt_disable(handle->rmt_channel);
        rmt_del_channel(handle->rmt_channel);
        gpio_reset_pin(handle->gpio_num);
    }
    
    free(handle);
    ESP_LOGI(TAG, "IR RX deleted");
}

int ir_rx_init(ir_rx_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    if (handle->initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return 0;
    }
    
    if (handle->gpio_num < 0) {
        ESP_LOGI(TAG, "IR RX disabled (GPIO not configured)");
        handle->initialized = true;
        return 0;
    }
    
    // Configure RMT RX channel
    rmt_rx_channel_config_t rx_chan_config = {
        .gpio_num = handle->gpio_num,
        .clk_src = RMT_CLK_SRC_APB,
        .resolution_hz = 1000000,  // 1MHz = 1us resolution
        .mem_block_symbols = 64,
        .min_rx_duration_hint = handle->idle_timeout_us,
    };
    
    esp_err_t err = rmt_new_rx_channel(&rx_chan_config, &handle->rmt_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT channel: %s", esp_err_to_name(err));
        return -2;
    }
    
    handle->initialized = true;
    ESP_LOGI(TAG, "IR RX initialized on GPIO%d", handle->gpio_num);
    
    return 0;
}

int ir_rx_start(ir_rx_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    if (!handle->initialized) {
        return -2;
    }
    
    if (handle->running) {
        ESP_LOGW(TAG, "Already running");
        return 0;
    }
    
    // Configure receiver
    rmt_receive_config_t receive_config = {
        .signal_range_min_ns = 1000,
        .signal_range_max_ns = handle->idle_timeout_us * 1000,
    };
    
    // Register callback
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = ir_rx_done_callback,
    };
    
    esp_err_t err = rmt_rx_register_event_callbacks(handle->rmt_channel, &cbs, handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register callback: %s", esp_err_to_name(err));
        return -3;
    }
    
    // Start receiving
    err = rmt_enable(handle->rmt_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable RMT: %s", esp_err_to_name(err));
        return -4;
    }
    
    // Start receive loop
    rmt_symbol_word_t symbols[64];
    while (handle->running) {
        rmt_receive(handle->rmt_channel, symbols, sizeof(symbols), &receive_config);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    handle->running = true;
    ESP_LOGI(TAG, "IR RX started");
    
    return 0;
}

int ir_rx_stop(ir_rx_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    handle->running = false;
    
    if (handle->initialized && handle->rmt_channel) {
        rmt_disable(handle->rmt_channel);
    }
    
    ESP_LOGI(TAG, "IR RX stopped");
    return 0;
}

int ir_rx_check_command(ir_rx_handle_t handle, ir_command_t *cmd) {
    if (handle == NULL || cmd == NULL) {
        return -1;
    }
    
    if (xQueueReceive(handle->cmd_queue, cmd, 0) == pdTRUE) {
        return 0;
    }
    
    return -1;  // Queue empty
}

int ir_rx_wait_command(ir_rx_handle_t handle, ir_command_t *cmd, uint32_t timeout_ms) {
    if (handle == NULL || cmd == NULL) {
        return -1;
    }
    
    if (xQueueReceive(handle->cmd_queue, cmd, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return 0;
    }
    
    return ESP_ERR_TIMEOUT;
}

int ir_rx_register_callback(ir_rx_handle_t handle, ir_rx_callback_t callback, void *user_data) {
    if (handle == NULL) {
        return -1;
    }
    
    handle->callback = callback;
    handle->user_data = user_data;
    
    return 0;
}

uint32_t ir_rx_queue_size(ir_rx_handle_t handle) {
    if (handle == NULL) {
        return 0;
    }
    
    return uxQueueMessagesWaiting(handle->cmd_queue);
}

void ir_rx_clear_queue(ir_rx_handle_t handle) {
    if (handle == NULL) {
        return;
    }
    
    ir_command_t cmd;
    while (xQueueReceive(handle->cmd_queue, &cmd, 0) == pdTRUE) {
        // Drain queue
    }
}
