#include "../../esp32/components/ble/heartbeat_client.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    unsigned int heartbeats;
    unsigned int disconnects;
    unsigned int autonomous_transitions;
    unsigned int reconnects;
} client_test_context_t;

/* Count heartbeat messages in place of the authenticated BLE sender. */
static int test_send_heartbeat(void *context)
{
    client_test_context_t *test_context = context;
    test_context->heartbeats++;
    return 0;
}

/* Count disconnect requests from the heartbeat timeout path. */
static int test_disconnect(void *context)
{
    client_test_context_t *test_context = context;
    test_context->disconnects++;
    return 0;
}

/* Count transitions into local autonomous operation. */
static int test_enter_autonomous_mode(void *context)
{
    client_test_context_t *test_context = context;
    test_context->autonomous_transitions++;
    return 0;
}

/* Count reconnect attempts without starting a BLE connection. */
static int test_reconnect(void *context)
{
    client_test_context_t *test_context = context;
    test_context->reconnects++;
    return 0;
}

/* Verify 30-second heartbeats, 2-minute timeout, and 5-second retries. */
static int test_periodic_heartbeat_and_reconnect(void)
{
    heartbeat_client_t client;
    client_test_context_t context = {0U, 0U, 0U, 0U};
    const heartbeat_client_ops_t ops = {
        test_send_heartbeat,
        test_disconnect,
        test_enter_autonomous_mode,
        test_reconnect};

    heartbeat_client_init(&client);
    if ((heartbeat_client_on_connected(&client, UINT64_C(0)) != 0) ||
        (heartbeat_client_tick(&client, UINT64_C(29999), &ops, &context) !=
         0) ||
        (context.heartbeats != 0U) ||
        (heartbeat_client_tick(&client, UINT64_C(30000), &ops, &context) !=
         0) ||
        (context.heartbeats != 1U) ||
        (heartbeat_client_on_acknowledgement(&client, UINT64_C(31000)) !=
         0) ||
        (heartbeat_client_tick(&client, UINT64_C(150999), &ops, &context) !=
         0) ||
        (context.disconnects != 0U) ||
        (heartbeat_client_tick(&client, UINT64_C(151000), &ops, &context) !=
         0) ||
        (context.disconnects != 1U) ||
        (context.autonomous_transitions != 1U) ||
        (heartbeat_client_tick(&client, UINT64_C(155999), &ops, &context) !=
         0) ||
        (context.reconnects != 0U) ||
        (heartbeat_client_tick(&client, UINT64_C(156000), &ops, &context) !=
         0) ||
        (context.reconnects != 1U) ||
        (heartbeat_client_tick(&client, UINT64_C(161000), &ops, &context) !=
         0) ||
        (context.reconnects != 2U) ||
        (heartbeat_client_on_connected(&client, UINT64_C(161001)) != 0) ||
        (heartbeat_client_tick(&client, UINT64_C(166001), &ops, &context) !=
         0) ||
        (context.reconnects != 2U))
    {
        return 1;
    }

    return 0;
}

/* Start the two-minute acknowledgement timeout at the first heartbeat sent. */
static int test_timeout_from_first_heartbeat(void)
{
    heartbeat_client_t client;
    client_test_context_t context = {0U, 0U, 0U, 0U};
    const heartbeat_client_ops_t ops = {
        test_send_heartbeat,
        test_disconnect,
        test_enter_autonomous_mode,
        test_reconnect};

    heartbeat_client_init(&client);
    if ((heartbeat_client_on_connected(&client, UINT64_C(0)) != 0) ||
        (heartbeat_client_tick(&client, UINT64_C(30000), &ops, &context) !=
         0) ||
        (context.heartbeats != 1U) ||
        (heartbeat_client_tick(&client, UINT64_C(149999), &ops, &context) !=
         0) ||
        (context.disconnects != 0U) ||
        (heartbeat_client_tick(&client, UINT64_C(150000), &ops, &context) !=
         0) ||
        (context.disconnects != 1U) ||
        (context.autonomous_transitions != 1U))
    {
        return 1;
    }

    return 0;
}

/* Run ESP32 heartbeat logic tests without ESP-IDF or Bluetooth hardware. */
int main(void)
{
    if ((test_periodic_heartbeat_and_reconnect() != 0) ||
        (test_timeout_from_first_heartbeat() != 0))
    {
        (void)fputs("heartbeat client tests failed\n", stderr);
        return EXIT_FAILURE;
    }

    (void)puts("heartbeat client tests passed");
    return EXIT_SUCCESS;
}
