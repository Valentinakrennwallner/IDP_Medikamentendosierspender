#include "report_message.h"
#include "secure_channel.h"

#include "../../esp32/components/dosing/dosing.h"

#include <stdio.h>
#include <string.h>

/* Return nonzero if a message is accepted by the decoder. */
static int expect_rejected(const uint8_t *message, size_t size, uint8_t type)
{
    report_message_t report;

    (void)memset(&report, 0xA5, sizeof(report));
    if (report_message_decode(message, size, type, &report) == 0)
    {
        printf("  decode accepted %02x %02x %02x %02x %02x\n",
               (unsigned int)message[0], (unsigned int)message[1],
               (unsigned int)message[2], (unsigned int)message[3],
               (unsigned int)message[4]);
        return 1;
    }
    return 0;
}

/* The payload types match the secure-channel message types. */
static int test_message_types(void)
{
    if ((REPORT_MESSAGE_TYPE_RESULT != (unsigned int)SECURE_CHANNEL_MESSAGE_RESULT) ||
        (REPORT_MESSAGE_TYPE_FAULT != (unsigned int)SECURE_CHANNEL_MESSAGE_FAULT))
    {
        return 1;
    }
    return 0;
}

/* The protocol index limit covers every intake the ESP32 dosing table holds. */
static int test_intake_limit_matches_dosing(void)
{
    if (DOSING_MAX_INTAKES > REPORT_MESSAGE_MAX_INTAKES)
    {
        printf("  DOSING_MAX_INTAKES exceeds REPORT_MESSAGE_MAX_INTAKES\n");
        return 1;
    }
    return 0;
}

/* Requirement 2.13 and 3.2: RESULT wire layout and round trip. */
static int test_result_round_trip(void)
{
    static const report_status_t statuses[] = {
        REPORT_STATUS_TAKEN,
        REPORT_STATUS_NOT_YET_TAKEN,
        REPORT_STATUS_WRONG_AMOUNT,
        REPORT_STATUS_MISSED,
    };
    uint8_t buffer[REPORT_MESSAGE_SIZE];
    size_t size = 0U;
    size_t index;
    report_message_t report;

    for (index = 0U; index < (sizeof(statuses) / sizeof(statuses[0])); index++)
    {
        const uint8_t intake = (uint8_t)(REPORT_MESSAGE_MAX_INTAKES - 1U);
        const uint8_t expected[REPORT_MESSAGE_SIZE] = {
            1U, 4U, 200U, (uint8_t)statuses[index], intake};

        if ((report_message_encode_result(200U, statuses[index], intake, buffer,
                                          sizeof(buffer), &size) != 0) ||
            (size != REPORT_MESSAGE_SIZE) ||
            (memcmp(buffer, expected, sizeof(expected)) != 0) ||
            (report_message_decode(buffer, size, 4U, &report) != 0) ||
            (report.message_type != 4U) || (report.sequence != 200U) ||
            (report.status != statuses[index]) ||
            (report.intake_index != intake) ||
            (report.fault_state != REPORT_FAULT_CLEARED))
        {
            return 1;
        }
    }

    /* Index 0 and sequence 0 (first report since start-up). */
    if ((report_message_encode_result(0U, REPORT_STATUS_TAKEN, 0U, buffer,
                                      sizeof(buffer), &size) != 0) ||
        (report_message_decode(buffer, size, 4U, &report) != 0) ||
        (report.sequence != 0U) || (report.intake_index != 0U))
    {
        return 1;
    }
    return 0;
}

/* Requirement 2.13: an unscheduled removal is never attributed to a dose. */
static int test_unscheduled_removal_index(void)
{
    const uint8_t expected[REPORT_MESSAGE_SIZE] = {1U, 4U, 255U, 5U, 0xFFU};
    uint8_t buffer[REPORT_MESSAGE_SIZE];
    size_t size = 0U;
    report_message_t report;

    if ((report_message_encode_result(255U, REPORT_STATUS_UNSCHEDULED_REMOVAL,
                                      REPORT_MESSAGE_NO_INTAKE, buffer,
                                      sizeof(buffer), &size) != 0) ||
        (memcmp(buffer, expected, sizeof(expected)) != 0) ||
        (report_message_decode(buffer, size, 4U, &report) != 0) ||
        (report.status != REPORT_STATUS_UNSCHEDULED_REMOVAL) ||
        (report.intake_index != REPORT_MESSAGE_NO_INTAKE))
    {
        return 1;
    }

    /* Removal with a dose index, or a dose result without one. */
    if ((report_message_encode_result(1U, REPORT_STATUS_UNSCHEDULED_REMOVAL, 0U,
                                      buffer, sizeof(buffer), &size) == 0) ||
        (report_message_encode_result(1U, REPORT_STATUS_MISSED,
                                      REPORT_MESSAGE_NO_INTAKE, buffer,
                                      sizeof(buffer), &size) == 0))
    {
        return 1;
    }
    return 0;
}

