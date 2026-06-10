#include "color.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "device_mac.h"
#include "esp_rom_sys.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "cJSON.h"

static const char *TAG = "color";

/* ── RMT / SK6812 config ─────────────────────────────────────────────────── */
#define RMT_RESOLUTION_HZ  10000000  /* 10 MHz → 100 ns per tick              */

/* SK6812 bit timing at 10 MHz (datasheet: T0H=300ns T0L=900ns T1H=600ns T1L=600ns) */
#define SK6812_T0H  3   /* 300 ns */
#define SK6812_T0L  9   /* 900 ns */
#define SK6812_T1H  6   /* 600 ns */
#define SK6812_T1L  6   /* 600 ns */

/* Reset: data line low >80 µs; we busy-wait after the last transmission.    */
#define SK6812_RESET_US  100

static rmt_channel_handle_t s_led_chan    = NULL;
static rmt_encoder_handle_t s_led_encoder = NULL;

/* ── HTTP state ──────────────────────────────────────────────────────────── */
#define URL_BUF_SIZE   192
#define RESP_BUF_SIZE  128

static char s_url[URL_BUF_SIZE];
static bool s_ready = false;

typedef struct { char buf[RESP_BUF_SIZE]; int len; } resp_ctx_t;

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    resp_ctx_t *ctx = evt->user_data;
    int copy = evt->data_len;
    if (ctx->len + copy >= RESP_BUF_SIZE) copy = RESP_BUF_SIZE - ctx->len - 1;
    if (copy > 0) {
        memcpy(ctx->buf + ctx->len, evt->data, copy);
        ctx->len += copy;
    }
    return ESP_OK;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

esp_err_t color_init(const char *server_base_url)
{
    char mac_str[DEVICE_MAC_STR_LEN];
    device_mac_get_str(mac_str);
    snprintf(s_url, sizeof(s_url), "%s/colors/%s", server_base_url, mac_str);

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

    /* Bytes encoder — sends each byte MSB-first using SK6812 timings. */
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
    ESP_LOGI(TAG, "SK6812 ready — %d LED(s) on GPIO %d",
             CONFIG_COLOR_NUM_LEDS, CONFIG_COLOR_GPIO_DATA);
    return ESP_OK;
}

esp_err_t color_apply(uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    /* SK6812 wire order is G R B W. */
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

    rmt_tx_wait_all_done(s_led_chan, pdMS_TO_TICKS(100));

    /* Hold line low for the SK6812 reset period. */
    esp_rom_delay_us(SK6812_RESET_US);

    ESP_LOGD(TAG, "applied R=%u G=%u B=%u W=%u", r, g, b, w);
    return ESP_OK;
}

esp_err_t color_fetch_and_apply(void)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    resp_ctx_t ctx = { .len = 0 };
    esp_http_client_config_t cfg = {
        .url           = s_url,
        .method        = HTTP_METHOD_GET,
        .timeout_ms    = 5000,
        .event_handler = on_http_event,
        .user_data     = &ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(client);
    int status    = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "fetch failed: %s", esp_err_to_name(err));
        return err;
    }
    if (status != 200) {
        ESP_LOGW(TAG, "HTTP %d", status);
        return ESP_FAIL;
    }

    ctx.buf[ctx.len] = '\0';
    cJSON *root = cJSON_Parse(ctx.buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed: %.64s", ctx.buf);
        return ESP_FAIL;
    }

    cJSON *jr = cJSON_GetObjectItem(root, "r");
    cJSON *jg = cJSON_GetObjectItem(root, "g");
    cJSON *jb = cJSON_GetObjectItem(root, "b");
    cJSON *jw = cJSON_GetObjectItem(root, "w");

    if (!cJSON_IsNumber(jr) || !cJSON_IsNumber(jg) ||
        !cJSON_IsNumber(jb) || !cJSON_IsNumber(jw)) {
        ESP_LOGE(TAG, "unexpected JSON fields");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    uint8_t rv = (uint8_t)jr->valueint, gv = (uint8_t)jg->valueint,
            bv = (uint8_t)jb->valueint, wv = (uint8_t)jw->valueint;
    cJSON_Delete(root);
    return color_apply(rv, gv, bv, wv);
}
