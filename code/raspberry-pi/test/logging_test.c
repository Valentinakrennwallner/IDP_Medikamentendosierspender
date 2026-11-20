/* pthread, condition variables and clock_gettime() for the fake output. */
#define _POSIX_C_SOURCE 200809L

#include "../src/logging/logging.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TEST_MAX_LINES 64U
#define TEST_LINE_CAPACITY 256U
#define TEST_TIMESTAMP_LENGTH 20U
#define TEST_WAIT_SECONDS 5
#define TEST_STARTUP_CYCLES 20U
#define TEST_PRODUCER_THREADS 4U
#define TEST_MESSAGES_PER_PRODUCER 50U
#define TEST_STARTUP_LINE "[INFO] [main] Gateway started, version " GATEWAY_SOFTWARE_VERSION "\n"

typedef enum
{
    FAKE_MODE_RECORD = 0,     /* record every line */
    FAKE_MODE_GATED,          /* record; the first write() blocks until released */
    FAKE_MODE_FAIL,           /* every write() fails */
    FAKE_MODE_LOG_FROM_WRITE  /* record; the first write() calls log_info() */
} fake_mode_t;

typedef struct
{
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    fake_mode_t mode;
    bool write_entered;
    bool released;
    bool nested_logged;
    size_t write_calls;
    size_t line_count;
    char lines[TEST_MAX_LINES][TEST_LINE_CAPACITY];
} fake_output_t;

typedef bool (*fake_predicate_fn)(void);

static fake_output_t fake;

/*
 * Fake terminal: records lines and can fail, block, or log from write().
 * The first line is always the start-up message queued by log_init(). In
 * FAKE_MODE_GATED the logging thread is blocked in that first write() and
 * holds no mutex while the test queues its messages, so callers never
 * collide with the thread and the expected output is deterministic.
 */
static bool fake_write(void *context, const char *text, size_t length)
{
    fake_output_t *output = context;
    bool success = true;
    bool log_nested = false;

    (void)pthread_mutex_lock(&output->mutex);
    output->write_calls++;
    if (output->mode == FAKE_MODE_FAIL)
    {
        success = false;
    }
    else
    {
        if ((output->line_count < TEST_MAX_LINES) && (length < TEST_LINE_CAPACITY))
        {
            memcpy(output->lines[output->line_count], text, length);
            output->lines[output->line_count][length] = '\0';
            output->line_count++;
        }
        output->write_entered = true;
        (void)pthread_cond_broadcast(&output->condition);
        if ((output->mode == FAKE_MODE_GATED) && (output->write_calls == 1U))
        {
            /* Ended by release_output(), which every gated test calls. */
            while (!output->released)
            {
                (void)pthread_cond_wait(&output->condition, &output->mutex);
            }
        }
        if ((output->mode == FAKE_MODE_LOG_FROM_WRITE) && !output->nested_logged)
        {
            output->nested_logged = true;
            log_nested = true;
        }
    }
    (void)pthread_mutex_unlock(&output->mutex);

    /* Runs on the logging thread while it is inside write(). */
    if (log_nested)
    {
        log_info("test", "Nested message");
    }

    return success;
}

/* Reset the fake output and start logging with it. */
static int start_fake(fake_mode_t mode)
{
    log_output_t output = {fake_write, &fake};

    (void)pthread_mutex_lock(&fake.mutex);
    fake.mode = mode;
    fake.write_entered = false;
    fake.released = false;
    fake.nested_logged = false;
    fake.write_calls = 0U;
    fake.line_count = 0U;
    (void)pthread_mutex_unlock(&fake.mutex);

    return (log_init(&output) == LOG_RESULT_OK) ? 0 : 1;
}

