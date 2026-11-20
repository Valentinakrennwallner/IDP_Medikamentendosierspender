#include "../src/ble/message_dispatch.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct
{
    unsigned int results;
    unsigned int unassigned;
    unsigned int faults;
    unsigned int day_ends;
    report_status_t last_status;
    uint8_t last_intake_index;
    report_fault_state_t last_fault_state;
} dispatch_test_context_t;

/* Record a reported result instead of sending a Telegram warning. */
static void test_on_result(void *context, report_status_t status,
                           uint8_t intake_index)
{
    dispatch_test_context_t *test_context = context;
    test_context->results++;
    test_context->last_status = status;
    test_context->last_intake_index = intake_index;
}

/* Record a result that could not be assigned to today's schedule. */
static void test_on_unassigned(void *context, report_status_t status)
{
    dispatch_test_context_t *test_context = context;
    test_context->unassigned++;
    test_context->last_status = status;
}

/* Record a reported fault start or end. */
static void test_on_fault(void *context, report_status_t status,
                          report_fault_state_t fault_state)
{
    dispatch_test_context_t *test_context = context;
    test_context->faults++;
    test_context->last_status = status;
    test_context->last_fault_state = fault_state;
}

/* Record a reported end of day. */
static void test_on_day_end(void *context)
{
    dispatch_test_context_t *test_context = context;
    test_context->day_ends++;
}

/* Total number of callbacks invoked so far. */
static unsigned int callback_count(const dispatch_test_context_t *context)
{
    return context->results + context->unassigned + context->faults +
           context->day_ends;
}

/* Initialize a dispatcher bound to a zeroed test context. */
static int setup(message_dispatch_t *dispatch,
                 dispatch_test_context_t *context)
{
    message_dispatch_callbacks_t callbacks = {
        test_on_result, test_on_unassigned, test_on_fault, test_on_day_end,
        NULL};
    dispatch_test_context_t empty = {0U, 0U, 0U, 0U, REPORT_STATUS_TAKEN, 0U,
                                     REPORT_FAULT_CLEARED};

    *context = empty;
    callbacks.context = context;
    return message_dispatch_init(dispatch, &callbacks);
}

/* Send a fake RESULT payload through the central adapter. */
static int send_result(message_dispatch_t *dispatch, uint8_t sequence,
                       report_status_t status, uint8_t intake_index)
{
    uint8_t payload[REPORT_MESSAGE_SIZE];
    size_t payload_size = 0U;

    if (report_message_encode_result(sequence, status, intake_index, payload,
                                     sizeof(payload), &payload_size) != 0)
    {
        return -2;
    }
    return message_dispatch_on_message(dispatch, SECURE_CHANNEL_MESSAGE_RESULT,
                                       payload, payload_size);
}

/* Send a fake FAULT payload through the central adapter. */
static int send_fault(message_dispatch_t *dispatch, uint8_t sequence,
                      report_status_t status, report_fault_state_t state)
{
    uint8_t payload[REPORT_MESSAGE_SIZE];
    size_t payload_size = 0U;

    if (report_message_encode_fault(sequence, status, state, payload,
                                    sizeof(payload), &payload_size) != 0)
    {
        return -2;
    }
    return message_dispatch_on_message(dispatch, SECURE_CHANNEL_MESSAGE_FAULT,
                                       payload, payload_size);
}

/* Init requires all four callbacks; entry count is limited to 16. */
static int test_init_and_entry_count(void)
{
    message_dispatch_t dispatch;
    dispatch_test_context_t context;
    message_dispatch_callbacks_t missing = {test_on_result, NULL,
                                            test_on_fault, test_on_day_end,
                                            NULL};

    if ((message_dispatch_init(&dispatch, &missing) == 0) ||
        (message_dispatch_init(NULL, &missing) == 0) ||
        (setup(&dispatch, &context) != 0) ||
        (message_dispatch_set_entry_count(&dispatch, 0U) != 0) ||
        (message_dispatch_set_entry_count(&dispatch, 16U) != 0) ||
        (message_dispatch_set_entry_count(&dispatch, 17U) == 0) ||
        (message_dispatch_set_entry_count(NULL, 1U) == 0) ||
        (message_dispatch_on_message(NULL, SECURE_CHANNEL_MESSAGE_DAY_END,
                                     NULL, 0U) == 0))
    {
        return 1;
    }
    return 0;
}

