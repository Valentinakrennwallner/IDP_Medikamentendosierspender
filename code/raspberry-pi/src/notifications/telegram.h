#ifndef TELEGRAM_H
#define TELEGRAM_H

#include <stdint.h>

#include "../time_sync/time_sync.h"

typedef enum
{
    TELEGRAM_RESULT_OK = 0,
    TELEGRAM_RESULT_INVALID_ARGUMENT,
    TELEGRAM_RESULT_TRUSTED_TIME_UNAVAILABLE,
    TELEGRAM_RESULT_INITIALIZATION_FAILED,
    TELEGRAM_RESULT_TLS_BACKEND_UNSUPPORTED,
    TELEGRAM_RESULT_REQUEST_FAILED,
    TELEGRAM_RESULT_API_REJECTED
} telegram_result_t;

/*
 * Send one minimal Telegram Bot API text message over HTTPS. The supplied
 * epoch is used for certificate validity checks; use the last saved valid
 * epoch until NTP is synchronized.
 */
telegram_result_t telegram_send_message(const char *bot_token,
                                        const char *chat_id,
                                        const char *message,
                                        int64_t certificate_time_unix);

/* Select synchronized or persisted certificate time, then send the message. */
telegram_result_t telegram_send_message_with_time(
    const char *bot_token,
    const char *chat_id,
    const char *message,
    const time_sync_service_t *time_sync);

#endif
