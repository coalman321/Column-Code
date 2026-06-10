#include "color.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_http_client.h"
#include "driver/ledc.h"
#include "cJSON.h"

static const char *TAG = "color";

/* ── LEDC config ─────────────────────────────────────────────────────────── */
#define LEDC_TIMER        LEDC_TIMER_1
#define LEDC_MODE         LEDC_LOW_SPEED_MODE
#define LEDC_RESOLUTION   LEDC_TIMER_13_BIT
#define LEDC_FREQ_HZ      5000
#define LEDC_MAX_DUTY     ((1u << 13) - 1u)   /* 8191 */

static const ledc_channel_t s_channels[4] = {
    LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2, LEDC_CHANNEL_3
};
static const int s_gpios[4] = {
    CONFIG_COLOR_GPIO_R, CONFIG_COLOR_GPIO_G,
    CONFIG_COLOR_GPIO_B, CONFIG_COLOR_GPIO_W,
};

/* ── State ───────────────────────────────────────────────────────────────── */
#define URL_BUF_SIZE  192
#define RESP_BUF_SIZE 128

static char s_url[URL_BUF_SIZE];
static bool s_ready = false;

/* ── HTTP response capture ───────────────────────────────────────────────── */
typedef struct {
    char buf[RESP_BUF_SIZE];
    int  len;
} resp_ctx_t;

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    resp_ctx_t *ctx = (resp_ctx_t *)evt->user_data;
    int copy = evt->data_len;
    if (ctx->len + copy >= RESP_BUF_SIZE) {
        copy = RESP_BUF_SIZE - ctx->len - 1;
    }
    if (copy > 0) {
        memcpy(ctx->buf + ctx->len, evt->data, copy);
        ctx->len += copy;
    }
    return ESP_OK;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

esp_err_t color_init(const char *server_base_url)
{
    /* Derive MAC the same way heartbeat does. */
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    snprintf(s_url, sizeof(s_url), "%s/colors/%s", server_base_url, mac_str);

    /* Set up LEDC timer. */
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RESOLUTION,
        .freq_hz         = LEDC_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Set up one LEDC channel per colour channel. */
    for (int i = 0; i < 4; i++) {
        ledc_channel_config_t ch_cfg = {
            .gpio_num   = s_gpios[i],
            .speed_mode = LEDC_MODE,
            .channel    = s_channels[i],
            .timer_sel  = LEDC_TIMER,
            .duty       = 0,
            .hpoint     = 0,
        };
        err = ledc_channel_config(&ch_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "ledc_channel_config ch%d failed: %s", i, esp_err_to_name(err));
            return err;
        }
    }

    s_ready = true;
    ESP_LOGI(TAG, "color init done (url=%s)", s_url);
    return ESP_OK;
}

esp_err_t color_apply(uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
    uint8_t values[4] = { r, g, b, w };
    for (int i = 0; i < 4; i++) {
        uint32_t duty = (uint32_t)values[i] * LEDC_MAX_DUTY / 255u;
        ledc_set_duty(LEDC_MODE, s_channels[i], duty);
        ledc_update_duty(LEDC_MODE, s_channels[i]);
    }
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
        ESP_LOGW(TAG, "server returned HTTP %d", status);
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

    uint8_t rv = (uint8_t)jr->valueint;
    uint8_t gv = (uint8_t)jg->valueint;
    uint8_t bv = (uint8_t)jb->valueint;
    uint8_t wv = (uint8_t)jw->valueint;
    cJSON_Delete(root);

    return color_apply(rv, gv, bv, wv);
}
