#include "report_message.h"

#define REPORT_OFFSET_VERSION 0U
#define REPORT_OFFSET_TYPE 1U
#define REPORT_OFFSET_SEQUENCE 2U
#define REPORT_OFFSET_STATUS 3U
#define REPORT_OFFSET_DETAIL 4U

/* Wire value -> status; index 0 is the invalid status value 0. */
#define REPORT_STATUS_COUNT 9U

/*
 * Map a received status byte to the status enum without casting an integer
 * to an enum type. Design Document, Chapter 6, Section 6.1,
 * Requirements 2.6, 2.10 and 2.13.
 */
static bool status_from_wire(uint8_t value, report_status_t *status)
{
    static const report_status_t status_table[REPORT_STATUS_COUNT] = {
        REPORT_STATUS_TAKEN, /* placeholder for invalid value 0 */
        REPORT_STATUS_TAKEN,
        REPORT_STATUS_NOT_YET_TAKEN,
        REPORT_STATUS_WRONG_AMOUNT,
        REPORT_STATUS_MISSED,
        REPORT_STATUS_UNSCHEDULED_REMOVAL,
        REPORT_STATUS_FILLING_ERROR,
        REPORT_STATUS_SENSOR_FAULT,
        REPORT_STATUS_NO_TIME,
    };

    if ((value == 0U) || (value >= REPORT_STATUS_COUNT))
    {
        return false;
    }

    *status = status_table[value];
    return true;
}

/*
 * True for statuses carried by a RESULT message (dose evaluation results).
 * Design Document, Chapter 6, Section 6.1, Requirements 1.6, 1.8, 2.12 and
 * 2.13.
 */
bool report_status_is_result(report_status_t status)
{
    return (status == REPORT_STATUS_TAKEN) ||
           (status == REPORT_STATUS_NOT_YET_TAKEN) ||
           (status == REPORT_STATUS_WRONG_AMOUNT) ||
           (status == REPORT_STATUS_MISSED) ||
           (status == REPORT_STATUS_UNSCHEDULED_REMOVAL);
}

/*
 * True for statuses carried by a FAULT message (filling error, sensor fault,
 * no schedule/time). Design Document, Chapter 6, Section 6.1,
 * Requirements 2.6, 2.10 and 3.3.
 */
bool report_status_is_fault(report_status_t status)
{
    return (status == REPORT_STATUS_FILLING_ERROR) ||
           (status == REPORT_STATUS_SENSOR_FAULT) ||
           (status == REPORT_STATUS_NO_TIME);
}

/*
 * Check the intake index of a RESULT: an unscheduled removal is never
 * attributed to a dose (0xFF), every other result names one of at most
 * REPORT_MESSAGE_MAX_INTAKES intakes. Design Document, Chapter 6,
 * Section 6.1, Requirements 1.5 and 2.13.
 */
static bool result_index_is_valid(report_status_t status, uint8_t intake_index)
{
    if (status == REPORT_STATUS_UNSCHEDULED_REMOVAL)
    {
        return intake_index == REPORT_MESSAGE_NO_INTAKE;
    }

    return intake_index < REPORT_MESSAGE_MAX_INTAKES;
}

/*
 * Check a caller-supplied fault state before it is written to the wire.
 * Design Document, Chapter 6, Section 6.1, Requirements 2.7 and 2.10.
 */
static bool fault_state_is_valid(report_fault_state_t fault_state)
{
    return (fault_state == REPORT_FAULT_CLEARED) ||
           (fault_state == REPORT_FAULT_ACTIVE);
}

/*
 * Write the common version/type/sequence/status header and the detail byte.
 * Design Document, Chapter 2, Section 2.1 (Bluetooth communication module).
 */
static void write_report(uint8_t message_type,
                         uint8_t sequence,
                         report_status_t status,
                         uint8_t detail,
                         uint8_t *output,
                         size_t *output_size)
{
    output[REPORT_OFFSET_VERSION] = (uint8_t)REPORT_MESSAGE_VERSION;
    output[REPORT_OFFSET_TYPE] = message_type;
    output[REPORT_OFFSET_SEQUENCE] = sequence;
    output[REPORT_OFFSET_STATUS] = (uint8_t)status;
    output[REPORT_OFFSET_DETAIL] = detail;
    *output_size = (size_t)REPORT_MESSAGE_SIZE;
}

