#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "report_buffer.h"
#include "secure_channel.h"

/*
 * Host tests for the report ring buffer (components/report_buffer). A fake
 * transport records every report handed to it and can be made to fail, so
 * ordering, exactly-once delivery, resending and overflow can be checked.
 * Every recorded RESULT/FAULT is decoded again with the shared protocol
 * decoder. Design Document, Chapter 6, Section 6.1, Requirements 1.6, 1.7,
 * 2.6, 2.13 and 3.2.
 */

#define MAX_RECORDED 600U

typedef struct
{
    uint8_t message_type;
    uint8_t payload[REPORT_MESSAGE_SIZE];
    size_t length;
} recorded_t;

typedef struct
{
    recorded_t records[MAX_RECORDED];
    uint32_t count;
    /* Number of further sends that succeed; afterwards every send fails. */
    uint32_t successes_left;
    uint32_t attempts;
} fake_transport_t;

/* Report a failed check with its line number and return 1. */
static int fail(int line)
{
    (void)fprintf(stderr, "report_buffer_test: check failed at line %d\n", line);
    return 1;
}

#define CHECK(condition)           \
    do                             \
    {                              \
        if (!(condition))          \
        {                          \
            return fail(__LINE__); \
        }                          \
    } while (0)

/* Fake transport: record the report if a success is left, otherwise fail. */
static bool fake_send(void *context, uint8_t message_type, const uint8_t *payload, size_t length)
{
    fake_transport_t *transport = (fake_transport_t *)context;

    transport->attempts++;
    if ((transport->successes_left == 0U) || (transport->count >= MAX_RECORDED) ||
        (length > REPORT_MESSAGE_SIZE))
    {
        return false;
    }
    transport->successes_left--;
    transport->records[transport->count].message_type = message_type;
    transport->records[transport->count].length = length;
    if (length > 0U)
    {
        (void)memcpy(transport->records[transport->count].payload, payload, length);
    }
    transport->count++;
    return true;
}

/* Start a fake transport that accepts the given number of reports. */
static void transport_init(fake_transport_t *transport, uint32_t successes)
{
    (void)memset(transport, 0, sizeof(*transport));
    transport->successes_left = successes;
}

/* Decode recorded report number index; returns false if it is malformed. */
static bool decode_record(const fake_transport_t *transport, uint32_t index,
                          report_message_t *report)
{
    const recorded_t *record = &transport->records[index];

    return report_message_decode(record->payload, record->length, record->message_type,
                                 report) == 0;
}

/* The day-end constant matches the secure-channel message type. */
static int test_day_end_type_matches_protocol(void)
{
    CHECK(REPORT_BUFFER_MESSAGE_TYPE_DAY_END == (unsigned int)SECURE_CHANNEL_MESSAGE_DAY_END);
    CHECK(REPORT_BUFFER_CAPACITY == 127U);
    return 0;
}

/*
 * Requirement 3.2: reports queued without a connection are sent after the
 * reconnect in the order they were queued, each exactly once, with the
 * sequence numbers 0, 1, 2, ...
 */
