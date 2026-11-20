/*
 * Gateway status logging (Raspberry Pi).
 *
 * Callers fill a static ring buffer without ever waiting: they try the
 * mutex at most LOG_LOCK_ATTEMPTS times (trylock, no sleep) and drop the
 * message if it stays busy. A low-priority thread prints the entries on the
 * terminal with write(). The mutex is held only to insert or copy out one
 * entry, never during write(), so a slow terminal cannot make callers drop
 * messages. log_init() puts the start-up message into the buffer before the
 * thread exists, so it can never be dropped. Nothing is stored in a file.
 *
 * Compile definitions (set in CMakeLists.txt for this file, not with
 * #define, see MISRA C:2012 Rule 21.1):
 * - _GNU_SOURCE: SCHED_IDLE is a Linux extension of <sched.h>; it is the
 *   lowest thread priority and needs no root rights.
 * - GATEWAY_SOFTWARE_VERSION: the CMake project version, for the start-up
 *   message.
 */

#include "logging.h"

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <time.h>
#include <unistd.h>

#ifndef GATEWAY_SOFTWARE_VERSION
#error "GATEWAY_SOFTWARE_VERSION must be defined by the build system"
#endif

#define LOG_STARTUP_MESSAGE "Gateway started, version " GATEWAY_SOFTWARE_VERSION
#define LOG_REJECTED_MESSAGE "Log message rejected"
#define LOG_DROPPED_MESSAGE "Log messages dropped"
#define LOG_OWN_MODULE "logging"
#define LOG_LINE_CAPACITY 256U

_Static_assert(sizeof(LOG_STARTUP_MESSAGE) <= (LOG_MESSAGE_MAX_LENGTH + 1U),
               "start-up message too long");

typedef enum
{
    LOG_LEVEL_INFO = 0,
    LOG_LEVEL_WARN = 1,
    LOG_LEVEL_ERROR = 2
} log_level_t;

typedef struct
{
    log_level_t level;
    time_t timestamp_seconds;
    bool timestamp_valid;
    bool has_code;
    int32_t code;
    char module[LOG_MODULE_MAX_LENGTH + 1U];
    char message[LOG_MESSAGE_MAX_LENGTH + 1U];
} log_entry_t;

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t log_condition = PTHREAD_COND_INITIALIZER;

/* Protected by log_mutex. */
static log_entry_t log_queue[LOG_QUEUE_CAPACITY];
static size_t log_queue_head = 0U;
static size_t log_queue_count = 0U;
static bool log_stop_requested = false;

/* Read by callers without the mutex. */
static atomic_bool log_active = false;
static atomic_uint_least32_t log_dropped_count = 0U;

/* Used only by log_init()/log_shutdown() and the logging thread. */
static log_output_t log_output;
static pthread_t log_thread;
static bool log_thread_started = false;

/*
 * Write a whole line to standard output, retrying partial writes and
 * interrupted calls. Design Document, Chapter 6, Section 6.3,
 * Requirement 5.1 (terminal output, nothing stored).
 */
static bool log_write_terminal(void *context, const char *text, size_t length)
{
    size_t written = 0U;
    bool success = true;

    (void)context;
    while (success && (written < length))
    {
        ssize_t result;

        errno = 0;
        result = write(STDOUT_FILENO, &text[written], length - written);

        if (result > 0)
        {
            written += (size_t)result;
        }
        else if ((result < 0) && (errno == EINTR))
        {
            /* Interrupted before anything was written: try again. */
        }
        else
        {
            success = false;
        }
    }

    return success;
}

/*
 * Accept only module names of the form [a-z_]{1,15}.
 * Design Document, Chapter 6, Section 6.3, Requirement 5.2 and
 * Section 6.2, Requirement 4.4.
 */