/*
 * Encode one dose evaluation result for the Raspberry Pi. Each new result is
 * reported once (buffered while disconnected). Design Document, Chapter 2,
 * Section 2.1, and Chapter 6, Section 6.1, Requirements 1.6, 2.13 and 3.2.
 */
int report_message_encode_result(uint8_t sequence,
                                 report_status_t status,
                                 uint8_t intake_index,
                                 uint8_t *output,
                                 size_t output_capacity,
                                 size_t *output_size)
{
    if ((output == NULL) || (output_size == NULL) ||
        (output_capacity < (size_t)REPORT_MESSAGE_SIZE) ||
        !report_status_is_result(status) ||
        !result_index_is_valid(status, intake_index))
    {
        return -1;
    }

    write_report((uint8_t)REPORT_MESSAGE_TYPE_RESULT, sequence, status,
                 intake_index, output, output_size);
    return 0;
}

/*
 * Encode the start or end of a fault (filling error, sensor fault, no
 * schedule/time) for the Raspberry Pi alert. Design Document, Chapter 2,
 * Section 2.1, and Chapter 6, Section 6.1, Requirements 2.6, 2.7, 2.10 and
 * 3.3.
 */
int report_message_encode_fault(uint8_t sequence,
                                report_status_t status,
                                report_fault_state_t fault_state,
                                uint8_t *output,
                                size_t output_capacity,
                                size_t *output_size)
{
    if ((output == NULL) || (output_size == NULL) ||
        (output_capacity < (size_t)REPORT_MESSAGE_SIZE) ||
        !report_status_is_fault(status) || !fault_state_is_valid(fault_state))
    {
        return -1;
    }

    write_report((uint8_t)REPORT_MESSAGE_TYPE_FAULT, sequence, status,
                 (uint8_t)fault_state, output, output_size);
    return 0;
}

/*
 * Decode and validate an authenticated RESULT or FAULT payload. The payload
 * type must match the secure-channel message type (expected_type). On
 * failure the output is not modified. Design Document, Chapter 2,
 * Section 2.1, and Chapter 6, Section 6.1, Requirements 2.6, 2.10, 2.13
 * and 3.2.
 */
int report_message_decode(const uint8_t *message,
                          size_t message_size,
                          uint8_t expected_type,
                          report_message_t *report)
{
    report_status_t status = REPORT_STATUS_TAKEN;
    uint8_t message_type;
    uint8_t detail;
    report_message_t decoded;

    if ((message == NULL) || (report == NULL) ||
        (message_size != (size_t)REPORT_MESSAGE_SIZE) ||
        (message[REPORT_OFFSET_VERSION] != (uint8_t)REPORT_MESSAGE_VERSION) ||
        (message[REPORT_OFFSET_TYPE] != expected_type) ||
        !status_from_wire(message[REPORT_OFFSET_STATUS], &status))
    {
        return -1;
    }

    message_type = message[REPORT_OFFSET_TYPE];
    detail = message[REPORT_OFFSET_DETAIL];
    decoded.message_type = message_type;
    decoded.sequence = message[REPORT_OFFSET_SEQUENCE];
    decoded.status = status;

    if (message_type == (uint8_t)REPORT_MESSAGE_TYPE_RESULT)
    {
        if (!report_status_is_result(status) ||
            !result_index_is_valid(status, detail))
        {
            return -1;
        }
        decoded.intake_index = detail;
        decoded.fault_state = REPORT_FAULT_CLEARED;
    }
    else if (message_type == (uint8_t)REPORT_MESSAGE_TYPE_FAULT)
    {
        if (!report_status_is_fault(status))
        {
            return -1;
        }
        if (detail == (uint8_t)REPORT_FAULT_ACTIVE)
        {
            decoded.fault_state = REPORT_FAULT_ACTIVE;
        }
        else if (detail == (uint8_t)REPORT_FAULT_CLEARED)
        {
            decoded.fault_state = REPORT_FAULT_CLEARED;
        }
        else
        {
            return -1;
        }
        decoded.intake_index = (uint8_t)REPORT_MESSAGE_NO_INTAKE;
    }
    else
    {
        return -1;
    }

    *report = decoded;
    return 0;
}