/* Wait at most TEST_WAIT_SECONDS for a condition on the fake output. */
static bool wait_for(fake_predicate_fn predicate)
{
    struct timespec deadline;
    bool satisfied;
    int wait_result = 0;

    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0)
    {
        return false;
    }
    deadline.tv_sec += TEST_WAIT_SECONDS;

    (void)pthread_mutex_lock(&fake.mutex);
    satisfied = predicate();
    while (!satisfied && (wait_result == 0))
    {
        wait_result = pthread_cond_timedwait(&fake.condition, &fake.mutex, &deadline);
        satisfied = predicate();
    }
    (void)pthread_mutex_unlock(&fake.mutex);

    return satisfied;
}

/* Predicate: the logging thread has entered the first write(). */
static bool first_write_entered(void)
{
    return fake.write_entered;
}

/* Predicate: the nested message has been written as the second line. */
static bool nested_message_written(void)
{
    return fake.line_count >= 2U;
}

/* Let the blocked first write() return. */
static void release_output(void)
{
    (void)pthread_mutex_lock(&fake.mutex);
    fake.released = true;
    (void)pthread_cond_broadcast(&fake.condition);
    (void)pthread_mutex_unlock(&fake.mutex);
}

/* True if a recorded line is "<timestamp> " followed by the expected text. */
static bool line_matches(size_t index, const char *expected_suffix)
{
    const char *line;

    if (index >= fake.line_count)
    {
        return false;
    }
    line = fake.lines[index];
    return (strlen(line) == (TEST_TIMESTAMP_LENGTH + 1U + strlen(expected_suffix))) &&
           (strcmp(&line[TEST_TIMESTAMP_LENGTH + 1U], expected_suffix) == 0);
}

/* True if any recorded line contains the given text. */
static bool any_line_contains(const char *text)
{
    size_t index;

    for (index = 0U; index < fake.line_count; index++)
    {
        if (strstr(fake.lines[index], text) != NULL)
        {
            return true;
        }
    }
    return false;
}

/* Check the UTC timestamp layout YYYY-MM-DDTHH:MM:SSZ followed by a space. */
static bool timestamp_is_valid(const char *line)
{
    static const char layout[] = "dddd-dd-ddTdd:dd:ddZ ";
    size_t index;

    for (index = 0U; layout[index] != '\0'; index++)
    {
        bool digit = (line[index] >= '0') && (line[index] <= '9');

        if ((layout[index] == 'd') ? !digit : (line[index] != layout[index]))
        {
            return false;
        }
    }
    return true;
}

/* Requirements 5.2 and 5.3: timestamp, level, module, code and version. */
static int test_format_and_startup(void)
{
    size_t index;
    bool gated;

    if (start_fake(FAKE_MODE_GATED) != 0)
    {
        return 1;
    }
    gated = wait_for(first_write_entered);
    log_info("ble", "Connected");
    log_warn("time_sync", "Sync failed");
    log_error("telegram", "Send failed");
    log_error_code("telegram", "Send failed", -5);
    log_error_code("ble", "Write failed", INT32_MIN);
    release_output();
    log_shutdown();

    if (!gated || (fake.line_count != 6U) || !line_matches(0U, TEST_STARTUP_LINE) ||
        !line_matches(1U, "[INFO] [ble] Connected\n") ||
        !line_matches(2U, "[WARN] [time_sync] Sync failed\n") ||
        !line_matches(3U, "[ERROR] [telegram] Send failed\n") ||
        !line_matches(4U, "[ERROR] [telegram] Send failed (code -5)\n") ||
        !line_matches(5U, "[ERROR] [ble] Write failed (code -2147483648)\n"))
    {
        return 1;
    }
    for (index = 0U; index < fake.line_count; index++)
    {
        if (!timestamp_is_valid(fake.lines[index]))
        {
            return 1;
        }
    }
    return (strcmp(GATEWAY_SOFTWARE_VERSION, "0.1.0") == 0) ? 0 : 1;
}