static bool log_module_is_valid(const char *module)
{
    size_t index = 0U;
    bool valid = (module != NULL);

    while (valid && (index <= LOG_MODULE_MAX_LENGTH) && (module[index] != '\0'))
    {
        char character = module[index];

        valid = ((character >= 'a') && (character <= 'z')) || (character == '_');
        index++;
    }

    return valid && (index > 0U) && (index <= LOG_MODULE_MAX_LENGTH);
}

/*
 * Health data filter: accept only printable ASCII without digits and '%'
 * (no intake times, doses, weights or format strings), at most
 * LOG_MESSAGE_MAX_LENGTH characters.
 * Design Document, Chapter 6, Section 6.2, Requirement 4.4 and
 * Chapter 5 (Threat 3).
 */
static bool log_message_is_valid(const char *message)
{
    size_t index = 0U;
    bool valid = (message != NULL);

    while (valid && (index <= LOG_MESSAGE_MAX_LENGTH) && (message[index] != '\0'))
    {
        char character = message[index];
        bool printable = (character >= ' ') && (character <= '~');
        bool digit = (character >= '0') && (character <= '9');

        valid = printable && !digit && (character != '%');
        index++;
    }

    return valid && (index > 0U) && (index <= LOG_MESSAGE_MAX_LENGTH);
}

/*
 * Copy a validated, NUL-terminated text into a fixed entry field.
 * Design Document, Chapter 2, Section 2.1 (System start and logging).
 */
static void log_copy_text(char *destination, size_t capacity, const char *source)
{
    size_t index = 0U;

    while (((index + 1U) < capacity) && (source[index] != '\0'))
    {
        destination[index] = source[index];
        index++;
    }
    destination[index] = '\0';
}

/*
 * Build an entry with the current UTC time, a level and a module name.
 * Design Document, Chapter 6, Section 6.3, Requirement 5.2; time from
 * clock_gettime(), Chapter 2, Section 2.2.
 */
static void log_build_entry(log_entry_t *entry, log_level_t level, const char *module,
                            const char *message, bool has_code, int32_t code)
{
    struct timespec now;

    entry->level = level;
    entry->timestamp_valid = (clock_gettime(CLOCK_REALTIME, &now) == 0);
    entry->timestamp_seconds = entry->timestamp_valid ? now.tv_sec : (time_t)0;
    entry->has_code = has_code;
    entry->code = code;
    log_copy_text(entry->module, sizeof(entry->module), module);
    log_copy_text(entry->message, sizeof(entry->message), message);
}

/*
 * Insert an entry into the ring buffer without waiting: the mutex is tried
 * at most LOG_LOCK_ATTEMPTS times; if it stays busy or the queue is full,
 * the entry is dropped and counted.
 * Design Document, Chapter 6, Section 6.3, Requirement 5.4.
 */
static void log_enqueue(const log_entry_t *entry)
{
    bool queued = false;
    bool locked = false;
    uint32_t attempt;

    for (attempt = 0U; (!locked) && (attempt < LOG_LOCK_ATTEMPTS); attempt++)
    {
        locked = (pthread_mutex_trylock(&log_mutex) == 0);
    }

    if (locked)
    {
        if ((!log_stop_requested) && (log_queue_count < LOG_QUEUE_CAPACITY))
        {
            size_t tail = (log_queue_head + log_queue_count) % LOG_QUEUE_CAPACITY;

            log_queue[tail] = *entry;
            log_queue_count++;
            queued = true;
        }
        if (pthread_mutex_unlock(&log_mutex) != 0)
        {
            /* Cannot happen for a mutex owned by this thread; logging only. */
            atomic_store(&log_active, false);
        }
    }

    if (queued)
    {
        /* pthread_cond_signal() does not block. */
        (void)pthread_cond_signal(&log_condition);
    }
    else
    {
        (void)atomic_fetch_add(&log_dropped_count, 1U);
    }
}

