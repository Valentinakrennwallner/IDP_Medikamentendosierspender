#include "time_sync.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

/* Ensure the platform supplies all callbacks needed by the time service. */
static bool time_sync_ops_are_valid(const time_sync_ops_t *ops)
{
    return (ops != NULL) && (ops->request_sync != NULL) &&
           (ops->read_synchronized_time != NULL) &&
           (ops->send_unavailable_alert != NULL);
}

/*
 * Initialize time synchronization state and its platform callbacks.
 * Design Document, Chapter 2, Section 2.1 (intake schedule and time control)
 * and Chapter 6, Section 6.1, Requirements 1.1–1.4.
 */
int time_sync_init(time_sync_service_t *service, const time_sync_ops_t *ops)
{
    if ((service == NULL) || !time_sync_ops_are_valid(ops))
    {
        return -1;
    }

    service->ops = *ops;
    service->consecutive_failures = 0U;
    service->operation_stopped = false;
    service->has_last_valid_time = false;
    service->last_valid_unix_time = 0;
    return 0;
}

/*
 * Read the current system time only while the service considers NTP valid.
 * Design Document, Chapter 6, Section 6.1, Requirement 1.4.
 */
int time_sync_get_current_time(const time_sync_service_t *service,
                               int64_t *unix_time)
{
    if ((service == NULL) || (unix_time == NULL) ||
        service->operation_stopped || !service->has_last_valid_time ||
        !time_sync_ops_are_valid(&service->ops))
    {
        return -1;
    }

    if ((service->ops.read_synchronized_time(service->ops.context, unix_time) != 0) ||
        (*unix_time <= 0))
    {
        return -1;
    }

    return 0;
}

/*
 * Run one NTP attempt and select the retry interval or stopped state.
 * Design Document, Chapter 2, Section 2.1 (intake schedule and time control)
 * and Chapter 6, Section 6.1, Requirements 1.1–1.3.
 */
int time_sync_step(time_sync_service_t *service, uint32_t *next_delay_seconds)
{
    int64_t synchronized_time = 0;
    bool synchronized = false;

    if ((service == NULL) || (next_delay_seconds == NULL) ||
        !time_sync_ops_are_valid(&service->ops))
    {
        return -1;
    }

    if (service->ops.request_sync(service->ops.context) == 0)
    {
        synchronized =
            (service->ops.read_synchronized_time(service->ops.context,
                                                 &synchronized_time) == 0) &&
            (synchronized_time > 0);
    }

    if (synchronized)
    {
        service->consecutive_failures = 0U;
        service->operation_stopped = false;
        service->has_last_valid_time = true;
        service->last_valid_unix_time = synchronized_time;
        *next_delay_seconds = TIME_SYNC_INTERVAL_SECONDS;
        return 0;
    }

    if (service->operation_stopped)
    {
        *next_delay_seconds = TIME_SYNC_OPERATIONAL_RETRY_SECONDS;
        return 0;
    }

    service->consecutive_failures++;
    if (service->consecutive_failures < TIME_SYNC_FAILURE_LIMIT)
    {
        *next_delay_seconds = TIME_SYNC_RETRY_SECONDS;
        return 0;
    }

    service->operation_stopped = true;
    *next_delay_seconds = TIME_SYNC_OPERATIONAL_RETRY_SECONDS;
    if (service->ops.send_unavailable_alert(service->ops.context) != 0)
    {
        return -1;
    }

    return 0;
}

/*
 * Restore the last valid epoch for pre-NTP TLS certificate checks.
 * Design Document, Chapter 2, Section 2.1 (Telegram connection) and Chapter
 * 6, Section 6.2, Requirement 4.10.
 */
int time_sync_load_last_valid_time(time_sync_service_t *service,
                                   const char *path)
{
    char buffer[32];
    char *end = NULL;
    intmax_t parsed_time;
    int trailing_character;
    int file_error;
    int close_result;
    FILE *file;

    if ((service == NULL) || (path == NULL))
    {
        return -1;
    }

    file = fopen(path, "r");
    if (file == NULL)
    {
        return -1;
    }

    if (fgets(buffer, sizeof(buffer), file) == NULL)
    {
        (void)fclose(file);
        return -1;
    }

    errno = 0;
    parsed_time = strtoimax(buffer, &end, 10);
    if ((errno != 0) || (end == buffer) || (parsed_time <= 0) ||
        ((*end != '\0') && (*end != '\n')))
    {
        (void)fclose(file);
        return -1;
    }

    trailing_character = fgetc(file);
    file_error = ferror(file);
    close_result = fclose(file);
    if ((trailing_character != EOF) || (file_error != 0) ||
        (close_result != 0) || (parsed_time > INT64_MAX))
    {
        return -1;
    }

    service->last_valid_unix_time = (int64_t)parsed_time;
    service->has_last_valid_time = true;
    return 0;
}

/*
 * Persist the last valid epoch for TLS certificate checks after restart.
 * Design Document, Chapter 2, Section 2.1 (Telegram connection) and Chapter
 * 6, Section 6.2, Requirement 4.10.
 */
int time_sync_save_last_valid_time(const time_sync_service_t *service,
                                   const char *path)
{
    FILE *file;
    int write_result;
    int close_result;

    if ((service == NULL) || (path == NULL) ||
        !service->has_last_valid_time || (service->last_valid_unix_time <= 0))
    {
        return -1;
    }

    file = fopen(path, "w");
    if (file == NULL)
    {
        return -1;
    }

    write_result = fprintf(file, "%" PRId64 "\n", service->last_valid_unix_time);
    close_result = fclose(file);
    if ((write_result < 0) || (close_result != 0))
    {
        return -1;
    }

    return 0;
}
