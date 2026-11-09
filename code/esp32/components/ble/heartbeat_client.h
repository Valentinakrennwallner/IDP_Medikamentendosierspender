#ifndef HEARTBEAT_CLIENT_H
#define HEARTBEAT_CLIENT_H

#include <stdbool.h>
#include <stdint.h>

#define HEARTBEAT_SEND_INTERVAL_MS UINT64_C(30000)
#define HEARTBEAT_ACK_TIMEOUT_MS UINT64_C(120000)
#define HEARTBEAT_RECONNECT_INTERVAL_MS UINT64_C(5000)

typedef int (*heartbeat_send_fn)(void *context);
typedef int (*heartbeat_disconnect_fn)(void *context);
typedef int (*heartbeat_autonomous_fn)(void *context);
typedef int (*heartbeat_reconnect_fn)(void *context);

typedef struct
{
    uint64_t last_acknowledgement_ms;
    uint64_t last_heartbeat_sent_ms;
    uint64_t next_reconnect_ms;
    bool connected;
    bool autonomous;
    bool reconnect_scheduled;
    bool heartbeat_sent;
} heartbeat_client_t;

typedef struct
{
    heartbeat_send_fn send_heartbeat;
    heartbeat_disconnect_fn disconnect_transport;
    heartbeat_autonomous_fn enter_autonomous_mode;
    heartbeat_reconnect_fn reconnect;
} heartbeat_client_ops_t;

/* Initialize the heartbeat client before starting the BLE connection flow. */
void heartbeat_client_init(heartbeat_client_t *client);

/* Start a heartbeat session after the authenticated BLE connection succeeds. */
int heartbeat_client_on_connected(heartbeat_client_t *client,
                                  uint64_t monotonic_time_ms);

/* Refresh the timeout after an authenticated, non-replayed acknowledgement. */
int heartbeat_client_on_acknowledgement(heartbeat_client_t *client,
                                        uint64_t monotonic_time_ms);

/* Switch to autonomous operation when the BLE stack reports disconnection. */
int heartbeat_client_on_disconnected(heartbeat_client_t *client,
                                     uint64_t monotonic_time_ms,
                                     const heartbeat_client_ops_t *ops,
                                     void *context);

/* Send periodic heartbeats, detect missing acknowledgements, and reconnect. */
int heartbeat_client_tick(heartbeat_client_t *client,
                          uint64_t monotonic_time_ms,
                          const heartbeat_client_ops_t *ops,
                          void *context);

#endif
