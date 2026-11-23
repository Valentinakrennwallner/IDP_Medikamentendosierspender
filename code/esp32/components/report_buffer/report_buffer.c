#include "report_buffer.h"

/*
 * Encode one entry with the given sequence number using the shared
 * protocol encoders (code/protocol). Also used to validate an entry before
 * it is queued. Returns true on success.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.13.
 */
static bool report_buffer_encode(const report_buffer_entry_t *entry, uint8_t sequence,
                                 uint8_t *payload, size_t *length)
{
    int status = -1;

    if (entry->message_type == REPORT_MESSAGE_TYPE_RESULT)
    {
        status = report_message_encode_result(sequence, entry->status, entry->detail, payload,
                                              REPORT_MESSAGE_SIZE, length);
    }
    else if (entry->message_type == REPORT_MESSAGE_TYPE_FAULT)
    {
        status = report_message_encode_fault(sequence, entry->status,
                                             (report_fault_state_t)entry->detail, payload,
                                             REPORT_MESSAGE_SIZE, length);
    }
    else if (entry->message_type == REPORT_BUFFER_MESSAGE_TYPE_DAY_END)
    {
        *length = 0U;
        status = 0;
    }
    else
    {
        /* Unknown type: not encodable. */
    }
    return (status == 0);
}

/*
 * Next report sequence number: 0 for the first report since start-up,
 * then 1 to 255, wrapping from 255 to 1 (code/protocol/SECURE_CHANNEL.md).
 * It lets the Raspberry Pi recognise reports resent after a reconnect.
 * Design Document, Chapter 6, Section 6.1, Requirement 3.2.
 */
static uint8_t report_buffer_next_sequence(report_buffer_t *buffer)
{
    uint8_t sequence;

    if (!buffer->first_sequence_used)
    {
        buffer->first_sequence_used = true;
        sequence = (uint8_t)REPORT_MESSAGE_FIRST_SEQUENCE;
    }
    else if (buffer->last_sequence >= (uint8_t)REPORT_MESSAGE_MAX_SEQUENCE)
    {
        sequence = 1U;
    }
    else
    {
        sequence = buffer->last_sequence + 1U;
    }
    buffer->last_sequence = sequence;
    return sequence;
}

/*
 * Validate and append one entry. If the buffer is full, the oldest entry is
 * dropped first (the current state is kept). Design Document, Chapter 6,
 * Section 6.1, Requirement 3.2.
 */
static report_buffer_push_result_t report_buffer_push(report_buffer_t *buffer,
                                                      uint8_t message_type,
                                                      report_status_t status,
                                                      uint8_t detail)
{
    report_buffer_push_result_t result = REPORT_BUFFER_PUSH_OK;
    report_buffer_entry_t entry;
    uint8_t payload[REPORT_MESSAGE_SIZE];
    size_t length = 0U;
    uint32_t tail;

    if (buffer == NULL)
    {
        return REPORT_BUFFER_PUSH_INVALID;
    }

    entry.message_type = message_type;
    entry.status = status;
    entry.detail = detail;
    entry.has_sequence = false;
    entry.sequence = 0U;
    entry.id = buffer->next_id;
    if (!report_buffer_encode(&entry, 0U, payload, &length))
    {
        return REPORT_BUFFER_PUSH_INVALID;
    }

    if (buffer->count >= REPORT_BUFFER_CAPACITY)
    {
        buffer->head = (buffer->head + 1U) % REPORT_BUFFER_CAPACITY;
        buffer->count--;
        buffer->dropped_count++;
        result = REPORT_BUFFER_PUSH_OLDEST_DROPPED;
    }
    tail = (buffer->head + buffer->count) % REPORT_BUFFER_CAPACITY;
    buffer->entries[tail] = entry;
    buffer->count++;
    buffer->next_id++;
    return result;
}

/*
 * Empty the buffer; the next report gets sequence number 0.
 * Design Document, Chapter 6, Section 6.1, Requirement 3.2.
 */
void report_buffer_init(report_buffer_t *buffer)
{
    if (buffer != NULL)
    {
        buffer->head = 0U;
        buffer->count = 0U;
        buffer->next_id = 0U;
        buffer->first_sequence_used = false;
        buffer->last_sequence = 0U;
        buffer->dropped_count = 0U;
    }
}

