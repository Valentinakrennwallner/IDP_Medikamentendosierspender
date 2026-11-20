#ifndef REPORT_MESSAGE_H
#define REPORT_MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Result (type 4) and fault (type 6) application payloads sent from the
 * ESP32 to the Raspberry Pi. Both are fixed-size and carry no weights,
 * times, medication names or doses. The layout is defined in
 * SECURE_CHANNEL.md and README.md of this directory.
 */
#define REPORT_MESSAGE_VERSION 1U
#define REPORT_MESSAGE_TYPE_RESULT 4U
#define REPORT_MESSAGE_TYPE_FAULT 6U
#define REPORT_MESSAGE_SIZE 5U

/* Highest number of intakes per day a result can refer to (index 0..15). */
#define REPORT_MESSAGE_MAX_INTAKES 16U
/* Intake index of an unscheduled removal: not attributable to a dose. */
#define REPORT_MESSAGE_NO_INTAKE 0xFFU

/*
 * Report sequence number: 0 marks the first report since ESP32 start-up,
 * later reports count 1..255 and wrap from 255 to 1. A receiver accepts a
 * report when its distance to the last accepted number is 1..127, so the
 * ESP32 must never hold more than REPORT_MESSAGE_MAX_PENDING unsent reports.
 */
#define REPORT_MESSAGE_FIRST_SEQUENCE 0U
#define REPORT_MESSAGE_MAX_SEQUENCE 255U
#define REPORT_MESSAGE_MAX_PENDING 127U

/* One status numbering for both messages; 0 is never valid. */
typedef enum
{
    REPORT_STATUS_TAKEN = 1,               /* RESULT */
    REPORT_STATUS_NOT_YET_TAKEN = 2,       /* RESULT */
    REPORT_STATUS_WRONG_AMOUNT = 3,        /* RESULT */
    REPORT_STATUS_MISSED = 4,              /* RESULT */
    REPORT_STATUS_UNSCHEDULED_REMOVAL = 5, /* RESULT, intake index 0xFF */
    REPORT_STATUS_FILLING_ERROR = 6,       /* FAULT */
    REPORT_STATUS_SENSOR_FAULT = 7,        /* FAULT */
    REPORT_STATUS_NO_TIME = 8              /* FAULT */
} report_status_t;

typedef enum
{
    REPORT_FAULT_CLEARED = 0,
    REPORT_FAULT_ACTIVE = 1
} report_fault_state_t;

/*
 * Decoded report. For a RESULT, fault_state is REPORT_FAULT_CLEARED; for a
 * FAULT, intake_index is REPORT_MESSAGE_NO_INTAKE.
 */
typedef struct
{
    uint8_t message_type;
    uint8_t sequence;
    report_status_t status;
    uint8_t intake_index;
    report_fault_state_t fault_state;
} report_message_t;

bool report_status_is_result(report_status_t status);
bool report_status_is_fault(report_status_t status);
int report_message_encode_result(uint8_t sequence,
                                 report_status_t status,
                                 uint8_t intake_index,
                                 uint8_t *output,
                                 size_t output_capacity,
                                 size_t *output_size);
int report_message_encode_fault(uint8_t sequence,
                                report_status_t status,
                                report_fault_state_t fault_state,
                                uint8_t *output,
                                size_t output_capacity,
                                size_t *output_size);
int report_message_decode(const uint8_t *message,
                          size_t message_size,
                          uint8_t expected_type,
                          report_message_t *report);

#endif
