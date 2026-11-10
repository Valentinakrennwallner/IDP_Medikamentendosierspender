#include "heartbeat_client.h"

#include <stddef.h>

/*
 * Initialize ESP32 heartbeat state. Design Document, Chapter 2, Section 2.1,
 * and Chapter 6, Section 6.1, Requirements 3.2 and 3.4.
 */
void heartbeat_client_init(heartbeat_client_t *client)
{
    if (client != NULL)
    {
        client->last_acknowledgement_ms = 0U;
        client->last_heartbeat_sent_ms = 0U;
        client->next_reconnect_ms = 0U;
        client->connected = false;
        client->autonomous = false;
        client->heartbeat_sent = false;
    }
}

/*
 * Begin heartbeat timing after connection authentication. Design Document,
 * Chapter 6, Section 6.1, Requirements 3.2 and 3.4.
 */
int heartbeat_client_on_connected(heartbeat_client_t *client,
                                  uint64_t monotonic_time_ms)
{
    if (client == NULL)
    {
        return -1;
    }

    client->last_acknowledgement_ms = monotonic_time_ms;
    client->last_heartbeat_sent_ms = monotonic_time_ms;
    client->next_reconnect_ms = 0U;
    client->connected = true;
    client->autonomous = false;
    client->heartbeat_sent = false;
    return 0;
}

/*
 * Record a valid acknowledgement only while connected. Design Document,
 * Chapter 6, Section 6.1, Requirement 3.2.
 */
int heartbeat_client_on_acknowledgement(heartbeat_client_t *client,
                                        uint64_t monotonic_time_ms)
{
    if ((client == NULL) || !client->connected || !client->heartbeat_sent ||
        (monotonic_time_ms < client->last_acknowledgement_ms))
    {
        return -1;
    }

    client->last_acknowledgement_ms = monotonic_time_ms;
    return 0;
}

/*
 * Enter autonomous mode once and schedule the first reconnect after five
 * seconds. Design Document, Chapter 6, Section 6.1, Requirement 3.2.
 */
int heartbeat_client_on_disconnected(heartbeat_client_t *client,
                                     uint64_t monotonic_time_ms,
                                     const heartbeat_client_ops_t *ops,
                                     void *context)
{
    int result = 0;

    if ((client == NULL) || (ops == NULL))
    {
        return -1;
    }

    if (client->connected)
    {
        client->connected = false;
        client->autonomous = true;
        client->next_reconnect_ms =
            monotonic_time_ms + HEARTBEAT_RECONNECT_INTERVAL_MS;
        if ((ops->enter_autonomous_mode == NULL) ||
            (ops->enter_autonomous_mode(context) != 0))
        {
            result = -1;
        }
    }

    return result;
}

/*
 * Terminate the connection and switch to autonomous mode.
 * Design Document, Chapter 6, Section 6.1, Requirement 3.2.
 */
static int heartbeat_client_drop_connection(heartbeat_client_t *client,
                                            uint64_t monotonic_time_ms,
                                            const heartbeat_client_ops_t *ops,
                                            void *context)
{
    int result = 0;

    if ((ops->disconnect_transport == NULL) ||
        (ops->disconnect_transport(context) != 0))
    {
        result = -1;
    }
    if (heartbeat_client_on_disconnected(client, monotonic_time_ms, ops,
                                         context) != 0)
    {
        result = -1;
    }

    return result;
}

/*
 * Run periodic heartbeat checks and background reconnect attempts. Design
 * Document, Chapter 6, Section 6.1, Requirements 3.2 and 3.4.
 */
int heartbeat_client_tick(heartbeat_client_t *client,
                          uint64_t monotonic_time_ms,
                          const heartbeat_client_ops_t *ops,
                          void *context)
{
    int result = 0;

    if ((client == NULL) || (ops == NULL))
    {
        return -1;
    }

    if (client->connected)
    {
        if ((client->heartbeat_sent &&
             (monotonic_time_ms < client->last_acknowledgement_ms)) ||
            (monotonic_time_ms < client->last_heartbeat_sent_ms))
        {
            return -1;
        }

        if (client->heartbeat_sent &&
            ((monotonic_time_ms - client->last_acknowledgement_ms) >=
             HEARTBEAT_ACK_TIMEOUT_MS))
        {
            return heartbeat_client_drop_connection(client, monotonic_time_ms,
                                                    ops, context);
        }

        if ((monotonic_time_ms - client->last_heartbeat_sent_ms) >=
            HEARTBEAT_SEND_INTERVAL_MS)
        {
            if ((ops->send_heartbeat == NULL) ||
                (ops->send_heartbeat(context) != 0))
            {
                (void)heartbeat_client_drop_connection(
                    client, monotonic_time_ms, ops, context);
                return -1;
            }

            if (!client->heartbeat_sent)
            {
                client->last_acknowledgement_ms = monotonic_time_ms;
                client->heartbeat_sent = true;
            }
            client->last_heartbeat_sent_ms = monotonic_time_ms;
        }
    }
    else if (client->autonomous &&
             (monotonic_time_ms >= client->next_reconnect_ms))
    {
        if ((ops->reconnect == NULL) || (ops->reconnect(context) != 0))
        {
            result = -1;
        }

        client->next_reconnect_ms =
            monotonic_time_ms + HEARTBEAT_RECONNECT_INTERVAL_MS;
    }

    return result;
}
