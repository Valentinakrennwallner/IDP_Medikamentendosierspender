#include "message_dispatch.h"

/* Modulus of the 1..255 report sequence cycle (0 is never reused). */
#define DISPATCH_SEQUENCE_MODULUS 255U

/*
 * Decide whether a report sequence number is new, following the receiver
 * rule in protocol/SECURE_CHANNEL.md: 0 is always accepted (ESP32 restart),
 * the first report after a gateway start is always accepted, otherwise the
 * distance to the last accepted number must be 1..127. Design Document,
 * Chapter 6, Section 6.1, Requirement 2.13 (each result reported once).
 */
static bool report_sequence_is_new(const message_dispatch_t *dispatch,
                                   uint8_t sequence)
{
    uint32_t distance;

    if ((sequence == (uint8_t)REPORT_MESSAGE_FIRST_SEQUENCE) ||
        !dispatch->report_accepted)
    {
        return true;
    }

    if (dispatch->last_sequence == (uint8_t)REPORT_MESSAGE_FIRST_SEQUENCE)
    {
        distance = (uint32_t)sequence;
    }
    else
    {
        distance = (((uint32_t)sequence + DISPATCH_SEQUENCE_MODULUS) -
                    (uint32_t)dispatch->last_sequence) %
                   DISPATCH_SEQUENCE_MODULUS;
    }

    return (distance >= 1U) && (distance <= REPORT_MESSAGE_MAX_PENDING);
}

/*
 * Report an accepted RESULT. Indices that are not below today's entry count
 * (or any RESULT before a schedule was sent) are reported as unassigned
 * instead of being dropped, so a missed dose is never lost. Design Document,
 * Chapter 2, Section 2.1, and Chapter 6, Section 6.1, Requirements 1.6 and
 * 2.13.
 */
static void dispatch_result(const message_dispatch_t *dispatch,
                            const report_message_t *report)
{
    const message_dispatch_callbacks_t *callbacks = &dispatch->callbacks;
    bool assigned;

    if (dispatch->entry_count == 0U)
    {
        assigned = false;
    }
    else if (report->intake_index == (uint8_t)REPORT_MESSAGE_NO_INTAKE)
    {
        /* The decoder allows 0xFF only with an unscheduled removal. */
        assigned = true;
    }
    else
    {
        assigned = ((size_t)report->intake_index < dispatch->entry_count);
    }

    if (assigned)
    {
        callbacks->on_result(callbacks->context, report->status,
                             report->intake_index);
    }
    else
    {
        callbacks->on_report_unassigned(callbacks->context, report->status);
    }
}

/*
 * Decode a RESULT or FAULT payload, drop duplicates and report new events.
 * Returns nonzero only for malformed payloads. Design Document, Chapter 2,
 * Section 2.1, and Chapter 6, Section 6.1, Requirements 1.6, 2.6, 2.10 and
 * 2.13.
 */
static int dispatch_report(message_dispatch_t *dispatch,
                           uint8_t expected_type,
                           const uint8_t *payload,
                           size_t payload_size)
{
    report_message_t report;

    if (report_message_decode(payload, payload_size, expected_type,
                              &report) != 0)
    {
        return -1;
    }

    if (!report_sequence_is_new(dispatch, report.sequence))
    {
        return 0;
    }

    dispatch->last_sequence = report.sequence;
    dispatch->report_accepted = true;

    if (report.message_type == (uint8_t)REPORT_MESSAGE_TYPE_RESULT)
    {
        dispatch_result(dispatch, &report);
    }
    else
    {
        dispatch->callbacks.on_fault(dispatch->callbacks.context,
                                     report.status, report.fault_state);
    }
    return 0;
}

/*
 * Initialize the gateway report dispatcher with no accepted report and no
 * schedule entry count. Design Document, Chapter 2, Section 2.1.
 */
int message_dispatch_init(message_dispatch_t *dispatch,
                          const message_dispatch_callbacks_t *callbacks)
{
    if ((dispatch == NULL) || (callbacks == NULL) ||
        (callbacks->on_result == NULL) ||
        (callbacks->on_report_unassigned == NULL) ||
        (callbacks->on_fault == NULL) || (callbacks->on_day_end == NULL))
    {
        return -1;
    }

    dispatch->callbacks = *callbacks;
    dispatch->entry_count = 0U;
    dispatch->last_sequence = (uint8_t)REPORT_MESSAGE_FIRST_SEQUENCE;
    dispatch->report_accepted = false;
    dispatch->initialized = true;
    return 0;
}

/*
 * Remember how many entries today's schedule sent to the ESP32 has, so that
 * result intake indices can be checked against it. Design Document,
 * Chapter 6, Section 6.1, Requirements 1.4 and 1.5.
 */
int message_dispatch_set_entry_count(message_dispatch_t *dispatch,
                                     size_t entry_count)
{
    if ((dispatch == NULL) || !dispatch->initialized ||
        (entry_count > (size_t)REPORT_MESSAGE_MAX_INTAKES))
    {
        return -1;
    }

    dispatch->entry_count = entry_count;
    return 0;
}

/*
 * Dispatch one authenticated application message from the BLE central.
 * Unknown types and malformed payloads return nonzero so that the central
 * closes the session. A valid day-end clears today's entry count. Design
 * Document, Chapter 2, Section 2.1, and Chapter 6, Section 6.1,
 * Requirements 1.6, 1.7, 2.6, 2.10 and 2.13.
 */
int message_dispatch_on_message(void *context,
                                secure_channel_message_type_t message_type,
                                const uint8_t *payload,
                                size_t payload_size)
{
    message_dispatch_t *dispatch = (message_dispatch_t *)context;
    int result = -1;

    if ((dispatch == NULL) || !dispatch->initialized)
    {
        return -1;
    }

    switch (message_type)
    {
    case SECURE_CHANNEL_MESSAGE_RESULT:
        result = dispatch_report(dispatch, (uint8_t)REPORT_MESSAGE_TYPE_RESULT,
                                 payload, payload_size);
        break;
    case SECURE_CHANNEL_MESSAGE_FAULT:
        result = dispatch_report(dispatch, (uint8_t)REPORT_MESSAGE_TYPE_FAULT,
                                 payload, payload_size);
        break;
    case SECURE_CHANNEL_MESSAGE_DAY_END:
        if (payload_size == 0U)
        {
            /* Today's schedule has ended: later results are unassigned
             * until the next schedule is sent. Sequence state is kept. */
            dispatch->entry_count = 0U;
            dispatch->callbacks.on_day_end(dispatch->callbacks.context);
            result = 0;
        }
        break;
    default:
        result = -1;
        break;
    }
    return result;
}
