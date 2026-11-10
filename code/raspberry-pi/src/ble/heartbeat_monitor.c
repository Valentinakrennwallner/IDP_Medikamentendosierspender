#include "heartbeat_monitor.h"

#include <stddef.h>

/*
 * Initialize gateway heartbeat monitoring. Design Document, Chapter 2,
 * Section 2.1, and Chapter 6, Section 6.1, Requirements 3.4 and 3.5.
 */
void heartbeat_monitor_init(heartbeat_monitor_t *monitor)
{
    if (monitor != NULL)
    {
        monitor->last_heartbeat_ms = 0U;
        monitor->last_reconnect_attempt_ms = 0U;
        monitor->connected = false;
        monitor->monitoring = false;
        monitor->reconnect_attempted = false;
        monitor->alert_sent = false;
    }
}

/*
 * Start the first two-minute reachability window while searching for the
 * dispenser. Design Document, Chapter 6, Section 6.1, Requirement 3.5.
 */
int heartbeat_monitor_start(heartbeat_monitor_t *monitor,
                            uint64_t monotonic_time_ms)
{
    if (monitor == NULL)
    {
        return -1;
    }

    monitor->last_heartbeat_ms = monotonic_time_ms;
    monitor->last_reconnect_attempt_ms = 0U;
    monitor->connected = false;
    monitor->monitoring = true;
    monitor->reconnect_attempted = false;
    monitor->alert_sent = false;
    return 0;
}

/*
 * Begin a fresh monitoring period on an authenticated connection. Design
 * Document, Chapter 6, Section 6.1, Requirements 3.4 and 3.5.
 */
int heartbeat_monitor_on_connected(heartbeat_monitor_t *monitor,
                                   uint64_t monotonic_time_ms)
{
    if (heartbeat_monitor_start(monitor, monotonic_time_ms) != 0)
    {
        return -1;
    }

    monitor->connected = true;
    return 0;
}

/*
 * Acknowledge each authenticated heartbeat and refresh the timeout. Design
 * Document, Chapter 6, Section 6.1, Requirement 3.4.
 */
int heartbeat_monitor_on_heartbeat(heartbeat_monitor_t *monitor,
                                   uint64_t monotonic_time_ms,
                                   heartbeat_acknowledge_fn acknowledge,
                                   void *context)
{
    if ((monitor == NULL) || (acknowledge == NULL) || !monitor->connected ||
        !monitor->monitoring ||
        (monotonic_time_ms < monitor->last_heartbeat_ms))
    {
        return -1;
    }

    monitor->last_heartbeat_ms = monotonic_time_ms;
    monitor->reconnect_attempted = false;
    monitor->alert_sent = false;
    return acknowledge(context);
}

/*
 * Disable timeout alerts after the ESP32 reports completion of the day.
 * Design Document, Chapter 6, Section 6.1, Requirement 3.5.
 */
int heartbeat_monitor_on_day_end(heartbeat_monitor_t *monitor)
{
    if (monitor == NULL)
    {
        return -1;
    }

    monitor->monitoring = false;
    monitor->reconnect_attempted = false;
    return 0;
}

/*
 * Preserve timeout monitoring after a transport disconnect so a lost device
 * is reported at the specified two-minute threshold. Design Document,
 * Chapter 6, Section 6.1, Requirement 3.5.
 */
int heartbeat_monitor_on_disconnected(heartbeat_monitor_t *monitor)
{
    if (monitor == NULL)
    {
        return -1;
    }

    monitor->connected = false;
    return 0;
}

/*
 * Report a missed heartbeat once, then request reconnect attempts at
 * five-second intervals. Design Document, Chapter 6, Section 6.1,
 * Requirement 3.5.
 */
int heartbeat_monitor_tick(heartbeat_monitor_t *monitor,
                           uint64_t monotonic_time_ms,
                           heartbeat_alert_fn alert,
                           heartbeat_reconnect_fn reconnect,
                           void *context)
{
    int result = 0;
    bool timeout_reached;

    if (monitor == NULL)
    {
        return -1;
    }

    if (!monitor->monitoring)
    {
        return 0;
    }

    if (monotonic_time_ms < monitor->last_heartbeat_ms)
    {
        return -1;
    }

    timeout_reached =
        (monotonic_time_ms - monitor->last_heartbeat_ms) >=
        HEARTBEAT_TIMEOUT_MS;
    if (timeout_reached)
    {
        monitor->connected = false;

        if (!monitor->alert_sent)
        {
            if (alert == NULL)
            {
                result = -1;
            }
            else
            {
                monitor->alert_sent = true;
                if (alert(context) != 0)
                {
                    result = -1;
                }
            }
        }

        if (!monitor->reconnect_attempted ||
            ((monotonic_time_ms - monitor->last_reconnect_attempt_ms) >=
             HEARTBEAT_RECONNECT_INTERVAL_MS))
        {
            if (reconnect == NULL)
            {
                result = -1;
            }
            else
            {
                monitor->last_reconnect_attempt_ms = monotonic_time_ms;
                monitor->reconnect_attempted = true;
                if (reconnect(context) != 0)
                {
                    result = -1;
                }
            }
        }
    }

    return result;
}