/*
 * Validate and queue one message; a message that fails the health data
 * filter is replaced by "Log message rejected".
 * Design Document, Chapter 6, Section 6.2, Requirement 4.4 and
 * Section 6.3, Requirements 5.2 and 5.4.
 */
static void log_submit(log_level_t level, const char *module, const char *message,
                       bool has_code, int32_t code)
{
    log_entry_t entry;
    bool module_valid;

    if (!atomic_load(&log_active))
    {
        return;
    }

    module_valid = log_module_is_valid(module);
    if (module_valid && log_message_is_valid(message))
    {
        log_build_entry(&entry, level, module, message, has_code, code);
    }
    else
    {
        log_build_entry(&entry, LOG_LEVEL_ERROR, module_valid ? module : LOG_OWN_MODULE,
                        LOG_REJECTED_MESSAGE, false, 0);
    }
    log_enqueue(&entry);
}

/*
 * Append a text to the line buffer, truncating at its capacity.
 * Design Document, Chapter 6, Section 6.3, Requirement 5.2.
 */
static void log_append_text(char *line, size_t *length, const char *text)
{
    size_t index = 0U;

    while ((*length < LOG_LINE_CAPACITY) && (text[index] != '\0'))
    {
        line[*length] = text[index];
        (*length)++;
        index++;
    }
}

/*
 * Append an unsigned number with at least min_digits digits (zero padded).
 * Hand-written so the logging thread needs no <stdio.h> (MISRA C:2012
 * Rule 21.6). Design Document, Chapter 6, Section 6.3, Requirement 5.2.
 */
static void log_append_unsigned(char *line, size_t *length, uint32_t value,
                                size_t min_digits)
{
    static const char digit_characters[] = "0123456789";
    char digits[10];
    size_t count = 0U;
    uint32_t remaining = value;

    do
    {
        digits[count] = digit_characters[remaining % 10U];
        remaining /= 10U;
        count++;
    } while ((remaining != 0U) && (count < sizeof(digits)));

    while ((count < min_digits) && (count < sizeof(digits)))
    {
        digits[count] = '0';
        count++;
    }

    while ((count > 0U) && (*length < LOG_LINE_CAPACITY))
    {
        count--;
        line[*length] = digits[count];
        (*length)++;
    }
}

/*
 * Append the timestamp as UTC ISO 8601, e.g. 2026-11-20T10:15:03Z.
 * Design Document, Chapter 6, Section 6.3, Requirement 5.2.
 */
static void log_append_timestamp(char *line, size_t *length, const log_entry_t *entry)
{
    struct tm utc_time;

    if (entry->timestamp_valid && (gmtime_r(&entry->timestamp_seconds, &utc_time) != NULL))
    {
        log_append_unsigned(line, length, (uint32_t)utc_time.tm_year + 1900U, 4U);
        log_append_text(line, length, "-");
        log_append_unsigned(line, length, (uint32_t)utc_time.tm_mon + 1U, 2U);
        log_append_text(line, length, "-");
        log_append_unsigned(line, length, (uint32_t)utc_time.tm_mday, 2U);
        log_append_text(line, length, "T");
        log_append_unsigned(line, length, (uint32_t)utc_time.tm_hour, 2U);
        log_append_text(line, length, ":");
        log_append_unsigned(line, length, (uint32_t)utc_time.tm_min, 2U);
        log_append_text(line, length, ":");
        log_append_unsigned(line, length, (uint32_t)utc_time.tm_sec, 2U);
        log_append_text(line, length, "Z");
    }
    else
    {
        log_append_text(line, length, "0000-00-00T00:00:00Z");
    }
}

/*
 * Format one entry as "<timestamp> [<LEVEL>] [<module>] <message>
 * [(code N)]" and hand it to the output.
 * Design Document, Chapter 6, Section 6.3, Requirements 5.1 and 5.2.
 */