/* Requirements 2.6, 2.7, 2.10 and 3.3: FAULT layout, start and clear. */
static int test_fault_round_trip(void)
{
    static const report_status_t statuses[] = {
        REPORT_STATUS_FILLING_ERROR,
        REPORT_STATUS_SENSOR_FAULT,
        REPORT_STATUS_NO_TIME,
    };
    static const report_fault_state_t states[] = {
        REPORT_FAULT_ACTIVE,
        REPORT_FAULT_CLEARED,
    };
    uint8_t buffer[REPORT_MESSAGE_SIZE];
    size_t size = 0U;
    size_t status_index;
    size_t state_index;
    report_message_t report;

    for (status_index = 0U; status_index < (sizeof(statuses) / sizeof(statuses[0]));
         status_index++)
    {
        for (state_index = 0U; state_index < (sizeof(states) / sizeof(states[0]));
             state_index++)
        {
            const uint8_t expected[REPORT_MESSAGE_SIZE] = {
                1U, 6U, 7U, (uint8_t)statuses[status_index],
                (uint8_t)states[state_index]};

            if ((report_message_encode_fault(7U, statuses[status_index],
                                             states[state_index], buffer,
                                             sizeof(buffer), &size) != 0) ||
                (size != REPORT_MESSAGE_SIZE) ||
                (memcmp(buffer, expected, sizeof(expected)) != 0) ||
                (report_message_decode(buffer, size, 6U, &report) != 0) ||
                (report.message_type != 6U) || (report.sequence != 7U) ||
                (report.status != statuses[status_index]) ||
                (report.fault_state != states[state_index]) ||
                (report.intake_index != REPORT_MESSAGE_NO_INTAKE))
            {
                return 1;
            }
        }
    }
    return 0;
}

/* Encoders reject statuses of the other message type and bad arguments. */
static int test_encode_rejects_invalid(void)
{
    uint8_t buffer[REPORT_MESSAGE_SIZE];
    size_t size = 0U;

    if ((report_message_encode_result(1U, REPORT_STATUS_SENSOR_FAULT, 0U, buffer,
                                      sizeof(buffer), &size) == 0) ||
        (report_message_encode_result(1U, REPORT_STATUS_TAKEN,
                                      (uint8_t)REPORT_MESSAGE_MAX_INTAKES, buffer,
                                      sizeof(buffer), &size) == 0) ||
        (report_message_encode_result(1U, REPORT_STATUS_TAKEN, 0U, buffer,
                                      sizeof(buffer) - 1U, &size) == 0) ||
        (report_message_encode_result(1U, REPORT_STATUS_TAKEN, 0U, NULL,
                                      sizeof(buffer), &size) == 0) ||
        (report_message_encode_result(1U, REPORT_STATUS_TAKEN, 0U, buffer,
                                      sizeof(buffer), NULL) == 0) ||
        (report_message_encode_fault(1U, REPORT_STATUS_MISSED, REPORT_FAULT_ACTIVE,
                                     buffer, sizeof(buffer), &size) == 0) ||
        (report_message_encode_fault(1U, REPORT_STATUS_NO_TIME, REPORT_FAULT_ACTIVE,
                                     buffer, sizeof(buffer) - 1U, &size) == 0) ||
        (report_message_encode_fault(1U, REPORT_STATUS_NO_TIME, REPORT_FAULT_ACTIVE,
                                     NULL, sizeof(buffer), &size) == 0))
    {
        return 1;
    }
    return 0;
}

