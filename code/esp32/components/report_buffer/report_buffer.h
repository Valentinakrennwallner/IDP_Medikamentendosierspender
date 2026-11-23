#ifndef REPORT_BUFFER_H
#define REPORT_BUFFER_H

/*
 * Ring buffer of the reports the ESP32 sends to the Raspberry Pi: RESULT
 * (type 4), FAULT (type 6) and DAY_END (type 5). Pure logic without
 * hardware dependencies, unit-tested on the host.
 *
 * Every report is queued once and handed to the send function once with
 * success, in the order it was queued. While there is no connection the
 * reports stay in the buffer and are sent in order after the reconnect.
 * The report sequence number (code/protocol/SECURE_CHANNEL.md) is assigned
 * at the first send attempt, not when queuing, and kept for a resend.
 *
 * Design Document, Chapter 3 (ESP32 flowchart), and Chapter 6, Section 6.1,
 * Requirements 1.6, 1.7, 2.6, 2.10, 2.13 and 3.2.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "report_message.h"

/* At most this many unsent reports are held (one day without connection). */
#define REPORT_BUFFER_CAPACITY REPORT_MESSAGE_MAX_PENDING

/* Message type of the empty day-end message (SECURE_CHANNEL_MESSAGE_DAY_END). */
#define REPORT_BUFFER_MESSAGE_TYPE_DAY_END 5U

/* Result of queuing a report. */
typedef enum
{
    /* Queued. */
    REPORT_BUFFER_PUSH_OK = 0,
    /* Queued; the buffer was full and its oldest report was dropped. */
    REPORT_BUFFER_PUSH_OLDEST_DROPPED = 1,
    /* Not queued: invalid status, intake index or fault state, or missing argument. */
    REPORT_BUFFER_PUSH_INVALID = 2
} report_buffer_push_result_t;

typedef struct
{
    /* REPORT_MESSAGE_TYPE_RESULT, REPORT_MESSAGE_TYPE_FAULT or ..._DAY_END. */
    uint8_t message_type;
    report_status_t status;
    /* Intake index (RESULT) or fault state (FAULT); unused for DAY_END. */
    uint8_t detail;
    /* Set at the first send attempt; DAY_END has no sequence number. */
    bool has_sequence;
    uint8_t sequence;
    /* Identifies the entry for report_buffer_commit(). */
    uint32_t id;
} report_buffer_entry_t;

typedef struct
{
    report_buffer_entry_t entries[REPORT_BUFFER_CAPACITY];
    /* Index of the oldest entry. */
    uint32_t head;
    uint32_t count;
    uint32_t next_id;
    /* False until sequence number 0 (first report since start-up) is used. */
    bool first_sequence_used;
    uint8_t last_sequence;
    /* Number of reports dropped because the buffer was full. */
    uint32_t dropped_count;
} report_buffer_t;

/* The oldest report, encoded and ready to be sent. */
typedef struct
{
    uint8_t message_type;
    uint8_t payload[REPORT_MESSAGE_SIZE];
    /* REPORT_MESSAGE_SIZE for RESULT and FAULT, 0 for DAY_END. */
    size_t length;
    uint32_t id;
} report_buffer_frame_t;

/*
 * Send one report to the Raspberry Pi (provided by the BLE transport, tasks
 * 18 and 19). Returns true only if the transport accepted the message; on
 * false the report stays in the buffer and is resent with the same number.
 */
typedef bool (*report_buffer_send_fn)(void *context,
                                      uint8_t message_type,
                                      const uint8_t *payload,
                                      size_t length);

/*
 * Empty the buffer; the next report gets sequence number 0.
 * Design Document, Chapter 6, Section 6.1, Requirement 3.2.
 */
void report_buffer_init(report_buffer_t *buffer);

/*
 * Queue a RESULT (status 1 to 5 with intake index 0 to 15, or
 * REPORT_STATUS_UNSCHEDULED_REMOVAL with REPORT_MESSAGE_NO_INTAKE).
 * Design Document, Chapter 6, Section 6.1, Requirements 1.6, 2.13 and 3.2.
 */
report_buffer_push_result_t report_buffer_push_result(report_buffer_t *buffer,
                                                      report_status_t status,
                                                      uint8_t intake_index);

/*
 * Queue a FAULT (status 6 to 8, fault state active or cleared).
 * Design Document, Chapter 6, Section 6.1, Requirements 2.6, 2.10, 3.2
 * and 3.3.
 */
report_buffer_push_result_t report_buffer_push_fault(report_buffer_t *buffer,
                                                     report_status_t status,
                                                     report_fault_state_t fault_state);

/*
 * Queue the end-of-day message. Design Document, Chapter 6, Section 6.1,
 * Requirements 1.7 and 3.2.
 */
report_buffer_push_result_t report_buffer_push_day_end(report_buffer_t *buffer);

/*
 * Encode the oldest report into *frame without removing it. Assigns the
 * next report sequence number at the first call for a RESULT or FAULT and
 * keeps it afterwards. Returns false if the buffer is empty or on a
 * missing argument. Design Document, Chapter 6, Section 6.1,
 * Requirement 3.2.
 */
bool report_buffer_peek(report_buffer_t *buffer, report_buffer_frame_t *frame);

/*
 * Remove the oldest report after it was sent, but only if it is still the
 * entry with this id (it may have been dropped meanwhile because the buffer
 * was full). Returns true if an entry was removed.
 * Design Document, Chapter 6, Section 6.1, Requirement 3.2.
 */
bool report_buffer_commit(report_buffer_t *buffer, uint32_t id);

/*
 * Send the buffered reports in order until the buffer is empty or the send
 * function fails. Returns the number of reports sent. For a single thread;
 * the ESP32 service uses peek and commit with a lock instead.
 * Design Document, Chapter 6, Section 6.1, Requirement 3.2.
 */
uint32_t report_buffer_flush(report_buffer_t *buffer, report_buffer_send_fn send, void *context);

/*
 * Number of reports waiting to be sent (0 on a missing argument).
 * Design Document, Chapter 6, Section 6.1, Requirement 3.2.
 */
uint32_t report_buffer_count(const report_buffer_t *buffer);

#endif