static bool log_write_entry(const log_entry_t *entry)
{
    static const char *const level_texts[] = {"INFO", "WARN", "ERROR"};
    char line[LOG_LINE_CAPACITY];
    size_t length = 0U;

    log_append_timestamp(line, &length, entry);
    log_append_text(line, &length, " [");
    log_append_text(line, &length, level_texts[entry->level]);
    log_append_text(line, &length, "] [");
    log_append_text(line, &length, entry->module);
    log_append_text(line, &length, "] ");
    log_append_text(line, &length, entry->message);
    if (entry->has_code)
    {
        /* Two's complement magnitude also covers INT32_MIN. */
        uint32_t magnitude = (entry->code < 0) ? (0U - (uint32_t)entry->code)
                                               : (uint32_t)entry->code;

        log_append_text(line, &length, (entry->code < 0) ? " (code -" : " (code ");
        log_append_unsigned(line, &length, magnitude, 1U);
        log_append_text(line, &length, ")");
    }
    if (length >= LOG_LINE_CAPACITY)
    {
        length = LOG_LINE_CAPACITY - 1U;
    }
    line[length] = '\n';
    length++;

    return log_output.write(log_output.context, line, length);
}

/*
 * Print one warning with the number of messages dropped since the last
 * report. Design Document, Chapter 6, Section 6.3, Requirement 5.4.
 */
static bool log_report_drops(uint32_t *reported_count)
{
    uint32_t total = (uint32_t)atomic_load(&log_dropped_count);
    uint32_t new_drops = total - *reported_count;
    bool success = true;

    if (new_drops != 0U)
    {
        log_entry_t entry;
        int32_t code = (new_drops > (uint32_t)INT32_MAX) ? INT32_MAX : (int32_t)new_drops;

        log_build_entry(&entry, LOG_LEVEL_WARN, LOG_OWN_MODULE, LOG_DROPPED_MESSAGE, true,
                        code);
        success = log_write_entry(&entry);
        *reported_count = total;
    }

    return success;
}

/*
 * Logging thread: lower itself to the lowest priority, then copy entries
 * out of the queue under the mutex and print them after releasing it. If
 * the output fails, logging is switched off and the thread ends; the rest
 * of the gateway continues.
 * Design Document, Chapter 2, Section 2.3 (thread priorities, Table 2) and
 * Chapter 6, Section 6.3, Requirement 5.4.
 */
static void *log_thread_main(void *argument)
{
    const struct sched_param idle_parameter = {.sched_priority = 0};
    sigset_t blocked_signals;
    uint32_t reported_drops = 0U;
    bool running = true;

    (void)argument;

    /* If SCHED_IDLE is refused, the thread keeps the default priority. */
    (void)pthread_setschedparam(pthread_self(), SCHED_IDLE, &idle_parameter);

    /*
     * A closed terminal or pipe would raise SIGPIPE and end the process.
     * Blocked here, write() returns EPIPE instead and logging switches off.
     */
    if ((sigemptyset(&blocked_signals) != 0) || (sigaddset(&blocked_signals, SIGPIPE) != 0) ||
        (pthread_sigmask(SIG_BLOCK, &blocked_signals, NULL) != 0))
    {
        running = false;
        atomic_store(&log_active, false);
    }

    while (running)
    {
        log_entry_t entry;
        bool have_entry = false;

        if (pthread_mutex_lock(&log_mutex) != 0)
        {
            atomic_store(&log_active, false);
            break;
        }
        while ((log_queue_count == 0U) && (!log_stop_requested))
        {
            if (pthread_cond_wait(&log_condition, &log_mutex) != 0)
            {
                log_stop_requested = true;
            }
        }
        if (log_queue_count > 0U)
        {
            entry = log_queue[log_queue_head];
            log_queue_head = (log_queue_head + 1U) % LOG_QUEUE_CAPACITY;
            log_queue_count--;
            have_entry = true;
        }
        (void)pthread_mutex_unlock(&log_mutex);

        /* The mutex is released: write() may take as long as it needs. */
        if (have_entry)
        {
            running = log_write_entry(&entry) && log_report_drops(&reported_drops);
            if (!running)
            {
                atomic_store(&log_active, false);
            }
        }
        else
        {
            running = false;
            (void)log_report_drops(&reported_drops);
        }
    }

    return NULL;
}

