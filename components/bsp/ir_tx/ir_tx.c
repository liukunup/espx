/**
 * @file ir_tx.c
 * @brief IR Transmitter Driver Implementation
 */

#include "ir_tx.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_types.h"
#include "esp_log.h"
#include "esp_err.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ir_tx";

/** @brief IR timing constants (microseconds) */
#define IR_NEC_HDR_MARK    9000
#define IR_NEC_HDR_SPACE   4500
#define IR_NEC_BIT_MARK    560
#define IR_NEC_ONE_SPACE   1690
#define IR_NEC_ZERO_SPACE  560
#define IR_NEC_RPT_MARK    9000
#define IR_NEC_RPT_SPACE   2250

#define IR_RC5_HALF_BIT    889
#define IR_SONY_HDR_MARK   2400
#define IR_SONY_HDR_SPACE  600
#define IR_SONY_BIT_MARK   1200
#define IR_SONY_ZERO_SPACE 600
#define IR_SONY_ONE_SPACE  1200

/** @brief Maximum timing entries */
#define IR_MAX_TIMINGS 128

/** @brief Internal handle structure */
struct ir_tx_handle_s {
    int8_t gpio_num;
    bool carrier_enable;
    uint32_t carrier_freq;
    
    rmt_channel_handle_t rmt_channel;
    rmt_encoder_handle_t copy_encoder;
    rmt_encoder_handle_t bits_encoder;
    rmt_transmit_config_t tx_config;
    
    bool initialized;
};

/**
 * @brief Encode a bit into timing data
 */
static void encode_bit(uint32_t *timings, uint16_t *index, uint32_t mark, uint32_t space) {
    timings[(*index)++] = mark;
    timings[(*index)++] = space;
}

/**
 * @brief Encode NEC protocol
 */
static uint16_t encode_nec(uint32_t *timings, uint16_t address, uint8_t command, bool extended) {
    uint16_t index = 0;
    
    // Header mark and space
    timings[index++] = IR_NEC_HDR_MARK;
    timings[index++] = IR_NEC_HDR_SPACE;
    
    // Address (LSB first)
    uint16_t addr = address;
    uint16_t addr_inv = ~address & (extended ? 0xFFFF : 0xFF);
    
    for (int i = 0; i < (extended ? 16 : 8); i++) {
        uint32_t mark = IR_NEC_BIT_MARK;
        uint32_t space = (addr & 1) ? IR_NEC_ONE_SPACE : IR_NEC_ZERO_SPACE;
        encode_bit(timings, &index, mark, space);
        addr >>= 1;
    }
    
    for (int i = 0; i < (extended ? 16 : 8); i++) {
        uint32_t mark = IR_NEC_BIT_MARK;
        uint32_t space = (addr_inv & 1) ? IR_NEC_ONE_SPACE : IR_NEC_ZERO_SPACE;
        encode_bit(timings, &index, mark, space);
        addr_inv >>= 1;
    }
    
    // Command (LSB first)
    uint8_t cmd = command;
    uint8_t cmd_inv = ~command & 0xFF;
    
    for (int i = 0; i < 8; i++) {
        uint32_t mark = IR_NEC_BIT_MARK;
        uint32_t space = (cmd & 1) ? IR_NEC_ONE_SPACE : IR_NEC_ZERO_SPACE;
        encode_bit(timings, &index, mark, space);
        cmd >>= 1;
    }
    
    for (int i = 0; i < 8; i++) {
        uint32_t mark = IR_NEC_BIT_MARK;
        uint32_t space = (cmd_inv & 1) ? IR_NEC_ONE_SPACE : IR_NEC_ZERO_SPACE;
        encode_bit(timings, &index, mark, space);
        cmd_inv >>= 1;
    }
    
    // Stop bit
    timings[index++] = IR_NEC_BIT_MARK;
    
    return index;
}

/**
 * @brief Encode RC5 protocol
 */
