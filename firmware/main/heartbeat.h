#pragma once

/* Configure wakeup GPIO, register MQTT cmd subscription, and spawn the
   heartbeat task.  Must be called after device_mqtt subscriptions are
   registered but before device_mqtt_start(). */
void heartbeat_start(void);

/* Signal the heartbeat task to enter light sleep at its next opportunity.
   Safe to call from any task. */
void heartbeat_request_sleep(void);
