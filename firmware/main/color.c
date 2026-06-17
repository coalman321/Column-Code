#include "color.h"

#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "cJSON.h"
#include "device_mac.h"
#include "device_mqtt.h"

static const char *TAG = "color";

/* ── RMT / SK6812 config ─────────────────────────────────────────────────── */
#define RMT_RESOLUTION_HZ  10000000  /* 10 MHz → 100 ns per tick */

#define SK6812_T0H  3
#define SK6812_T0L  9
#define SK6812_T1H  7
#define SK6812_T1L  4
#define SK6812_RESET_US  100

static rmt_channel_handle_t s_led_chan    = NULL;
static rmt_encoder_handle_t s_led_encoder = NULL;
static bool s_ready = false;

/* ── Flicker state ──────────────────────────────────────────────────────── */
static uint8_t s_color_r = 0, s_color_g = 0, s_color_b = 0, s_color_w = 0;
static bool s_flicker_enabled = false;
static TaskHandle_t s_flicker_task = NULL;

/* ── RandomWave structure and functions ─────────────────────────────────── */
typedef struct {
    int16_t min_value;
    int16_t max_value;
    uint32_t min_duration_ms;
    uint32_t max_duration_ms;

    uint32_t start_time;
    uint32_t current_duration;
    int16_t start_value;
    int16_t end_value;
} RandomWave;

static void random_wave_init(RandomWave *wave, int16_t min_val, int16_t max_val,
                             uint32_t min_dur, uint32_t max_dur, uint32_t now) {
    wave->min_value = min_val;
    wave->max_value = max_val;
    wave->min_duration_ms = min_dur;
    wave->max_duration_ms = max_dur;
    wave->start_time = now;
    wave->end_value = min_val + (esp_random() % (max_val - min_val + 1));
    wave->current_duration = min_dur + (esp_random() % (max_dur - min_dur + 1));
    wave->start_value = wave->end_value;
}

static void random_wave_next(RandomWave *wave, uint32_t now) {
    wave->start_value = wave->end_value;
    wave->start_time = now;
    wave->current_duration = wave->min_duration_ms +
                            (esp_random() % (wave->max_duration_ms - wave->min_duration_ms + 1));
    wave->end_value = wave->min_value + (esp_random() % (wave->max_value - wave->min_value + 1));
}

static int16_t random_wave_value_at(RandomWave *wave, uint32_t now) {
    uint32_t time_delta = now - wave->start_time;

    if (time_delta >= wave->current_duration) {
        random_wave_next(wave, now);
        time_delta = 0;
    }

    /* Linear interpolation: start_value + (end_value - start_value) * (time_delta / duration) */
    int32_t progress = (int32_t)time_delta * 1000 / (int32_t)wave->current_duration;
    int32_t delta = ((int32_t)wave->end_value - wave->start_value) * progress / 1000;
    return wave->start_value + (int16_t)delta;
}

static RandomWave s_slow_wave;   /* Base brightness drift (3-6 second transitions) */
static RandomWave s_fast_wave;   /* Flicker deltas (20-120ms transitions) */

/* ── MQTT color message handler ──────────────────────────────────────────── */

static void on_color_message(const char *topic, int topic_len,
                             const char *data,  int data_len)
{
    char buf[128];
    int copy = data_len < (int)sizeof(buf) - 1 ? data_len : (int)sizeof(buf) - 1;
    memcpy(buf, data, copy);
    buf[copy] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed: %.64s", buf);
        return;
    }

    cJSON *jr = cJSON_GetObjectItem(root, "r");
    cJSON *jg = cJSON_GetObjectItem(root, "g");
    cJSON *jb = cJSON_GetObjectItem(root, "b");
    cJSON *jw = cJSON_GetObjectItem(root, "w");
    cJSON *jflicker = cJSON_GetObjectItem(root, "flicker");

    if (!cJSON_IsNumber(jr) || !cJSON_IsNumber(jg) ||
        !cJSON_IsNumber(jb) || !cJSON_IsNumber(jw)) {
        ESP_LOGE(TAG, "unexpected JSON fields");
        cJSON_Delete(root);
        return;
    }

    uint8_t r = (uint8_t)jr->valueint, g = (uint8_t)jg->valueint,
            b = (uint8_t)jb->valueint, w = (uint8_t)jw->valueint;
    bool flicker = jflicker && cJSON_IsTrue(jflicker);
    cJSON_Delete(root);
    color_apply(r, g, b, w, flicker);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

