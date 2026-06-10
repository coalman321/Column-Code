#pragma once

void remote_log_init(const char *server_base_url);
void remote_log_post(const char *level, const char *tag, const char *message);