static int test_order_after_reconnect(void)
{
    report_buffer_t buffer;
    fake_transport_t transport;
    report_message_t report;

    report_buffer_init(&buffer);
    CHECK(report_buffer_push_result(&buffer, REPORT_STATUS_TAKEN, 0U) == REPORT_BUFFER_PUSH_OK);
    CHECK(report_buffer_push_fault(&buffer, REPORT_STATUS_SENSOR_FAULT, REPORT_FAULT_ACTIVE) ==
          REPORT_BUFFER_PUSH_OK);
    CHECK(report_buffer_push_fault(&buffer, REPORT_STATUS_SENSOR_FAULT, REPORT_FAULT_CLEARED) ==
          REPORT_BUFFER_PUSH_OK);
    CHECK(report_buffer_push_result(&buffer, REPORT_STATUS_UNSCHEDULED_REMOVAL,
                                    (uint8_t)REPORT_MESSAGE_NO_INTAKE) == REPORT_BUFFER_PUSH_OK);
    CHECK(report_buffer_push_result(&buffer, REPORT_STATUS_MISSED, 2U) == REPORT_BUFFER_PUSH_OK);
    CHECK(report_buffer_push_day_end(&buffer) == REPORT_BUFFER_PUSH_OK);
    CHECK(report_buffer_count(&buffer) == 6U);

    /* No connection: the transport refuses, nothing is lost. */
    transport_init(&transport, 0U);
    CHECK(report_buffer_flush(&buffer, fake_send, &transport) == 0U);
    CHECK(report_buffer_count(&buffer) == 6U);

    /* Reconnect: everything is sent in order, then the buffer is empty. */
    transport_init(&transport, 100U);
    CHECK(report_buffer_flush(&buffer, fake_send, &transport) == 6U);
    CHECK(report_buffer_count(&buffer) == 0U);
    CHECK(transport.count == 6U);

    CHECK(decode_record(&transport, 0U, &report));
    CHECK((report.message_type == REPORT_MESSAGE_TYPE_RESULT) && (report.sequence == 0U) &&
          (report.status == REPORT_STATUS_TAKEN) && (report.intake_index == 0U));
    CHECK(decode_record(&transport, 1U, &report));
    CHECK((report.message_type == REPORT_MESSAGE_TYPE_FAULT) && (report.sequence == 1U) &&
          (report.status == REPORT_STATUS_SENSOR_FAULT) &&
          (report.fault_state == REPORT_FAULT_ACTIVE));
    CHECK(decode_record(&transport, 2U, &report));
    CHECK((report.sequence == 2U) && (report.fault_state == REPORT_FAULT_CLEARED));
    CHECK(decode_record(&transport, 3U, &report));
    CHECK((report.sequence == 3U) && (report.status == REPORT_STATUS_UNSCHEDULED_REMOVAL) &&
          (report.intake_index == REPORT_MESSAGE_NO_INTAKE));
    CHECK(decode_record(&transport, 4U, &report));
    CHECK((report.sequence == 4U) && (report.status == REPORT_STATUS_MISSED) &&
          (report.intake_index == 2U));
    /* DAY_END is empty and uses no sequence number. */
    CHECK(transport.records[5].message_type == REPORT_BUFFER_MESSAGE_TYPE_DAY_END);
    CHECK(transport.records[5].length == 0U);

    /* Nothing is sent twice: a second flush sends nothing. */
    transport_init(&transport, 100U);
    CHECK(report_buffer_flush(&buffer, fake_send, &transport) == 0U);
    CHECK(transport.attempts == 0U);
    return 0;
}

/*
 * A failed send keeps the report with the same sequence number; the next
 * report continues the numbering (no number is skipped or reused).
 */
static int test_failed_send_keeps_number(void)
{
    report_buffer_t buffer;
    fake_transport_t transport;
    report_message_t report;

    report_buffer_init(&buffer);
    CHECK(report_buffer_push_result(&buffer, REPORT_STATUS_NOT_YET_TAKEN, 1U) ==
          REPORT_BUFFER_PUSH_OK);
    CHECK(report_buffer_push_result(&buffer, REPORT_STATUS_TAKEN, 1U) == REPORT_BUFFER_PUSH_OK);

    /* First report goes through, the second fails (connection lost). */
    transport_init(&transport, 1U);
    CHECK(report_buffer_flush(&buffer, fake_send, &transport) == 1U);
    CHECK(transport.attempts == 2U);
    CHECK(report_buffer_count(&buffer) == 1U);

    /* Second failed attempt, then success: still number 1. */
    transport_init(&transport, 0U);
    CHECK(report_buffer_flush(&buffer, fake_send, &transport) == 0U);
    transport_init(&transport, 10U);
    CHECK(report_buffer_flush(&buffer, fake_send, &transport) == 1U);
    CHECK(decode_record(&transport, 0U, &report));
    CHECK((report.sequence == 1U) && (report.status == REPORT_STATUS_TAKEN));

    CHECK(report_buffer_push_result(&buffer, REPORT_STATUS_TAKEN, 2U) == REPORT_BUFFER_PUSH_OK);
    transport_init(&transport, 10U);
    CHECK(report_buffer_flush(&buffer, fake_send, &transport) == 1U);
    CHECK(decode_record(&transport, 0U, &report));
    CHECK(report.sequence == 2U);
    return 0;
}