/* Requirement 4.4: digits, '%', control characters and bad names are rejected. */
static int test_health_data_rejected(void)
{
    static const char *const expected[] = {
        "[ERROR] [dosing] Log message rejected\n",  "[ERROR] [dosing] Log message rejected\n",
        "[ERROR] [weight] Log message rejected\n",  "[ERROR] [weight] Log message rejected\n",
        "[ERROR] [ble] Log message rejected\n",     "[ERROR] [ble] Log message rejected\n",
        "[ERROR] [ble] Log message rejected\n",     "[ERROR] [ble] Log message rejected\n",
        "[ERROR] [logging] Log message rejected\n", "[INFO] [ble_central_two] Connected\n",
        "[ERROR] [logging] Log message rejected\n", "[ERROR] [logging] Log message rejected\n",
        "[ERROR] [logging] Log message rejected\n", "[ERROR] [logging] Log message rejected\n"};
    char longest[LOG_MESSAGE_MAX_LENGTH + 1U];
    char too_long[LOG_MESSAGE_MAX_LENGTH + 2U];
    size_t index;
    bool gated;

    memset(longest, 'a', LOG_MESSAGE_MAX_LENGTH);
    longest[LOG_MESSAGE_MAX_LENGTH] = '\0';
    memset(too_long, 'a', LOG_MESSAGE_MAX_LENGTH + 1U);
    too_long[LOG_MESSAGE_MAX_LENGTH + 1U] = '\0';

    if (start_fake(FAKE_MODE_GATED) != 0)
    {
        return 1;
    }
    gated = wait_for(first_write_entered);
    log_info("dosing", "Intake at 08:00");     /* intake time */
    log_warn("dosing", "Took 2 tablets");      /* dose */
    log_error("weight", "Weight 12.5 g");      /* weight */
    log_info("weight", "Weight %f");           /* format string */
    log_info("ble", "Bad\x01text");            /* control character */
    log_info("ble", "");                       /* empty */
    log_info("ble", NULL);                     /* missing */
    log_info("ble", too_long);                 /* 121 characters */
    log_info("BLE", "Connected");              /* upper-case module */
    log_info("ble_central_two", "Connected");  /* 15 characters: accepted */
    log_info("ble_central_long", "Connected"); /* 16 characters */
    log_info("ble1", "Connected");             /* digit in module */
    log_info("", "Connected");                 /* empty module */
    log_info(NULL, "Connected");               /* missing module */
    log_info("ble", longest);                  /* 120 characters: accepted */
    release_output();
    log_shutdown();

    if (!gated || (fake.line_count != 16U) || !line_matches(0U, TEST_STARTUP_LINE))
    {
        return 1;
    }
    for (index = 0U; index < (sizeof(expected) / sizeof(expected[0])); index++)
    {
        if (!line_matches(index + 1U, expected[index]))
        {
            return 1;
        }
    }
    return ((strstr(fake.lines[15], longest) != NULL) && !any_line_contains("08:00") &&
            !any_line_contains("12.5"))
               ? 0
               : 1;
}

/*
 * Requirement 5.4: while write() is blocked (slow terminal), callers can
 * still fill the whole queue, so the mutex is not held during write();
 * only a full queue drops messages, and the drops are reported.
 */
static int test_slow_output_does_not_block_callers(void)
{
    size_t index;
    uint32_t dropped_while_blocked;
    uint32_t dropped_when_full;
    bool gated;

    if (start_fake(FAKE_MODE_GATED) != 0)
    {
        return 1;
    }
    /* The thread has taken the start-up entry and is blocked in write(). */
    gated = wait_for(first_write_entered);
    for (index = 0U; index < LOG_QUEUE_CAPACITY; index++)
    {
        log_info("ble", "Queued");
    }
    dropped_while_blocked = log_get_dropped_count();
    log_info("ble", "Overflow");
    dropped_when_full = log_get_dropped_count();
    release_output();
    log_shutdown();

    return (gated && (dropped_while_blocked == 0U) && (dropped_when_full == 1U) &&
            (fake.line_count == (LOG_QUEUE_CAPACITY + 2U)) &&
            any_line_contains("[WARN] [logging] Log messages dropped (code 1)") &&
            !any_line_contains("Overflow"))
               ? 0
               : 1;
}

