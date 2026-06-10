#pragma once

#include "esp_err.h"

esp_err_t ota_check_and_update(const char *server_base_url);
