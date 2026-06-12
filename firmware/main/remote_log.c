#include "remote_log.h"

#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "cJSON.h"
#include "device_mac.h"
#include "device_mqtt.h"

static const char *TAG = "remote_log";

static char s_log_topic[64];
static char s_mac[DEVICE_MAC_STR_LEN];
static bool s_ready   = false;
static bool s_in_hook = false;  /* recursion guard */

static const char *level_char_to_str(char c)
{
    switch (c) {
        case 'E': return "ERROR";
        case 'W': return "WARN";
        case 'I': return "INFO";
        case 'D': return "DEBUG";
        case 'V': return "VERBOSE";
        default:  return "INFO";
    }
}

/* Intercepts every ESP_LOGx call, writes to serial, then forwards to MQTT. */
static int log_vprintf_hook(const char *fmt, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);
    int ret = vprintf(fmt, args);   /* serial output — consumes args */

    if (!s_ready || s_in_hook || !device_mqtt_is_connected()) {
        va_end(args_copy);
        return ret;
    }

    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, args_copy);
    va_end(args_copy);

    /* Parse the formatted ESP-IDF log line:
       [optional ANSI escape] LEVEL_CHAR (timestamp) TAG: message [optional ANSI reset] \n */
    const char *p = buf;

    /* Skip leading ANSI escape: \033[x;xxm */
    if (*p == '\033') {
        while (*p && *p != 'm') p++;
        if (*p == 'm') p++;
    }

    char level_char = *p++;
    if (level_char != 'E' && level_char != 'W' && level_char != 'I' &&
        level_char != 'D' && level_char != 'V') {
        return ret;
    }
    const char *level = level_char_to_str(level_char);

    /* Skip " (timestamp) " */
    while (*p == ' ') p++;
    if (*p == '(') {
        while (*p && *p != ')') p++;
        if (*p == ')') p++;
    }
    while (*p == ' ') p++;

    /* TAG: */
    const char *tag_start = p;
    while (*p && *p != ':') p++;
    char tag[64] = {0};
    size_t tag_len = (size_t)(p - tag_start);
    if (tag_len < sizeof(tag)) memcpy(tag, tag_start, tag_len);
    if (*p == ':') p++;
    while (*p == ' ') p++;

    /* Message — strip trailing ANSI reset and newlines */
    char message[384];
    strncpy(message, p, sizeof(message) - 1);
    message[sizeof(message) - 1] = '\0';

    char *end = message + strlen(message);
    while (end > message && (*(end-1) == '\n' || *(end-1) == '\r')) end--;
    if (end - message >= 4 && memcmp(end - 4, "\033[0m", 4) == 0) end -= 4;
    *end = '\0';

    if (tag[0] == '\0' || message[0] == '\0') return ret;

    s_in_hook = true;
    remote_log_post(level, tag, message);
    s_in_hook = false;

    return ret;
}

void remote_log_init(void)
{
    device_mac_get_str(s_mac);
    snprintf(s_log_topic, sizeof(s_log_topic), "devices/%s/logs", s_mac);
    s_ready = true;
    esp_log_set_vprintf(log_vprintf_hook);
    ESP_LOGI(TAG, "remote logging ready (mac=%s)", s_mac);
}

void remote_log_post(const char *level, const char *tag, const char *message)
{
    if (!s_ready) return;

    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "level",   level);
    cJSON_AddStringToObject(body, "tag",     tag);
    cJSON_AddStringToObject(body, "message", message);
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!json) return;

    device_mqtt_publish(s_log_topic, json, 0, 0);
    free(json);
}