/* Sequence numbers: 0 first, then 1 ... 255, wrapping from 255 to 1. */
static int test_sequence_wraps_to_one(void)
{
    report_buffer_t buffer;
    fake_transport_t transport;
    report_message_t report;
    uint32_t i;

    report_buffer_init(&buffer);
    transport_init(&transport, MAX_RECORDED);
    for (i = 0U; i < 258U; ++i)
    {
        CHECK(report_buffer_push_result(&buffer, REPORT_STATUS_TAKEN, 0U) ==
              REPORT_BUFFER_PUSH_OK);
        CHECK(report_buffer_flush(&buffer, fake_send, &transport) == 1U);
    }
    CHECK(decode_record(&transport, 0U, &report) && (report.sequence == 0U));
    CHECK(decode_record(&transport, 1U, &report) && (report.sequence == 1U));
    CHECK(decode_record(&transport, 255U, &report) && (report.sequence == 255U));
    CHECK(decode_record(&transport, 256U, &report) && (report.sequence == 1U));
    CHECK(decode_record(&transport, 257U, &report) && (report.sequence == 2U));
    return 0;
}

/*
 * Overflow: with 127 reports waiting, a new one drops the oldest. The
 * remaining reports are sent in order with consecutive numbers starting at
 * 0, because numbers are assigned at the first send attempt.
 */
static int test_overflow_drops_oldest(void)
{
    report_buffer_t buffer;
    fake_transport_t transport;
    report_message_t report;
    uint32_t i;

    report_buffer_init(&buffer);
    for (i = 0U; i < REPORT_BUFFER_CAPACITY; ++i)
    {
        CHECK(report_buffer_push_result(&buffer, REPORT_STATUS_TAKEN, (uint8_t)(i % 16U)) ==
              REPORT_BUFFER_PUSH_OK);
    }
    CHECK(report_buffer_count(&buffer) == REPORT_BUFFER_CAPACITY);
    /* Two more: the two oldest (intake index 0 and 1) are dropped. */
    CHECK(report_buffer_push_result(&buffer, REPORT_STATUS_MISSED, 14U) ==
          REPORT_BUFFER_PUSH_OLDEST_DROPPED);
    CHECK(report_buffer_push_result(&buffer, REPORT_STATUS_MISSED, 15U) ==
          REPORT_BUFFER_PUSH_OLDEST_DROPPED);
    CHECK(report_buffer_count(&buffer) == REPORT_BUFFER_CAPACITY);
    CHECK(buffer.dropped_count == 2U);

    transport_init(&transport, MAX_RECORDED);
    CHECK(report_buffer_flush(&buffer, fake_send, &transport) == REPORT_BUFFER_CAPACITY);
    CHECK(decode_record(&transport, 0U, &report));
    CHECK((report.sequence == 0U) && (report.intake_index == 2U));
    CHECK(decode_record(&transport, REPORT_BUFFER_CAPACITY - 1U, &report));
    CHECK((report.sequence == 126U) && (report.status == REPORT_STATUS_MISSED) &&
          (report.intake_index == 15U));
    return 0;
}

/*
 * Commit removes only the entry that was peeked. If that entry was dropped
 * by an overflow meanwhile (another thread queued reports while sending),
 * the newer oldest entry is kept.
 */
static int test_commit_with_stale_id(void)
{
    report_buffer_t buffer;
    report_buffer_frame_t frame;
    uint32_t i;

    report_buffer_init(&buffer);
    CHECK(!report_buffer_peek(&buffer, &frame));
    for (i = 0U; i < REPORT_BUFFER_CAPACITY; ++i)
    {
        CHECK(report_buffer_push_result(&buffer, REPORT_STATUS_TAKEN, 0U) !=
              REPORT_BUFFER_PUSH_INVALID);
    }
    CHECK(report_buffer_peek(&buffer, &frame));
    CHECK(frame.length == REPORT_MESSAGE_SIZE);
    /* Peeking twice gives the same entry and the same number. */
    {
        report_buffer_frame_t again;

        CHECK(report_buffer_peek(&buffer, &again));
        CHECK((again.id == frame.id) && (memcmp(again.payload, frame.payload, 5U) == 0));
    }
    CHECK(report_buffer_push_day_end(&buffer) == REPORT_BUFFER_PUSH_OLDEST_DROPPED);
    CHECK(!report_buffer_commit(&buffer, frame.id));
    CHECK(report_buffer_count(&buffer) == REPORT_BUFFER_CAPACITY);
    CHECK(report_buffer_peek(&buffer, &frame));
    CHECK(report_buffer_commit(&buffer, frame.id));
    CHECK(report_buffer_count(&buffer) == (REPORT_BUFFER_CAPACITY - 1U));
    return 0;
}