static uint16_t encode_rc5(uint32_t *timings, uint8_t address, uint8_t command) {
    uint16_t index = 0;
    uint16_t data = 0;
    
    // RC5: 1 start bit (1), toggle bit (0), 5 address bits, 6 command bits
    data = (1 << 12) | (0 << 11) | ((address & 0x1F) << 6) | (command & 0x3F);
    
    bool last_level = false;  // Start with low
    
    for (int i = 0; i < 14; i++) {
        bool bit = (data >> (13 - i)) & 1;
        
        if (bit != last_level) {
            // Transition
            timings[index++] = IR_RC5_HALF_BIT;
            last_level = bit;
        } else {
            // Same level, add another half bit
            timings[index++] = IR_RC5_HALF_BIT;
        }
    }
    
    return index;
}

/**
 * @brief Encode Sony SIRC protocol
 */
static uint16_t encode_sony(uint32_t *timings, uint8_t address, uint8_t command, uint8_t extended, bool has_extended) {
    uint16_t index = 0;
    
    // Header
    timings[index++] = IR_SONY_HDR_MARK;
    timings[index++] = IR_SONY_HDR_SPACE;
    
    uint16_t data = (has_extended ? 0x8000 : 0x0000) | ((address & 0x7F) << 7) | (command & 0x7F);
    if (has_extended) {
        data |= (extended & 0xFF) << 7;
    }
    
    int num_bits = has_extended ? 20 : (address > 0x7F ? 15 : 12);
    
    for (int i = 0; i < num_bits; i++) {
        uint32_t mark = IR_SONY_BIT_MARK;
        uint32_t space = (data & 1) ? IR_SONY_ONE_SPACE : IR_SONY_ZERO_SPACE;
        encode_bit(timings, &index, mark, space);
        data >>= 1;
    }
    
    return index;
}

/**
 * @brief Convert timings to RMT symbols
 */
static void timings_to_rmt(const uint32_t *timings, uint16_t count, rmt_symbol_word_t *symbols, uint32_t idle_level) {
    for (uint16_t i = 0; i < count && i < IR_MAX_TIMINGS; i++) {
        uint32_t duration = timings[i];
        
        // Clamp duration to valid RMT range (0-32767)
        if (duration > 32767) duration = 32767;
        
        symbols[i].duration0 = duration;
        symbols[i].level0 = (i % 2 == 0) ? 1 : 0;
        symbols[i].duration1 = 0;
        symbols[i].level1 = 0;
    }
}

ir_tx_handle_t ir_tx_create(const ir_tx_config_t *config) {
    if (config == NULL) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    
    ir_tx_handle_t handle = (ir_tx_handle_t)calloc(1, sizeof(struct ir_tx_handle_s));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return NULL;
    }
    
    handle->gpio_num = config->gpio_num;
    handle->carrier_enable = config->carrier_enable;
    handle->carrier_freq = config->carrier_freq > 0 ? config->carrier_freq : 38000;
    handle->initialized = false;
    
    ESP_LOGI(TAG, "IR TX created: GPIO%d, carrier=%s, freq=%lu Hz",
             config->gpio_num,
             config->carrier_enable ? "yes" : "no",
             handle->carrier_freq);
    
    return handle;
}

void ir_tx_delete(ir_tx_handle_t handle) {
    if (handle == NULL) return;
    
    if (handle->initialized) {
        if (handle->rmt_channel) {
            rmt_disable(handle->rmt_channel);
            rmt_del_channel(handle->rmt_channel);
        }
        if (handle->copy_encoder) {
            rmt_del_encoder(handle->copy_encoder);
        }
        if (handle->bits_encoder) {
            rmt_del_encoder(handle->bits_encoder);
        }
        gpio_reset_pin(handle->gpio_num);
    }
    
    free(handle);
    ESP_LOGI(TAG, "IR TX deleted");
}

