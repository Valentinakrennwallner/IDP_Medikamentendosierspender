#include "../src/time_sync/time_sync.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    int sync_result;
    int clock_result;
    int alert_result;
    int alert_count;
    int64_t unix_time;
} fake_time_backend_t;

/* Simulate an NTP request for the time-sync unit tests. */
static int fake_request_sync(void *context)
{
    fake_time_backend_t *backend = context;
    return backend->sync_result;
}

/* Provide the fake system clock when synchronization has succeeded. */
static int fake_read_synchronized_time(void *context, int64_t *unix_time)
{
    fake_time_backend_t *backend = context;

    if (backend->clock_result != 0)
    {
        return backend->clock_result;
    }

    *unix_time = backend->unix_time;
    return 0;
}

/* Count simulated Telegram alerts and return the configured result. */
static int fake_send_unavailable_alert(void *context)
{
    fake_time_backend_t *backend = context;
    backend->alert_count++;
    return backend->alert_result;
}

/* Verify retries occur at 30 seconds and three failures stop operation. */
static int test_startup_retry_and_stop(void)
{
    fake_time_backend_t backend = {1, 0, 0, 0, 1700000000};
    time_sync_ops_t ops = {
        fake_request_sync,
        fake_read_synchronized_time,
        fake_send_unavailable_alert,
        &backend};
    time_sync_service_t service;
    uint32_t next_delay = 0U;

    if (time_sync_init(&service, &ops) != 0)
    {
        return 1;
    }

    if ((time_sync_step(&service, &next_delay) != 0) ||
        (next_delay != TIME_SYNC_RETRY_SECONDS) ||
        (service.consecutive_failures != 1U))
    {
        return 1;
    }

    if ((time_sync_step(&service, &next_delay) != 0) ||
        (next_delay != TIME_SYNC_RETRY_SECONDS) ||
        (service.consecutive_failures != 2U))
    {
        return 1;
    }

    if ((time_sync_step(&service, &next_delay) != 0) ||
        (next_delay != TIME_SYNC_OPERATIONAL_RETRY_SECONDS) ||
        !service.operation_stopped || (backend.alert_count != 1))
    {
        return 1;
    }

    if ((time_sync_step(&service, &next_delay) != 0) ||
        (next_delay != TIME_SYNC_OPERATIONAL_RETRY_SECONDS) ||
        (backend.alert_count != 1))
    {
        return 1;
    }

    return 0;
}

/* Verify successful retry resumes operation and schedules hourly sync. */
static int test_recovery_and_hourly_sync(void)
{
    fake_time_backend_t backend = {0, 0, 0, 0, 1700000100};
    time_sync_ops_t ops = {
        fake_request_sync,
        fake_read_synchronized_time,
        fake_send_unavailable_alert,
        &backend};
    time_sync_service_t service;
    uint32_t next_delay = 0U;

    if (time_sync_init(&service, &ops) != 0)
    {
        return 1;
    }

    service.operation_stopped = true;
    service.consecutive_failures = TIME_SYNC_FAILURE_LIMIT;

    if ((time_sync_step(&service, &next_delay) != 0) ||
        (next_delay != TIME_SYNC_INTERVAL_SECONDS) ||
        service.operation_stopped || (service.consecutive_failures != 0U) ||
        !service.has_last_valid_time ||
        (service.last_valid_unix_time != backend.unix_time))
    {
        return 1;
    }

    return 0;
}

/* Verify an alert-delivery failure is reported by the time service. */
static int test_alert_errors_are_reported(void)
{
    fake_time_backend_t backend = {1, 0, 1, 0, 1700000200};
    time_sync_ops_t ops = {
        fake_request_sync,
        fake_read_synchronized_time,
        fake_send_unavailable_alert,
        &backend};
    time_sync_service_t service;
    uint32_t next_delay = 0U;

    if (time_sync_init(&service, &ops) != 0)
    {
        return 1;
    }

    (void)time_sync_step(&service, &next_delay);
    (void)time_sync_step(&service, &next_delay);
    if ((time_sync_step(&service, &next_delay) == 0) ||
        !service.operation_stopped || (backend.alert_count != 1))
    {
        return 1;
    }

    return 0;
}

/* Verify the last valid epoch can be saved and loaded. */
static int test_last_valid_time_persistence(void)
{
    const char *path = "time_sync_last_time_test.tmp";
    fake_time_backend_t backend = {0, 0, 0, 0, 1700000300};
    time_sync_ops_t ops = {
        fake_request_sync,
        fake_read_synchronized_time,
        fake_send_unavailable_alert,
        &backend};
    time_sync_service_t service;
    time_sync_service_t restored_service;
    uint32_t next_delay = 0U;
    int result = 1;

    (void)remove(path);
    if ((time_sync_init(&service, &ops) != 0) ||
        (time_sync_init(&restored_service, &ops) != 0) ||
        (time_sync_step(&service, &next_delay) != 0) ||
        (time_sync_save_last_valid_time(&service, path) != 0) ||
        (time_sync_load_last_valid_time(&restored_service, path) != 0) ||
        !restored_service.has_last_valid_time ||
        (restored_service.last_valid_unix_time != backend.unix_time))
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    if (remove(path) != 0)
    {
        result = 1;
    }
    return result;
}

/* Run the time synchronization unit tests. */
int main(void)
{
    if ((test_startup_retry_and_stop() != 0) ||
        (test_recovery_and_hourly_sync() != 0) ||
        (test_alert_errors_are_reported() != 0) ||
        (test_last_valid_time_persistence() != 0))
    {
        (void)fputs("time_sync tests failed\n", stderr);
        return EXIT_FAILURE;
    }

    (void)puts("time_sync tests passed");
    return EXIT_SUCCESS;
}