/*
 * Queue a RESULT (status 1 to 5 with intake index 0 to 15, or
 * REPORT_STATUS_UNSCHEDULED_REMOVAL with REPORT_MESSAGE_NO_INTAKE).
 * Design Document, Chapter 6, Section 6.1, Requirements 1.6, 2.13 and 3.2.
 */
report_buffer_push_result_t report_buffer_push_result(report_buffer_t *buffer,
                                                      report_status_t status,
                                                      uint8_t intake_index)
{
    return report_buffer_push(buffer, (uint8_t)REPORT_MESSAGE_TYPE_RESULT, status, intake_index);
}

/*
 * Queue a FAULT (status 6 to 8, fault state active or cleared).
 * Design Document, Chapter 6, Section 6.1, Requirements 2.6, 2.10, 3.2
 * and 3.3.
 */
report_buffer_push_result_t report_buffer_push_fault(report_buffer_t *buffer,
                                                     report_status_t status,
                                                     report_fault_state_t fault_state)
{
    uint8_t detail = (fault_state == REPORT_FAULT_ACTIVE) ? 1U : 0U;

    if ((fault_state != REPORT_FAULT_ACTIVE) && (fault_state != REPORT_FAULT_CLEARED))
    {
        return REPORT_BUFFER_PUSH_INVALID;
    }
    return report_buffer_push(buffer, (uint8_t)REPORT_MESSAGE_TYPE_FAULT, status, detail);
}

/*
 * Queue the end-of-day message. Design Document, Chapter 6, Section 6.1,
 * Requirements 1.7 and 3.2.
 */
report_buffer_push_result_t report_buffer_push_day_end(report_buffer_t *buffer)
{
    return report_buffer_push(buffer, (uint8_t)REPORT_BUFFER_MESSAGE_TYPE_DAY_END,
                              REPORT_STATUS_TAKEN, 0U);
}

/*
 * Encode the oldest report into *frame without removing it. Assigns the
 * next report sequence number at the first call for a RESULT or FAULT and
 * keeps it afterwards. Returns false if the buffer is empty or on a
 * missing argument. Design Document, Chapter 6, Section 6.1,
 * Requirement 3.2.
 */
bool report_buffer_peek(report_buffer_t *buffer, report_buffer_frame_t *frame)
{
    report_buffer_entry_t *entry;

    if ((buffer == NULL) || (frame == NULL) || (buffer->count == 0U))
    {
        return false;
    }

    entry = &buffer->entries[buffer->head];
    if ((entry->message_type != (uint8_t)REPORT_BUFFER_MESSAGE_TYPE_DAY_END) &&
        (!entry->has_sequence))
    {
        entry->sequence = report_buffer_next_sequence(buffer);
        entry->has_sequence = true;
    }
    frame->message_type = entry->message_type;
    frame->id = entry->id;
    /* Cannot fail: every entry was validated with the same encoder when queued. */
    return report_buffer_encode(entry, entry->sequence, frame->payload, &frame->length);
}

/*
 * Remove the oldest report after it was sent, but only if it is still the
 * entry with this id. Returns true if an entry was removed.
 * Design Document, Chapter 6, Section 6.1, Requirement 3.2.
 */
bool report_buffer_commit(report_buffer_t *buffer, uint32_t id)
{
    bool removed = false;

    if ((buffer != NULL) && (buffer->count > 0U) && (buffer->entries[buffer->head].id == id))
    {
        buffer->head = (buffer->head + 1U) % REPORT_BUFFER_CAPACITY;
        buffer->count--;
        removed = true;
    }
    return removed;
}

/*
 * Send the buffered reports in order until the buffer is empty or the send
 * function fails. Returns the number of reports sent.
 * Design Document, Chapter 6, Section 6.1, Requirement 3.2.
 */
uint32_t report_buffer_flush(report_buffer_t *buffer, report_buffer_send_fn send, void *context)
{
    report_buffer_frame_t frame;
    uint32_t sent = 0U;
    bool sending = (send != NULL);

    while (sending && report_buffer_peek(buffer, &frame))
    {
        if (send(context, frame.message_type, frame.payload, frame.length))
        {
            (void)report_buffer_commit(buffer, frame.id);
            sent++;
        }
        else
        {
            sending = false;
        }
    }
    return sent;
}

/*
 * Number of reports waiting to be sent (0 on a missing argument).
 * Design Document, Chapter 6, Section 6.1, Requirement 3.2.
 */
uint32_t report_buffer_count(const report_buffer_t *buffer)
{
    return (buffer != NULL) ? buffer->count : 0U;
}