int ir_tx_init(ir_tx_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    if (handle->initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return 0;
    }
    
    if (handle->gpio_num < 0) {
        ESP_LOGI(TAG, "IR TX disabled (GPIO not configured)");
        handle->initialized = true;
        return 0;
    }
    
    // Configure RMT TX channel
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = handle->gpio_num,
        .clk_src = RMT_CLK_SRC_APB,
        .resolution_hz = 1000000,  // 1MHz = 1us resolution
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    
    esp_err_t err = rmt_new_tx_channel(&tx_chan_config, &handle->rmt_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create RMT channel: %s", esp_err_to_name(err));
        return -2;
    }
    
    // Create copy encoder (for raw data)
    rmt_copy_encoder_config_t copy_config = {};
    err = rmt_new_copy_encoder(&copy_config, &handle->copy_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create copy encoder: %s", esp_err_to_name(err));
        return -3;
    }
    
    handle->tx_config.loop_count = 0;
    
    rmt_enable(handle->rmt_channel);
    
    handle->initialized = true;
    ESP_LOGI(TAG, "IR TX initialized on GPIO%d", handle->gpio_num);
    
    return 0;
}

int ir_tx_send_raw(ir_tx_handle_t handle, const uint32_t *timings, uint16_t count) {
    if (handle == NULL || timings == NULL || count == 0) {
        return -1;
    }
    
    if (!handle->initialized) {
        return -2;
    }
    
    // Limit count
    uint16_t actual_count = (count > IR_MAX_TIMINGS) ? IR_MAX_TIMINGS : count;
    
    // Convert to RMT symbols
    rmt_symbol_word_t symbols[IR_MAX_TIMINGS];
    memset(symbols, 0, sizeof(symbols));
    timings_to_rmt(timings, actual_count, symbols, 0);
    
    esp_err_t err = rmt_transmit(handle->rmt_channel, 
                                  handle->copy_encoder,
                                  symbols,
                                  actual_count * sizeof(rmt_symbol_word_t),
                                  &handle->tx_config);
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RMT transmit failed: %s", esp_err_to_name(err));
        return -3;
    }
    
    return 0;
}

int ir_tx_send_nec(ir_tx_handle_t handle, uint16_t address, uint8_t command) {
    if (handle == NULL) {
        return -1;
    }
    
    uint32_t timings[IR_MAX_TIMINGS];
    uint16_t count = encode_nec(timings, address, command, false);
    
    return ir_tx_send_raw(handle, timings, count);
}

int ir_tx_send_nec_extended(ir_tx_handle_t handle, uint16_t address, uint8_t command) {
    if (handle == NULL) {
        return -1;
    }
    
    uint32_t timings[IR_MAX_TIMINGS];
    uint16_t count = encode_nec(timings, address, command, true);
    
    return ir_tx_send_raw(handle, timings, count);
}

int ir_tx_send_rc5(ir_tx_handle_t handle, uint8_t address, uint8_t command) {
    if (handle == NULL) {
        return -1;
    }
    
    uint32_t timings[IR_MAX_TIMINGS];
    uint16_t count = encode_rc5(timings, address, command);
    
    return ir_tx_send_raw(handle, timings, count);
}

int ir_tx_send_sony(ir_tx_handle_t handle, uint8_t address, uint8_t command, uint8_t extended) {
    if (handle == NULL) {
        return -1;
    }
    
    uint32_t timings[IR_MAX_TIMINGS];
    uint16_t count = encode_sony(timings, address, command, extended, false);
    
    return ir_tx_send_raw(handle, timings, count);
}

int ir_tx_send_nec_repeat(ir_tx_handle_t handle) {
    if (handle == NULL) {
        return -1;
    }
    
    uint32_t timings[] = {
        IR_NEC_RPT_MARK, IR_NEC_RPT_SPACE,
        IR_NEC_BIT_MARK, IR_NEC_BIT_MARK  // Stop bit
    };
    
    return ir_tx_send_raw(handle, timings, sizeof(timings) / sizeof(timings[0]));
}

int ir_tx_set_carrier(ir_tx_handle_t handle, bool enable) {
    if (handle == NULL) {
        return -1;
    }
    
    handle->carrier_enable = enable;
    return 0;
}
