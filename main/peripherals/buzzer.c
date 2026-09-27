/**
 * @file buzzer.c
 * @brief Passive buzzer (PWM via LEDC) driver
 *
 * Device type name: "buzzer"
 * Uses LEDC low-speed PWM output.
 */

#include <string.h>
#include <stdlib.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>

#include "buzzer.h"
#include "device_manager.h"

static const char *TAG = "buzzer";

typedef struct {
    gpio_num_t gpio;
    ledc_channel_t channel;
    uint32_t frequency;    /* Hz */
    uint8_t duty;         /* % */
    int auto_off_ms;
    int64_t auto_off_trigger_us;
    esp_timer_handle_t auto_off_timer;
    bool on;
} buzzer_data_t;

static void buzzer_auto_off_callback(void *arg)
{
    device_t *dev = (device_t *)arg;
    buzzer_data_t *data = dev->driver_data;
    if (!data) return;

    data->on = false;
    data->auto_off_trigger_us = 0;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, data->channel, 0);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, data->channel);
    ESP_LOGD(TAG, "Buzzer auto-off triggered");
}

static esp_err_t buzzer_init(device_t *dev, const cJSON *config)
{
    if (!cJSON_IsObject(config)) return ESP_ERR_INVALID_ARG;

    cJSON *gpio_node = cJSON_GetObjectItem(config, "gpio");
    if (!cJSON_IsNumber(gpio_node)) return ESP_ERR_INVALID_ARG;

    buzzer_data_t *data = calloc(1, sizeof(buzzer_data_t));
    if (!data) return ESP_ERR_NO_MEM;

    data->gpio = (gpio_num_t)gpio_node->valueint;
    data->frequency = 2000;
    data->duty = 50;
    data->auto_off_ms = 0;
    data->auto_off_trigger_us = 0;
    data->on = false;

    cJSON *freq = cJSON_GetObjectItem(config, "frequency");
    cJSON *duty = cJSON_GetObjectItem(config, "duty");
    cJSON *auto_off = cJSON_GetObjectItem(config, "auto_off_ms");

    if (cJSON_IsNumber(freq)) data->frequency = (uint32_t)freq->valueint;
    if (cJSON_IsNumber(duty)) data->duty = (uint8_t)duty->valueint;
    if (cJSON_IsNumber(auto_off)) data->auto_off_ms = auto_off->valueint;

    /* Clamp to valid range */
    if (data->frequency < 100)   data->frequency = 100;
    if (data->frequency > 10000) data->frequency = 10000;
    if (data->duty > 100)        data->duty = 100;

    /* GPIO config */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << data->gpio),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    /* LEDC timer — use timer 0, low-speed mode */
    ledc_timer_config_t timer_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = data->frequency,
        .duty_resolution = LEDC_TIMER_10_BIT,  /* 10-bit = 0-1023 */
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    /* LEDC channel — use channel 0 */
    data->channel = LEDC_CHANNEL_0;
    ledc_channel_config_t ch_cfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = data->channel,
        .timer_sel = LEDC_TIMER_0,
        .gpio_num = data->gpio,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));

    /* Auto-off timer (created lazily on first use) */
    data->auto_off_timer = NULL;

    dev->driver_data = data;
    ESP_LOGI(TAG, "Buzzer init: GPIO=%d freq=%luHz duty=%u%%",
             data->gpio, (unsigned long)data->frequency, data->duty);
    return ESP_OK;
}

static esp_err_t buzzer_deinit(device_t *dev)
{
    buzzer_data_t *data = dev->driver_data;
    if (!data) return ESP_OK;

    /* Stop the LEDC channel */
    ledc_stop(LEDC_LOW_SPEED_MODE, data->channel, 0);

    /* Cancel and delete auto-off timer */
    if (data->auto_off_timer) {
        esp_timer_stop(data->auto_off_timer);
        esp_timer_delete(data->auto_off_timer);
    }

    free(data);
    dev->driver_data = NULL;
    return ESP_OK;
}

static esp_err_t buzzer_read(device_t *dev, cJSON *value)
{
    buzzer_data_t *data = dev->driver_data;
    if (!data) return ESP_ERR_INVALID_STATE;

    cJSON_AddBoolToObject(value, "on", data->on);
    cJSON_AddNumberToObject(value, "frequency", data->frequency);
    cJSON_AddNumberToObject(value, "duty", data->duty);

    if (data->auto_off_trigger_us > 0) {
        int64_t remaining = (data->auto_off_trigger_us - esp_timer_get_time()) / 1000;
        cJSON_AddNumberToObject(value, "remaining_ms", remaining > 0 ? remaining : 0);
    } else {
        cJSON_AddNumberToObject(value, "remaining_ms", 0);
    }
    return ESP_OK;
}