esp_err_t color_init(void)
{
    rmt_tx_channel_config_t tx_cfg = {
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .gpio_num          = CONFIG_COLOR_GPIO_DATA,
        .mem_block_symbols = 64,
        .resolution_hz     = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    esp_err_t err = rmt_new_tx_channel(&tx_cfg, &s_led_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_tx_channel: %s", esp_err_to_name(err));
        return err;
    }

    rmt_bytes_encoder_config_t enc_cfg = {
        .bit0 = { .duration0 = SK6812_T0H, .level0 = 1,
                  .duration1 = SK6812_T0L, .level1 = 0 },
        .bit1 = { .duration0 = SK6812_T1H, .level0 = 1,
                  .duration1 = SK6812_T1L, .level1 = 0 },
        .flags.msb_first = 1,
    };
    err = rmt_new_bytes_encoder(&enc_cfg, &s_led_encoder);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_new_bytes_encoder: %s", esp_err_to_name(err));
        return err;
    }

    err = rmt_enable(s_led_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_enable: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;

    /* Subscribe to retained color updates from the backend. */
    static char s_color_topic[64];
    char mac[DEVICE_MAC_STR_LEN];
    device_mac_get_str(mac);
    snprintf(s_color_topic, sizeof(s_color_topic), "devices/%s/color", mac);
    device_mqtt_subscribe(s_color_topic, 1, on_color_message);

    ESP_LOGI(TAG, "SK6812 ready — %d LED(s) on GPIO %d",
             CONFIG_COLOR_NUM_LEDS, CONFIG_COLOR_GPIO_DATA);
    return ESP_OK;
}

/* Flicker task: layered RandomWave algorithm for realistic candlelight. */
static void flicker_task(void *arg)
{
    uint32_t start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

    /* Initialize waves on first run */
    random_wave_init(&s_slow_wave, 90, 130, 3000, 6000, start_time);      /* Slow: 50-150 over 3-6s */
    random_wave_init(&s_fast_wave, -20, 20, 120, 220, start_time);           /* Fast: ±40 over 20-120ms */

    while (1) {
        if (!s_flicker_enabled) {
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        /* Get slow drift (base brightness) and fast flicker (variation) */
        int16_t slow_brightness = random_wave_value_at(&s_slow_wave, now);
        int16_t fast_flicker = random_wave_value_at(&s_fast_wave, now);

        /* Combine layers: slow (50-150) + fast (-40 to +40) = range 10-190, mapped to 0.1-1.9 */
        int16_t combined = slow_brightness + fast_flicker;
        if (combined < 10) combined = 10;
        if (combined > 190) combined = 190;

        /* Convert to brightness factor: 0.1 to 1.9, clamped to 0.7-1.0 range for visibility */
        float brightness = (combined / 100.0f);
        if (brightness < 0.7f) brightness = 0.7f;
        if (brightness > 1.0f) brightness = 1.0f;

        uint8_t r_modulated = (uint8_t)(s_color_r * brightness);
        uint8_t g_modulated = (uint8_t)(s_color_g * brightness);
        uint8_t b_modulated = (uint8_t)(s_color_b * brightness);
        uint8_t w_modulated = (uint8_t)(s_color_w * brightness);

        /* SK6812 wire order is G R B W. */
        uint8_t pixels[CONFIG_COLOR_NUM_LEDS * 4];
        for (int i = 0; i < CONFIG_COLOR_NUM_LEDS; i++) {
            pixels[i * 4 + 0] = g_modulated;
            pixels[i * 4 + 1] = r_modulated;
            pixels[i * 4 + 2] = b_modulated;
            pixels[i * 4 + 3] = w_modulated;
        }

        rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
        rmt_transmit(s_led_chan, s_led_encoder, pixels, sizeof(pixels), &tx_cfg);
        esp_rom_delay_us(SK6812_RESET_US);

        vTaskDelay(pdMS_TO_TICKS(30));  /* Update every 15ms */
    }
}

esp_err_t color_apply(uint8_t r, uint8_t g, uint8_t b, uint8_t w, bool flicker)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    s_color_r = r;
    s_color_g = g;
    s_color_b = b;
    s_color_w = w;
    s_flicker_enabled = flicker;

    /* If flicker task doesn't exist yet, create it. */
    if (s_flicker_task == NULL) {
        xTaskCreate(flicker_task, "flicker", 4096, NULL, 1, &s_flicker_task);
    }

    /* If flicker is off, display the color immediately without task. */
    if (!flicker) {
        uint8_t pixels[CONFIG_COLOR_NUM_LEDS * 4];
        for (int i = 0; i < CONFIG_COLOR_NUM_LEDS; i++) {
            pixels[i * 4 + 0] = g;
            pixels[i * 4 + 1] = r;
            pixels[i * 4 + 2] = b;
            pixels[i * 4 + 3] = w;
        }

        rmt_transmit_config_t tx_cfg = { .loop_count = 0 };
        esp_err_t err = rmt_transmit(s_led_chan, s_led_encoder,
                                     pixels, sizeof(pixels), &tx_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "rmt_transmit: %s", esp_err_to_name(err));
            return err;
        }

        esp_rom_delay_us(SK6812_RESET_US);
    }

    ESP_LOGD(TAG, "applied R=%u G=%u B=%u W=%u flicker=%s", r, g, b, w, flicker ? "on" : "off");
    return ESP_OK;
}

void color_get(uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *w)
{
    if (r) *r = s_color_r;
    if (g) *g = s_color_g;
    if (b) *b = s_color_b;
    if (w) *w = s_color_w;
}

void color_disable_flicker(void)
{
    s_flicker_enabled = false;
    ESP_LOGI(TAG, "flicker disabled");
}