/*
 * Requirement 5.4: a message logged while the logging thread is inside
 * write() is accepted. A held (non-recursive) mutex would make every
 * trylock attempt fail and drop it.
 */
static int test_mutex_released_during_write(void)
{
    bool written;

    if (start_fake(FAKE_MODE_LOG_FROM_WRITE) != 0)
    {
        return 1;
    }
    written = wait_for(nested_message_written);
    log_shutdown();

    return (written && (log_get_dropped_count() == 0U) && (fake.line_count == 2U) &&
            line_matches(0U, TEST_STARTUP_LINE) &&
            line_matches(1U, "[INFO] [test] Nested message\n"))
               ? 0
               : 1;
}

/* Requirement 5.4: after an output failure, logging stops and callers continue. */
static int test_output_failure_disables_logging(void)
{
    size_t write_calls;

    if (start_fake(FAKE_MODE_FAIL) != 0)
    {
        return 1;
    }
    log_info("ble", "First");
    log_info("ble", "Second");
    log_error_code("ble", "Third", 1);
    log_shutdown();
    write_calls = fake.write_calls;

    log_info("ble", "After shutdown");

    /* Only the start-up line was attempted; nothing after the failure. */
    return ((write_calls == 1U) && (fake.write_calls == 1U)) ? 0 : 1;
}

/* Producer thread for the start-up ordering test. */
static void *log_burst(void *argument)
{
    size_t index;

    (void)argument;
    for (index = 0U; index < TEST_MESSAGES_PER_PRODUCER; index++)
    {
        log_info("ble", "Burst");
    }
    return NULL;
}

/*
 * Requirement 5.3: the start-up message is always the first line, even when
 * other threads log at the moment log_init() returns.
 */
static int test_startup_message_always_first(void)
{
    size_t cycle;

    for (cycle = 0U; cycle < TEST_STARTUP_CYCLES; cycle++)
    {
        pthread_t producers[TEST_PRODUCER_THREADS];
        size_t started = 0U;
        size_t index;

        if (start_fake(FAKE_MODE_RECORD) != 0)
        {
            return 1;
        }
        for (index = 0U; index < TEST_PRODUCER_THREADS; index++)
        {
            if (pthread_create(&producers[index], NULL, log_burst, NULL) == 0)
            {
                started++;
            }
        }
        log_info("main", "Burst");
        for (index = 0U; index < started; index++)
        {
            (void)pthread_join(producers[index], NULL);
        }
        log_shutdown();

        if ((started != TEST_PRODUCER_THREADS) || !line_matches(0U, TEST_STARTUP_LINE))
        {
            return 1;
        }
    }
    return 0;
}

/* Calls before log_init() are no-ops; a second log_init() is refused. */
static int test_lifecycle(void)
{
    log_output_t output = {fake_write, &fake};
    int result = 0;

    log_info("ble", "Before init");
    log_shutdown();
    if (start_fake(FAKE_MODE_RECORD) != 0)
    {
        return 1;
    }
    if (log_init(&output) != LOG_RESULT_ALREADY_ACTIVE)
    {
        result = 1;
    }
    log_shutdown();

    return ((result == 0) && (fake.line_count == 1U) && line_matches(0U, TEST_STARTUP_LINE))
               ? 0
               : 1;
}

/* Run the gateway logging tests with a fake terminal. */
int main(void)
{
    (void)pthread_mutex_init(&fake.mutex, NULL);
    (void)pthread_cond_init(&fake.condition, NULL);

    if ((test_format_and_startup() != 0) || (test_health_data_rejected() != 0) ||
        (test_slow_output_does_not_block_callers() != 0) ||
        (test_mutex_released_during_write() != 0) ||
        (test_output_failure_disables_logging() != 0) ||
        (test_startup_message_always_first() != 0) || (test_lifecycle() != 0))
    {
        (void)fputs("logging tests failed\n", stderr);
        return EXIT_FAILURE;
    }

    (void)puts("logging tests passed");
    return EXIT_SUCCESS;
}
