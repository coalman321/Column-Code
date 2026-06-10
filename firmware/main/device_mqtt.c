#include "device_mqtt.h"

#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "mqtt_client.h"   /* ESP-IDF MQTT component */
#include "device_mac.h"

static const char *TAG = "device_mqtt";

#define MAX_SUBS 8

typedef struct {
    const char        *topic;
    int                qos;
    mqtt_msg_handler_t handler;
} sub_t;

static sub_t s_subs[MAX_SUBS];
static int   s_nsubs     = 0;
static bool  s_connected = false;

static esp_mqtt_client_handle_t s_client = NULL;
static char s_status_topic[64];  /* "devices/{mac}/status" */

/* ── helpers ─────────────────────────────────────────────────────────────── */

static void resubscribe_all(void)
{
    for (int i = 0; i < s_nsubs; i++) {
        esp_mqtt_client_subscribe_single(s_client, s_subs[i].topic, s_subs[i].qos);
        ESP_LOGD(TAG, "subscribed: %s", s_subs[i].topic);
    }
}

static void dispatch(const char *topic, int tlen, const char *data, int dlen)
{
    for (int i = 0; i < s_nsubs; i++) {
        if ((int)strlen(s_subs[i].topic) == tlen &&
            memcmp(s_subs[i].topic, topic, tlen) == 0) {
            s_subs[i].handler(topic, tlen, data, dlen);
            return;
        }
    }
    ESP_LOGD(TAG, "unhandled topic: %.*s", tlen, topic);
}

/* ── MQTT event handler ──────────────────────────────────────────────────── */

static void mqtt_event_handler(void *arg,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    esp_mqtt_event_handle_t ev = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            /* Announce presence and set retained status. */
            esp_mqtt_client_publish(s_client, s_status_topic,
                                    "{\"connected\":true}", 0, 1, 1);
            resubscribe_all();
            ESP_LOGI(TAG, "connected");
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "disconnected");
            break;

        case MQTT_EVENT_DATA:
            dispatch(ev->topic, ev->topic_len, ev->data, ev->data_len);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "error type=%d",
                     ev->error_handle->error_type);
            break;

        default:
            break;
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void device_mqtt_subscribe(const char *topic_filter, int qos,
                           mqtt_msg_handler_t handler)
{
    if (s_nsubs >= MAX_SUBS) {
        ESP_LOGE(TAG, "subscription table full");
        return;
    }
    s_subs[s_nsubs].topic   = topic_filter;
    s_subs[s_nsubs].qos     = qos;
    s_subs[s_nsubs].handler = handler;
    s_nsubs++;
}

esp_err_t device_mqtt_start(const char *broker_url)
{
    char mac[DEVICE_MAC_STR_LEN];
    device_mac_get_str(mac);
    snprintf(s_status_topic, sizeof(s_status_topic), "devices/%s/status", mac);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = broker_url,
        .session.last_will  = {
            .topic  = s_status_topic,
            .msg    = "{\"connected\":false}",
            .msg_len = 0,   /* 0 = use strlen */
            .qos    = 1,
            .retain = 1,
        },
    };

    s_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID,
                                   mqtt_event_handler, NULL);
    return esp_mqtt_client_start(s_client);
}

esp_err_t device_mqtt_publish(const char *topic, const char *payload,
                              int qos, int retain)
{
    if (!s_client) return ESP_ERR_INVALID_STATE;
    int id = esp_mqtt_client_publish(s_client, topic, payload,
                                     strlen(payload), qos, retain);
    return (id >= 0) ? ESP_OK : ESP_FAIL;
}

bool device_mqtt_is_connected(void)
{
    return s_connected;
}