/* Invalid reports are rejected and not queued. */
static int test_invalid_reports(void)
{
    report_buffer_t buffer;

    report_buffer_init(&buffer);
    /* RESULT with a fault status, index out of range, removal with an index. */
    CHECK(report_buffer_push_result(&buffer, REPORT_STATUS_SENSOR_FAULT, 0U) ==
          REPORT_BUFFER_PUSH_INVALID);
    CHECK(report_buffer_push_result(&buffer, REPORT_STATUS_TAKEN, 16U) ==
          REPORT_BUFFER_PUSH_INVALID);
    CHECK(report_buffer_push_result(&buffer, REPORT_STATUS_TAKEN,
                                    (uint8_t)REPORT_MESSAGE_NO_INTAKE) ==
          REPORT_BUFFER_PUSH_INVALID);
    CHECK(report_buffer_push_result(&buffer, REPORT_STATUS_UNSCHEDULED_REMOVAL, 0U) ==
          REPORT_BUFFER_PUSH_INVALID);
    /* FAULT with a result status or an invalid fault state. */
    CHECK(report_buffer_push_fault(&buffer, REPORT_STATUS_MISSED, REPORT_FAULT_ACTIVE) ==
          REPORT_BUFFER_PUSH_INVALID);
    CHECK(report_buffer_push_fault(&buffer, REPORT_STATUS_SENSOR_FAULT,
                                   (report_fault_state_t)2) == REPORT_BUFFER_PUSH_INVALID);
    CHECK(report_buffer_count(&buffer) == 0U);
    /* No sequence number was used: the next valid report still gets 0. */
    CHECK(report_buffer_push_fault(&buffer, REPORT_STATUS_NO_TIME, REPORT_FAULT_ACTIVE) ==
          REPORT_BUFFER_PUSH_OK);
    CHECK(report_buffer_push_fault(&buffer, REPORT_STATUS_FILLING_ERROR, REPORT_FAULT_ACTIVE) ==
          REPORT_BUFFER_PUSH_OK);
    {
        report_buffer_frame_t frame;
        report_message_t report;

        CHECK(report_buffer_peek(&buffer, &frame));
        CHECK(report_message_decode(frame.payload, frame.length, frame.message_type, &report) ==
              0);
        CHECK((report.sequence == 0U) && (report.status == REPORT_STATUS_NO_TIME));
    }
    return 0;
}

/* Missing arguments are rejected without side effects. */
static int test_missing_arguments(void)
{
    report_buffer_t buffer;
    report_buffer_frame_t frame;
    fake_transport_t transport;

    report_buffer_init(NULL);
    report_buffer_init(&buffer);
    CHECK(report_buffer_push_result(NULL, REPORT_STATUS_TAKEN, 0U) == REPORT_BUFFER_PUSH_INVALID);
    CHECK(report_buffer_push_fault(NULL, REPORT_STATUS_SENSOR_FAULT, REPORT_FAULT_ACTIVE) ==
          REPORT_BUFFER_PUSH_INVALID);
    CHECK(report_buffer_push_day_end(NULL) == REPORT_BUFFER_PUSH_INVALID);
    CHECK(report_buffer_push_result(&buffer, REPORT_STATUS_TAKEN, 0U) == REPORT_BUFFER_PUSH_OK);
    CHECK(!report_buffer_peek(NULL, &frame));
    CHECK(!report_buffer_peek(&buffer, NULL));
    CHECK(!report_buffer_commit(NULL, 0U));
    CHECK(report_buffer_count(NULL) == 0U);
    transport_init(&transport, 10U);
    CHECK(report_buffer_flush(&buffer, NULL, &transport) == 0U);
    CHECK(report_buffer_flush(NULL, fake_send, &transport) == 0U);
    CHECK(transport.attempts == 0U);
    CHECK(report_buffer_count(&buffer) == 1U);
    return 0;
}

/* Run all report-buffer tests; exit status 0 only if all pass. */
int main(void)
{
    int failures = 0;

    failures += test_day_end_type_matches_protocol();
    failures += test_order_after_reconnect();
    failures += test_failed_send_keeps_number();
    failures += test_sequence_wraps_to_one();
    failures += test_overflow_drops_oldest();
    failures += test_commit_with_stale_id();
    failures += test_invalid_reports();
    failures += test_missing_arguments();

    if (failures != 0)
    {
        (void)fprintf(stderr, "report_buffer_test: %d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    (void)printf("report_buffer_test: all tests passed\n");
    return EXIT_SUCCESS;
}
