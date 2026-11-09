#include "../src/notifications/telegram.h"

#include <stdio.h>
#include <stdlib.h>

/* Provide callbacks so tests can create a valid time-sync service. */
static int test_request_sync(void *context)
{
    (void)context;
    return 0;
}

/* Return a positive test epoch as the synchronized wall clock. */
static int test_read_synchronized_time(void *context, int64_t *unix_time)
{
    (void)context;
    *unix_time = INT64_C(1700000000);
    return 0;
}

/* Accept the callback shape without making an actual Telegram request. */
static int test_send_time_alert(void *context)
{
    (void)context;
    return 0;
}

/* Ensure invalid Telegram inputs fail before any HTTPS request is made. */
static int test_rejects_invalid_inputs(void)
{
    if (telegram_send_message(NULL, "12345", "Reminder", INT64_C(1700000000)) !=
        TELEGRAM_RESULT_INVALID_ARGUMENT)
    {
        return 1;
    }

    if (telegram_send_message("123:token", "", "Reminder", INT64_C(1700000000)) !=
        TELEGRAM_RESULT_INVALID_ARGUMENT)
    {
        return 1;
    }

    if (telegram_send_message("123:token", "12345", "", INT64_C(1700000000)) !=
        TELEGRAM_RESULT_INVALID_ARGUMENT)
    {
        return 1;
    }

    if (telegram_send_message("123/token", "12345", "Reminder",
                              INT64_C(1700000000)) !=
        TELEGRAM_RESULT_INVALID_ARGUMENT)
    {
        return 1;
    }

    if (telegram_send_message("not-a-token", "12345", "Reminder",
                              INT64_C(1700000000)) !=
        TELEGRAM_RESULT_INVALID_ARGUMENT)
    {
        return 1;
    }

    if (telegram_send_message("123:token", "12345", "Reminder", 0) !=
        TELEGRAM_RESULT_INVALID_ARGUMENT)
    {
        return 1;
    }

    return 0;
}

/* Ensure HTTPS delivery refuses to run when no trusted certificate time exists. */
static int test_requires_trusted_certificate_time(void)
{
    time_sync_ops_t ops = {
        test_request_sync,
        test_read_synchronized_time,
        test_send_time_alert,
        NULL};
    time_sync_service_t time_sync;
    int64_t certificate_time = 0;

    if ((time_sync_init(&time_sync, &ops) != 0) ||
        (time_sync_get_certificate_verification_time(
             &time_sync, &certificate_time) == 0))
    {
        return 1;
    }

    if (telegram_send_message_with_time("123:token", "12345", "Reminder",
                                        &time_sync) !=
        TELEGRAM_RESULT_TRUSTED_TIME_UNAVAILABLE)
    {
        return 1;
    }

    return 0;
}

/* Run notification input and trusted-time checks without contacting Telegram. */
int main(void)
{
    if ((test_rejects_invalid_inputs() != 0) ||
        (test_requires_trusted_certificate_time() != 0))
    {
        (void)fputs("telegram tests failed\n", stderr);
        return EXIT_FAILURE;
    }

    (void)puts("telegram tests passed");
    return EXIT_SUCCESS;
}
