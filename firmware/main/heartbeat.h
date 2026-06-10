#pragma once

/* Configure wakeup GPIO, init HTTP state, then spawn the heartbeat task.
   Handles all light-sleep logic internally — callers need no sleep includes. */
void heartbeat_start(const char *server_base_url);