/* Requirements 1.6 and 2.13: results within today's schedule. */
static int test_result_in_schedule(void)
{
    message_dispatch_t dispatch;
    dispatch_test_context_t context;

    if ((setup(&dispatch, &context) != 0) ||
        (message_dispatch_set_entry_count(&dispatch, 3U) != 0) ||
        (send_result(&dispatch, 0U, REPORT_STATUS_TAKEN, 0U) != 0) ||
        (context.results != 1U) || (context.last_intake_index != 0U) ||
        (send_result(&dispatch, 1U, REPORT_STATUS_MISSED, 2U) != 0) ||
        (context.results != 2U) ||
        (context.last_status != REPORT_STATUS_MISSED) ||
        (context.last_intake_index != 2U) ||
        (send_result(&dispatch, 2U, REPORT_STATUS_UNSCHEDULED_REMOVAL,
                     (uint8_t)REPORT_MESSAGE_NO_INTAKE) != 0) ||
        (context.results != 3U) ||
        (context.last_intake_index != (uint8_t)REPORT_MESSAGE_NO_INTAKE) ||
        (context.unassigned != 0U))
    {
        return 1;
    }
    return 0;
}

/* Index >= entry count or no schedule yet: received, reported unassigned. */
static int test_result_unassigned(void)
{
    message_dispatch_t dispatch;
    dispatch_test_context_t context;

    if ((setup(&dispatch, &context) != 0) ||
        /* No entry count set yet: every RESULT is unassigned. */
        (send_result(&dispatch, 5U, REPORT_STATUS_MISSED, 0U) != 0) ||
        (context.unassigned != 1U) ||
        (send_result(&dispatch, 6U, REPORT_STATUS_UNSCHEDULED_REMOVAL,
                     (uint8_t)REPORT_MESSAGE_NO_INTAKE) != 0) ||
        (context.unassigned != 2U) ||
        (message_dispatch_set_entry_count(&dispatch, 2U) != 0) ||
        (send_result(&dispatch, 7U, REPORT_STATUS_MISSED, 2U) != 0) ||
        (context.unassigned != 3U) ||
        (context.last_status != REPORT_STATUS_MISSED) ||
        (send_result(&dispatch, 8U, REPORT_STATUS_TAKEN, 15U) != 0) ||
        (context.unassigned != 4U) || (context.results != 0U) ||
        /* The unassigned reports updated last: resending one is dropped. */
        (send_result(&dispatch, 8U, REPORT_STATUS_TAKEN, 15U) != 0) ||
        (context.unassigned != 4U) ||
        (send_result(&dispatch, 9U, REPORT_STATUS_TAKEN, 1U) != 0) ||
        (context.results != 1U))
    {
        return 1;
    }
    return 0;
}

/* Requirements 2.6 and 2.10: fault start and end. */
static int test_fault(void)
{
    message_dispatch_t dispatch;
    dispatch_test_context_t context;

    if ((setup(&dispatch, &context) != 0) ||
        (send_fault(&dispatch, 0U, REPORT_STATUS_SENSOR_FAULT,
                    REPORT_FAULT_ACTIVE) != 0) ||
        (context.faults != 1U) ||
        (context.last_status != REPORT_STATUS_SENSOR_FAULT) ||
        (context.last_fault_state != REPORT_FAULT_ACTIVE) ||
        (send_fault(&dispatch, 1U, REPORT_STATUS_SENSOR_FAULT,
                    REPORT_FAULT_CLEARED) != 0) ||
        (context.faults != 2U) ||
        (context.last_fault_state != REPORT_FAULT_CLEARED) ||
        (context.results != 0U) || (context.unassigned != 0U))
    {
        return 1;
    }
    return 0;
}

/* Requirement 1.7: empty day-end message. */
static int test_day_end(void)
{
    message_dispatch_t dispatch;
    dispatch_test_context_t context;
    const uint8_t extra = 0U;

    if ((setup(&dispatch, &context) != 0) ||
        (message_dispatch_on_message(&dispatch, SECURE_CHANNEL_MESSAGE_DAY_END,
                                     NULL, 0U) != 0) ||
        (context.day_ends != 1U) ||
        (message_dispatch_on_message(&dispatch, SECURE_CHANNEL_MESSAGE_DAY_END,
                                     &extra, 1U) == 0) ||
        (context.day_ends != 1U))
    {
        return 1;
    }
    return 0;
}