/* Malformed payloads are rejected and leave the output unchanged. */
static int test_decode_rejects_malformed(void)
{
    const uint8_t valid_result[REPORT_MESSAGE_SIZE] = {1U, 4U, 3U, 1U, 2U};
    const uint8_t valid_fault[REPORT_MESSAGE_SIZE] = {1U, 6U, 3U, 7U, 1U};
    const uint8_t longer[REPORT_MESSAGE_SIZE + 1U] = {1U, 4U, 3U, 1U, 2U, 0U};
    const uint8_t cases[][REPORT_MESSAGE_SIZE] = {
        {0U, 4U, 3U, 1U, 2U},    /* version 0 */
        {2U, 4U, 3U, 1U, 2U},    /* future version */
        {1U, 4U, 3U, 0U, 2U},    /* status 0 */
        {1U, 4U, 3U, 9U, 2U},    /* status out of range */
        {1U, 4U, 3U, 6U, 2U},    /* fault status in a RESULT */
        {1U, 4U, 3U, 1U, 16U},   /* index above the maximum */
        {1U, 4U, 3U, 1U, 0xFFU}, /* dose result without an index */
        {1U, 4U, 3U, 5U, 0U},    /* removal with an index */
    };
    const uint8_t fault_cases[][REPORT_MESSAGE_SIZE] = {
        {1U, 6U, 3U, 2U, 1U}, /* result status in a FAULT */
        {1U, 6U, 3U, 7U, 2U}, /* fault state out of range */
        {1U, 6U, 3U, 7U, 0xFFU},
    };
    report_message_t report;
    size_t index;

    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); index++)
    {
        if (expect_rejected(cases[index], REPORT_MESSAGE_SIZE, 4U) != 0)
        {
            return 1;
        }
    }
    for (index = 0U; index < (sizeof(fault_cases) / sizeof(fault_cases[0])); index++)
    {
        if (expect_rejected(fault_cases[index], REPORT_MESSAGE_SIZE, 6U) != 0)
        {
            return 1;
        }
    }

    /* Length, type mismatch with the secure-channel header, NULL. */
    if ((expect_rejected(valid_result, REPORT_MESSAGE_SIZE - 1U, 4U) != 0) ||
        (expect_rejected(longer, sizeof(longer), 4U) != 0) ||
        (expect_rejected(valid_result, REPORT_MESSAGE_SIZE, 6U) != 0) ||
        (expect_rejected(valid_fault, REPORT_MESSAGE_SIZE, 4U) != 0) ||
        (expect_rejected(valid_result, REPORT_MESSAGE_SIZE, 1U) != 0) ||
        (report_message_decode(NULL, REPORT_MESSAGE_SIZE, 4U, &report) == 0) ||
        (report_message_decode(valid_result, REPORT_MESSAGE_SIZE, 4U, NULL) == 0))
    {
        return 1;
    }

    /* A rejected payload must not overwrite a previous result. */
    if (report_message_decode(valid_result, REPORT_MESSAGE_SIZE, 4U, &report) != 0)
    {
        return 1;
    }
    (void)report_message_decode(cases[0], REPORT_MESSAGE_SIZE, 4U, &report);
    if ((report.status != REPORT_STATUS_TAKEN) || (report.intake_index != 2U))
    {
        return 1;
    }
    return 0;
}

/* Status classification is disjoint and complete. */
static int test_status_classification(void)
{
    if ((!report_status_is_result(REPORT_STATUS_TAKEN)) ||
        (!report_status_is_result(REPORT_STATUS_UNSCHEDULED_REMOVAL)) ||
        report_status_is_result(REPORT_STATUS_FILLING_ERROR) ||
        report_status_is_fault(REPORT_STATUS_MISSED) ||
        (!report_status_is_fault(REPORT_STATUS_FILLING_ERROR)) ||
        (!report_status_is_fault(REPORT_STATUS_NO_TIME)))
    {
        return 1;
    }
    return 0;
}

/* Run all result/fault payload tests and report failures. */
int main(void)
{
    struct
    {
        const char *name;
        int (*run)(void);
    } tests[] = {
        {"message_types", test_message_types},
        {"intake_limit_matches_dosing", test_intake_limit_matches_dosing},
        {"result_round_trip", test_result_round_trip},
        {"unscheduled_removal_index", test_unscheduled_removal_index},
        {"fault_round_trip", test_fault_round_trip},
        {"encode_rejects_invalid", test_encode_rejects_invalid},
        {"decode_rejects_malformed", test_decode_rejects_malformed},
        {"status_classification", test_status_classification},
    };
    int failures = 0;
    size_t index;

    for (index = 0U; index < (sizeof(tests) / sizeof(tests[0])); index++)
    {
        if (tests[index].run() != 0)
        {
            printf("FAIL %s\n", tests[index].name);
            failures++;
        }
    }

    if (failures == 0)
    {
        printf("All report message tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