static esp_err_t buzzer_write(device_t *dev, const cJSON *value)
{
    buzzer_data_t *data = dev->driver_data;
    if (!data) return ESP_ERR_INVALID_STATE;

    if (!cJSON_IsObject(value)) return ESP_ERR_INVALID_ARG;

    cJSON *on_node = cJSON_GetObjectItem(value, "on");
    cJSON *freq_node = cJSON_GetObjectItem(value, "frequency");
    cJSON *duty_node = cJSON_GetObjectItem(value, "duty");
    cJSON *duration_node = cJSON_GetObjectItem(value, "duration_ms");

    if (!cJSON_IsBool(on_node)) return ESP_ERR_INVALID_ARG;
    bool on = cJSON_IsTrue(on_node);

    uint32_t freq = data->frequency;
    uint8_t duty = data->duty;
    int auto_off_ms = data->auto_off_ms;

    if (cJSON_IsNumber(freq_node)) freq = (uint32_t)freq_node->valueint;
    if (cJSON_IsNumber(duty_node)) duty = (uint8_t)duty_node->valueint;
    if (cJSON_IsNumber(duration_node)) auto_off_ms = duration_node->valueint;

    /* Clamp */
    if (freq < 100)   freq = 100;
    if (freq > 10000) freq = 10000;
    if (duty > 100)   duty = 100;

    /* Cancel pending auto-off */
    if (data->auto_off_timer && data->auto_off_trigger_us > 0) {
        esp_timer_stop(data->auto_off_timer);
        data->auto_off_trigger_us = 0;
    }

    if (on) {
        /* Update frequency if changed */
        if (freq != data->frequency) {
            ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq);
            data->frequency = freq;
        }

        /* Set duty */
        uint32_t duty_val = ((uint32_t)duty * 1023) / 100;
        ledc_set_duty(LEDC_LOW_SPEED_MODE, data->channel, duty_val);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, data->channel);
        data->duty = duty;
        data->on = true;

        /* Schedule auto-off if requested */
        if (auto_off_ms > 0) {
            if (!data->auto_off_timer) {
                esp_timer_create_args_t timer_args = {
                    .callback = buzzer_auto_off_callback,
                    .arg = dev,
                    .name = "buzzer_auto_off",
                };
                ESP_ERROR_CHECK(esp_timer_create(&timer_args, &data->auto_off_timer));
            }
            data->auto_off_trigger_us = esp_timer_get_time() + (int64_t)auto_off_ms * 1000;
            esp_timer_start_once(data->auto_off_timer, (uint64_t)auto_off_ms * 1000);
        }

        ESP_LOGD(TAG, "Buzzer ON: freq=%luHz duty=%u%% auto_off=%dms",
                 (unsigned long)freq, duty, auto_off_ms);
    } else {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, data->channel, 0);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, data->channel);
        data->on = false;
        ESP_LOGD(TAG, "Buzzer OFF");
    }

    return ESP_OK;
}

static esp_err_t buzzer_tick(device_t *dev)
{
    buzzer_data_t *data = dev->driver_data;
    if (!data) return ESP_OK;
    /* Timer expiry handled by esp_timer callback */
    return ESP_OK;
}

static esp_err_t buzzer_default_config(cJSON *config)
{
    cJSON_AddNumberToObject(config, "gpio", 21);
    cJSON_AddNumberToObject(config, "frequency", 2000);
    cJSON_AddNumberToObject(config, "duty", 50);
    cJSON_AddNumberToObject(config, "auto_off_ms", 0);
    return ESP_OK;
}

static const device_type_t buzzer_driver = {
    .name = "buzzer",
    .description = "Passive buzzer (PWM)",
    .description_zh = "无源蜂鸣器",
    .save_state = true,
    .capabilities = DEVICE_CAPABILITY_WRITE | DEVICE_CAPABILITY_READ,
    .init = buzzer_init,
    .deinit = buzzer_deinit,
    .read = buzzer_read,
    .write = buzzer_write,
    .tick = buzzer_tick,
    .get_default_config = buzzer_default_config,
};

esp_err_t buzzer_driver_register(void)
{
    return device_type_register(&buzzer_driver);
}