/* Day-end clears the entry count but keeps the report sequence state. */
static int test_day_end_resets_entry_count(void)
{
    message_dispatch_t dispatch;
    dispatch_test_context_t context;
    const uint8_t extra = 0U;

    if ((setup(&dispatch, &context) != 0) ||
        (message_dispatch_set_entry_count(&dispatch, 3U) != 0) ||
        (send_result(&dispatch, 10U, REPORT_STATUS_TAKEN, 1U) != 0) ||
        (context.results != 1U) ||
        /* A rejected (non-empty) day-end changes nothing. */
        (message_dispatch_on_message(&dispatch, SECURE_CHANNEL_MESSAGE_DAY_END,
                                     &extra, 1U) == 0) ||
        (send_result(&dispatch, 11U, REPORT_STATUS_TAKEN, 2U) != 0) ||
        (context.results != 2U) ||
        (message_dispatch_on_message(&dispatch, SECURE_CHANNEL_MESSAGE_DAY_END,
                                     NULL, 0U) != 0) ||
        (context.day_ends != 1U) ||
        /* After day-end every RESULT is unassigned, even 0xFF. */
        (send_result(&dispatch, 12U, REPORT_STATUS_MISSED, 2U) != 0) ||
        (send_result(&dispatch, 13U, REPORT_STATUS_UNSCHEDULED_REMOVAL,
                     (uint8_t)REPORT_MESSAGE_NO_INTAKE) != 0) ||
        (context.unassigned != 2U) || (context.results != 2U) ||
        /* Sequence state survived day-end: a resent report is dropped. */
        (send_result(&dispatch, 11U, REPORT_STATUS_TAKEN, 2U) != 0) ||
        (callback_count(&context) != 5U) ||
        /* The next schedule makes results assignable again. */
        (message_dispatch_set_entry_count(&dispatch, 2U) != 0) ||
        (send_result(&dispatch, 14U, REPORT_STATUS_TAKEN, 1U) != 0) ||
        (context.results != 3U))
    {
        return 1;
    }
    return 0;
}

/* Malformed payloads and unknown types are rejected without callbacks. */
static int test_malformed_and_unknown(void)
{
    message_dispatch_t dispatch;
    dispatch_test_context_t context;
    uint8_t payload[REPORT_MESSAGE_SIZE + 1U];
    size_t payload_size = 0U;

    if ((setup(&dispatch, &context) != 0) ||
        (message_dispatch_set_entry_count(&dispatch, 4U) != 0) ||
        (report_message_encode_result(3U, REPORT_STATUS_TAKEN, 1U, payload,
                                      sizeof(payload), &payload_size) != 0))
    {
        return 1;
    }

    payload[REPORT_MESSAGE_SIZE] = 0U;
    if ((message_dispatch_on_message(&dispatch, SECURE_CHANNEL_MESSAGE_RESULT,
                                     payload, payload_size + 1U) == 0) ||
        (message_dispatch_on_message(&dispatch, SECURE_CHANNEL_MESSAGE_RESULT,
                                     payload, payload_size - 1U) == 0) ||
        (message_dispatch_on_message(&dispatch, SECURE_CHANNEL_MESSAGE_RESULT,
                                     NULL, 0U) == 0) ||
        /* RESULT payload inside a FAULT header: type byte mismatch. */
        (message_dispatch_on_message(&dispatch, SECURE_CHANNEL_MESSAGE_FAULT,
                                     payload, payload_size) == 0) ||
        (message_dispatch_on_message(&dispatch, SECURE_CHANNEL_MESSAGE_STARTUP,
                                     payload, payload_size) == 0) ||
        (message_dispatch_on_message(&dispatch,
                                     SECURE_CHANNEL_MESSAGE_HEARTBEAT, NULL,
                                     0U) == 0) ||
        (message_dispatch_on_message(&dispatch,
                                     SECURE_CHANNEL_MESSAGE_ACKNOWLEDGEMENT,
                                     NULL, 0U) == 0) ||
        (callback_count(&context) != 0U))
    {
        return 1;
    }

    /* Rejected payloads did not count as accepted: seq 3 is still new. */
    payload[3] = 0U; /* invalid status 0 */
    if ((message_dispatch_on_message(&dispatch, SECURE_CHANNEL_MESSAGE_RESULT,
                                     payload, payload_size) == 0) ||
        (send_result(&dispatch, 3U, REPORT_STATUS_TAKEN, 1U) != 0) ||
        (context.results != 1U))
    {
        return 1;
    }
    return 0;
}

