#include "../src/ble/heartbeat_monitor.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    unsigned int acknowledgements;
    unsigned int alerts;
    unsigned int reconnects;
} monitor_test_context_t;

/* Count secure acknowledgements without using a Bluetooth device. */
static int test_acknowledge(void *context)
{
    monitor_test_context_t *test_context = context;
    test_context->acknowledgements++;
    return 0;
}

/* Count one Telegram-alert request without sending a real message. */
static int test_alert(void *context)
{
    monitor_test_context_t *test_context = context;
    test_context->alerts++;
    return 0;
}

/* Count background reconnection requests. */
static int test_reconnect(void *context)
{
    monitor_test_context_t *test_context = context;
    test_context->reconnects++;
    return 0;
}

/* Verify acknowledgement cadence, two-minute timeout, and retry timing. */
static int test_timeout_and_reconnect(void)
{
    heartbeat_monitor_t monitor;
    monitor_test_context_t context = {0U, 0U, 0U};

    heartbeat_monitor_init(&monitor);
    if ((heartbeat_monitor_on_connected(&monitor, UINT64_C(0)) != 0) ||
        (heartbeat_monitor_on_heartbeat(&monitor, UINT64_C(30000),
                                         test_acknowledge, &context) != 0) ||
        (context.acknowledgements != 1U) ||
        (heartbeat_monitor_tick(&monitor, UINT64_C(149999), test_alert,
                                test_reconnect, &context) != 0) ||
        (context.alerts != 0U) || (context.reconnects != 0U) ||
        (heartbeat_monitor_tick(&monitor, UINT64_C(150000), test_alert,
                                test_reconnect, &context) != 0) ||
        (context.alerts != 1U) || (context.reconnects != 1U) ||
        (heartbeat_monitor_tick(&monitor, UINT64_C(154999), test_alert,
                                test_reconnect, &context) != 0) ||
        (context.alerts != 1U) || (context.reconnects != 1U) ||
        (heartbeat_monitor_tick(&monitor, UINT64_C(155000), test_alert,
                                test_reconnect, &context) != 0) ||
        (context.alerts != 1U) || (context.reconnects != 2U))
    {
        return 1;
    }

    return 0;
}

/* Verify end-of-day disables monitoring until a new connection. */
static int test_day_end_stops_monitoring(void)
{
    heartbeat_monitor_t monitor;
    monitor_test_context_t context = {0U, 0U, 0U};

    heartbeat_monitor_init(&monitor);
    if ((heartbeat_monitor_on_connected(&monitor, UINT64_C(0)) != 0) ||
        (heartbeat_monitor_on_day_end(&monitor) != 0) ||
        (heartbeat_monitor_on_disconnected(&monitor) != 0) ||
        (heartbeat_monitor_tick(&monitor, UINT64_C(300000), test_alert,
                                test_reconnect, &context) != 0) ||
        (context.alerts != 0U) || (context.reconnects != 0U) ||
        (heartbeat_monitor_on_connected(&monitor, UINT64_C(400000)) != 0) ||
        (heartbeat_monitor_tick(&monitor, UINT64_C(519999), test_alert,
                                test_reconnect, &context) != 0) ||
        (heartbeat_monitor_tick(&monitor, UINT64_C(520000), test_alert,
                                test_reconnect, &context) != 0) ||
        (context.alerts != 1U) || (context.reconnects != 1U))
    {
        return 1;
    }

    return 0;
}

/* Run gateway heartbeat monitor tests without BLE or Telegram services. */
int main(void)
{
    if ((test_timeout_and_reconnect() != 0) ||
        (test_day_end_stops_monitoring() != 0))
    {
        (void)fputs("heartbeat monitor tests failed\n", stderr);
        return EXIT_FAILURE;
    }

    (void)puts("heartbeat monitor tests passed");
    return EXIT_SUCCESS;
}
