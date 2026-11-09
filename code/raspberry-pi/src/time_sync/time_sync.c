#include "time_sync.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static bool time_sync_ops_are_valid(const time_sync_ops_t *ops)
{
    return (ops != NULL) && (ops->request_sync != NULL) &&
           (ops->read_synchronized_time != NULL) &&
           (ops->send_unavailable_alert != NULL);
}

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

int time_sync_load_last_valid_time(time_sync_service_t *service,
                                   const char *path)
{
    char buffer[32];
    char *end = NULL;
    intmax_t parsed_time;
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

    if ((fclose(file) != 0) || (parsed_time > INT64_MAX))
    {
        return -1;
    }

    service->last_valid_unix_time = (int64_t)parsed_time;
    service->has_last_valid_time = true;
    return 0;
}

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
