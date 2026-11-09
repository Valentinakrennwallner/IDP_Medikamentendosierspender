#ifndef TIME_SYNC_H
#define TIME_SYNC_H

#include <stdbool.h>
#include <stdint.h>

#define TIME_SYNC_RETRY_SECONDS 30U
#define TIME_SYNC_OPERATIONAL_RETRY_SECONDS 300U
#define TIME_SYNC_INTERVAL_SECONDS 3600U
#define TIME_SYNC_FAILURE_LIMIT 3U

typedef struct
{
    /* Return zero only after the NTP synchronization attempt has completed. */
    int (*request_sync)(void *context);
    /* Return zero only when the system clock is NTP-synchronized and readable. */
    int (*read_synchronized_time)(void *context, int64_t *unix_time);
    int (*send_unavailable_alert)(void *context);
    void *context;
} time_sync_ops_t;

typedef struct
{
    time_sync_ops_t ops;
    uint32_t consecutive_failures;
    bool operation_stopped;
    bool has_synchronized_time;
    bool has_last_valid_time;
    int64_t last_valid_unix_time;
} time_sync_service_t;

int time_sync_init(time_sync_service_t *service, const time_sync_ops_t *ops);
int time_sync_get_current_time(const time_sync_service_t *service,
                               int64_t *unix_time);
int time_sync_get_certificate_verification_time(
    const time_sync_service_t *service,
    int64_t *unix_time);

/*
 * Perform one synchronization attempt.
 * Returns zero if the attempt was processed, or -1 on invalid input or if
 * alert delivery fails. A processed attempt can still report unsynchronized
 * time through the service state and retry delay.
 * next_delay_seconds receives the delay before the next attempt.
 */
int time_sync_step(time_sync_service_t *service, uint32_t *next_delay_seconds);

/*
 * Load/store the last valid time for certificate checks before NTP is ready.
 * The application must load this before its first HTTPS request and save it
 * during graceful shutdown.
 */
int time_sync_load_last_valid_time(time_sync_service_t *service,
                                   const char *path);
int time_sync_save_last_valid_time(const time_sync_service_t *service,
                                   const char *path);

#endif