/* Report sequence rule from protocol/SECURE_CHANNEL.md. */
static int test_sequence_rule(void)
{
    message_dispatch_t dispatch;
    dispatch_test_context_t context;

    if ((setup(&dispatch, &context) != 0) ||
        /* First report after a gateway start is accepted, even seq 200. */
        (send_fault(&dispatch, 200U, REPORT_STATUS_NO_TIME,
                    REPORT_FAULT_ACTIVE) != 0) ||
        (context.faults != 1U) ||
        /* Duplicate and older numbers are dropped without a callback. */
        (send_fault(&dispatch, 200U, REPORT_STATUS_NO_TIME,
                    REPORT_FAULT_ACTIVE) != 0) ||
        (send_fault(&dispatch, 150U, REPORT_STATUS_NO_TIME,
                    REPORT_FAULT_ACTIVE) != 0) ||
        (context.faults != 1U) ||
        /* d = 127 (200 -> 72 wrapping over 255 -> 1) is accepted. */
        (send_fault(&dispatch, 72U, REPORT_STATUS_NO_TIME,
                    REPORT_FAULT_CLEARED) != 0) ||
        (context.faults != 2U) ||
        /* d = 128 from 72 (seq 200) is a duplicate. */
        (send_fault(&dispatch, 200U, REPORT_STATUS_NO_TIME,
                    REPORT_FAULT_ACTIVE) != 0) ||
        (context.faults != 2U) ||
        /* Seq 0 is always accepted and resets tracking (ESP32 restart). */
        (send_fault(&dispatch, 0U, REPORT_STATUS_FILLING_ERROR,
                    REPORT_FAULT_ACTIVE) != 0) ||
        (send_fault(&dispatch, 0U, REPORT_STATUS_FILLING_ERROR,
                    REPORT_FAULT_ACTIVE) != 0) ||
        (context.faults != 4U) ||
        /* After 0: d = seq, so 128 is dropped and 127 accepted. */
        (send_fault(&dispatch, 128U, REPORT_STATUS_FILLING_ERROR,
                    REPORT_FAULT_CLEARED) != 0) ||
        (context.faults != 4U) ||
        (send_fault(&dispatch, 127U, REPORT_STATUS_FILLING_ERROR,
                    REPORT_FAULT_CLEARED) != 0) ||
        (context.faults != 5U))
    {
        return 1;
    }

    /* Wrap from 255 to 1 (never back to 0). */
    if ((send_fault(&dispatch, 0U, REPORT_STATUS_SENSOR_FAULT,
                    REPORT_FAULT_ACTIVE) != 0) ||
        (send_fault(&dispatch, 100U, REPORT_STATUS_SENSOR_FAULT,
                    REPORT_FAULT_ACTIVE) != 0) ||
        (send_fault(&dispatch, 200U, REPORT_STATUS_SENSOR_FAULT,
                    REPORT_FAULT_ACTIVE) != 0) ||
        (send_fault(&dispatch, 255U, REPORT_STATUS_SENSOR_FAULT,
                    REPORT_FAULT_ACTIVE) != 0) ||
        (send_fault(&dispatch, 1U, REPORT_STATUS_SENSOR_FAULT,
                    REPORT_FAULT_CLEARED) != 0) ||
        (context.faults != 10U) ||
        (send_fault(&dispatch, 255U, REPORT_STATUS_SENSOR_FAULT,
                    REPORT_FAULT_ACTIVE) != 0) ||
        (context.faults != 10U))
    {
        return 1;
    }
    return 0;
}

/* Run the gateway message dispatch tests. */
int main(void)
{
    int failures = 0;

    failures += test_init_and_entry_count();
    failures += test_result_in_schedule();
    failures += test_result_unassigned();
    failures += test_fault();
    failures += test_day_end();
    failures += test_day_end_resets_entry_count();
    failures += test_malformed_and_unknown();
    failures += test_sequence_rule();

    if (failures != 0)
    {
        (void)fprintf(stderr, "%d message dispatch test(s) failed\n",
                      failures);
        return EXIT_FAILURE;
    }
    (void)puts("message dispatch tests passed");
    return EXIT_SUCCESS;
}
