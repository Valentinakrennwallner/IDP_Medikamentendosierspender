#ifndef LOGGING_H
#define LOGGING_H

/*
 * Gateway status logging (Raspberry Pi).
 *
 * Log lines are printed on the terminal only and never stored:
 *   2026-11-20T10:15:03Z [WARN] [ble] Reconnect failed (code 3)
 *
 * The API is deliberately not variadic: a printf-style "(module, ...)"
 * interface needs <stdarg.h>, which MISRA C:2012 Rule 17.1 (Required) does
 * not allow. Messages are fixed texts; the only number a caller can add is
 * the integer status/error code of log_error_code().
 *
 * Health data filter (Design Document, Chapter 6, Section 6.2,
 * Requirement 4.4): module names must match [a-z_]{1,15}; messages must be
 * printable ASCII, at most LOG_MESSAGE_MAX_LENGTH characters, and contain
 * no digits and no '%'. A message that fails is replaced by
 * "[ERROR] [<module>] Log message rejected". The filter cannot recognise
 * medication names, so callers must never pass configuration text
 * (medication names, Telegram texts, BLE addresses) as a message.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOG_MODULE_MAX_LENGTH 15U
#define LOG_MESSAGE_MAX_LENGTH 120U
#define LOG_QUEUE_CAPACITY 32U
/* Bounded trylock retries per message: a few microseconds, never a wait. */
#define LOG_LOCK_ATTEMPTS 100U

typedef enum
{
    LOG_RESULT_OK = 0,
    LOG_RESULT_ALREADY_ACTIVE = -1,
    LOG_RESULT_THREAD_ERROR = -2
} log_result_t;

/* Writes one complete log line; returns false if the output failed. */
typedef bool (*log_write_fn)(void *context, const char *text, size_t length);

typedef struct
{
    log_write_fn write;
    void *context;
} log_output_t;

/*
 * Start the low-priority logging thread. The start-up message
 *   "[INFO] [main] Gateway started, version <GATEWAY_SOFTWARE_VERSION>"
 * is queued before the thread starts, so it is always the first line and
 * is never dropped. output == NULL selects the terminal (standard output).
 * On failure logging stays off and every log_* call is a no-op; the caller
 * continues without logging.
 * Design Document, Chapter 6, Section 6.3, Requirements 5.1, 5.3 and 5.4.
 */
log_result_t log_init(const log_output_t *output);

/*
 * Queue an info/warning/error message. Never waits: if the queue stays
 * busy for LOG_LOCK_ATTEMPTS tries or is full, the message is dropped and
 * counted (see log_get_dropped_count()).
 */
void log_info(const char *module, const char *message);
void log_warn(const char *module, const char *message);
void log_error(const char *module, const char *message);

/*
 * Queue an error message with an integer status or error code (for example
 * an errno value or an attempt counter). Never pass doses, weights or
 * intake times as the code.
 */
void log_error_code(const char *module, const char *message, int32_t code);

/* Number of messages dropped because the queue was full or busy. */
uint32_t log_get_dropped_count(void);

/* Print the queued messages, then stop the logging thread. */
void log_shutdown(void);

#endif /* LOGGING_H */
