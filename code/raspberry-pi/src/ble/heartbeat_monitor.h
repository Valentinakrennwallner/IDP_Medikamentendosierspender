#ifndef HEARTBEAT_MONITOR_H
#define HEARTBEAT_MONITOR_H

#include <stdbool.h>
#include <stdint.h>

#define HEARTBEAT_TIMEOUT_MS UINT64_C(120000)
#define HEARTBEAT_RECONNECT_INTERVAL_MS UINT64_C(5000)

typedef int (*heartbeat_acknowledge_fn)(void *context);
typedef int (*heartbeat_alert_fn)(void *context);
typedef int (*heartbeat_reconnect_fn)(void *context);

typedef struct
{
    uint64_t last_heartbeat_ms;
    uint64_t last_reconnect_attempt_ms;
    bool connected;
    bool monitoring;
    bool day_ended;
    bool timed_out;
    bool reconnect_attempted;
    bool alert_sent;
} heartbeat_monitor_t;

/* Initialize the gateway heartbeat monitor in an inactive state. */
void heartbeat_monitor_init(heartbeat_monitor_t *monitor);

/* Begin the startup timeout while discovery/reconnection is in progress. */
int heartbeat_monitor_start(heartbeat_monitor_t *monitor,
                            uint64_t monotonic_time_ms);

/* Start monitoring from a newly authenticated BLE connection. */
int heartbeat_monitor_on_connected(heartbeat_monitor_t *monitor,
                                   uint64_t monotonic_time_ms);

/* Record a validated heartbeat and acknowledge it over the secure session. */
int heartbeat_monitor_on_heartbeat(heartbeat_monitor_t *monitor,
                                   uint64_t monotonic_time_ms,
                                   heartbeat_acknowledge_fn acknowledge,
                                   void *context);

/* Stop monitoring until the next connection after the ESP32 reports day end. */
int heartbeat_monitor_on_day_end(heartbeat_monitor_t *monitor);

/* Record a BLE disconnection while preserving the heartbeat timeout timer. */
int heartbeat_monitor_on_disconnected(heartbeat_monitor_t *monitor);

/* Alert once at timeout and attempt reconnection every five seconds. */
int heartbeat_monitor_tick(heartbeat_monitor_t *monitor,
                           uint64_t monotonic_time_ms,
                           heartbeat_alert_fn alert,
                           heartbeat_reconnect_fn reconnect,
                           void *context);

#endif