/*
 * Reset the queue, put the start-up message with the software version in
 * as the first entry, and start the logging thread. The entry is written
 * while no logging thread exists and before callers are accepted, so it
 * cannot collide with anything and is always the first line printed. The
 * version is a compile-time constant, so the digit filter is not applied
 * to it. pthread_create() makes the entry visible to the new thread.
 * Design Document, Chapter 2, Section 2.2 (pthread_create) and Chapter 6,
 * Section 6.3, Requirements 5.1, 5.3 and 5.4.
 */
log_result_t log_init(const log_output_t *output)
{
    if (log_thread_started)
    {
        return LOG_RESULT_ALREADY_ACTIVE;
    }

    if ((output != NULL) && (output->write != NULL))
    {
        log_output = *output;
    }
    else
    {
        log_output.write = log_write_terminal;
        log_output.context = NULL;
    }

    log_queue_head = 0U;
    log_build_entry(&log_queue[0], LOG_LEVEL_INFO, "main", LOG_STARTUP_MESSAGE, false, 0);
    log_queue_count = 1U;
    log_stop_requested = false;
    atomic_store(&log_dropped_count, 0U);
    atomic_store(&log_active, true);

    if (pthread_create(&log_thread, NULL, log_thread_main, NULL) != 0)
    {
        atomic_store(&log_active, false);
        return LOG_RESULT_THREAD_ERROR;
    }
    log_thread_started = true;

    return LOG_RESULT_OK;
}

/*
 * Queue an info message. Design Document, Chapter 6, Section 6.3,
 * Requirements 5.2 and 5.4.
 */
void log_info(const char *module, const char *message)
{
    log_submit(LOG_LEVEL_INFO, module, message, false, 0);
}

/*
 * Queue a warning. Design Document, Chapter 6, Section 6.3,
 * Requirements 5.2 and 5.4.
 */
void log_warn(const char *module, const char *message)
{
    log_submit(LOG_LEVEL_WARN, module, message, false, 0);
}

/*
 * Queue an error message. Design Document, Chapter 6, Section 6.3,
 * Requirements 5.2 and 5.4.
 */
void log_error(const char *module, const char *message)
{
    log_submit(LOG_LEVEL_ERROR, module, message, false, 0);
}

/*
 * Queue an error message with a status/error code (no health data).
 * Design Document, Chapter 6, Section 6.2, Requirement 4.4 and
 * Section 6.3, Requirement 5.2.
 */
void log_error_code(const char *module, const char *message, int32_t code)
{
    log_submit(LOG_LEVEL_ERROR, module, message, true, code);
}

/*
 * Report how many messages were dropped (queue full or busy).
 * Design Document, Chapter 6, Section 6.3, Requirement 5.4.
 */
uint32_t log_get_dropped_count(void)
{
    return (uint32_t)atomic_load(&log_dropped_count);
}

/*
 * Stop accepting messages, let the thread print what is queued, and join
 * it. Called once at program end, so it may wait for the thread.
 * Design Document, Chapter 2, Section 2.1 (System start and logging).
 */
void log_shutdown(void)
{
    if (!log_thread_started)
    {
        return;
    }

    if (pthread_mutex_lock(&log_mutex) == 0)
    {
        log_stop_requested = true;
        (void)pthread_cond_broadcast(&log_condition);
        (void)pthread_mutex_unlock(&log_mutex);
    }
    else
    {
        atomic_store(&log_active, false);
    }

    (void)pthread_join(log_thread, NULL);
    atomic_store(&log_active, false);
    log_thread_started = false;
}
