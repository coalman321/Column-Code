#pragma once

#include "esp_err.h"

void    heartbeat_init(const char *server_base_url);
esp_err_t heartbeat_send(void);
