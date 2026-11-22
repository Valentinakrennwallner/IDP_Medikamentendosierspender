#include <stdio.h>
#include <stdlib.h>

#include "sensor_fault.h"

/*
 * Host tests for the sensor-fault state machine. The monotonic clock is
 * injected as a plain millisecond value, so no timer or hardware is needed.
 * Design Document, Chapter 6, Section 6.1, Requirements 2.6 and 2.7.
 */

/* Report a failed check with its line number and return 1. */
static int fail(int line)
{
    (void)fprintf(stderr, "sensor_fault_test: check failed at line %d\n", line);
    return 1;
}

#define CHECK(condition)          \
    do                            \
    {                             \
        if (!(condition))         \
        {                         \
            return fail(__LINE__); \
        }                         \
    } while (0)

/* After init the sensor counts as valid: measure and evaluate. */
static int test_initial_state(void)
{
    sensor_fault_t fault;

    sensor_fault_init(&fault);
    CHECK(fault.state == SENSOR_FAULT_STATE_NORMAL);
    CHECK(sensor_fault_measurement_due(&fault, 0U));
    CHECK(sensor_fault_evaluation_allowed(&fault));
    return 0;
}

/* Valid measurements in the normal state change nothing. */
static int test_valid_measurements_stay_normal(void)
{
    sensor_fault_t fault;
    uint64_t now_ms;

    sensor_fault_init(&fault);
    for (now_ms = 0U; now_ms < 5000U; now_ms += 1000U)
    {
        CHECK(sensor_fault_update(&fault, WEIGHT_MEASURE_OK, now_ms) ==
              SENSOR_FAULT_ACTION_NONE);
        CHECK(sensor_fault_evaluation_allowed(&fault));
        CHECK(sensor_fault_measurement_due(&fault, now_ms));
    }
    return 0;
}

/*
 * Requirement 2.6: a sensor fault suspends the evaluation and asks the caller
 * to flash red and report the fault. Requirement 2.7: the next recheck is due
 * exactly 10 s later, not earlier.
 */
static int test_fault_suspends_evaluation_and_waits_10_s(void)
{
    sensor_fault_t fault;

    sensor_fault_init(&fault);
    CHECK(sensor_fault_update(&fault, WEIGHT_MEASURE_SENSOR_FAULT, 1000U) ==
          SENSOR_FAULT_ACTION_ENTER_FAULT);
    CHECK(fault.state == SENSOR_FAULT_STATE_FAULT);
    CHECK(!sensor_fault_evaluation_allowed(&fault));
    CHECK(!sensor_fault_measurement_due(&fault, 1000U));
    CHECK(!sensor_fault_measurement_due(&fault, 10999U));
    CHECK(sensor_fault_measurement_due(&fault, 11000U));
    return 0;
}

/*
 * Requirements 2.6 and 2.7: the fault is reported once; failed rechecks keep
 * the fault, report nothing new, and schedule the next recheck 10 s after
 * the actual recheck time (also when the recheck ran late).
 */
static int test_failed_rechecks_report_once(void)
{
    sensor_fault_t fault;
    uint64_t now_ms = 11000U;
    unsigned int recheck;

    sensor_fault_init(&fault);
    CHECK(sensor_fault_update(&fault, WEIGHT_MEASURE_SENSOR_FAULT, 1000U) ==
          SENSOR_FAULT_ACTION_ENTER_FAULT);
    for (recheck = 0U; recheck < 5U; ++recheck)
    {
        CHECK(sensor_fault_measurement_due(&fault, now_ms));
        CHECK(sensor_fault_update(&fault, WEIGHT_MEASURE_SENSOR_FAULT, now_ms) ==
              SENSOR_FAULT_ACTION_NONE);
        CHECK(!sensor_fault_evaluation_allowed(&fault));
        CHECK(!sensor_fault_measurement_due(&fault, now_ms + 9999U));
        CHECK(sensor_fault_measurement_due(&fault, now_ms + 10000U));
        now_ms += 10000U;
    }

    /* A late recheck (4 s after it was due) restarts the 10 s interval. */
    CHECK(sensor_fault_update(&fault, WEIGHT_MEASURE_SENSOR_FAULT,
                              now_ms + 4000U) == SENSOR_FAULT_ACTION_NONE);
    CHECK(!sensor_fault_measurement_due(&fault, now_ms + 13999U));
    CHECK(sensor_fault_measurement_due(&fault, now_ms + 14000U));
    return 0;
}

/* Requirement 2.7: a valid recheck ends the fault and resumes evaluation. */
static int test_valid_recheck_resumes(void)
{
    sensor_fault_t fault;

    sensor_fault_init(&fault);
    (void)sensor_fault_update(&fault, WEIGHT_MEASURE_SENSOR_FAULT, 1000U);
    (void)sensor_fault_update(&fault, WEIGHT_MEASURE_SENSOR_FAULT, 11000U);
    CHECK(sensor_fault_update(&fault, WEIGHT_MEASURE_OK, 21000U) ==
          SENSOR_FAULT_ACTION_RESUME);
    CHECK(fault.state == SENSOR_FAULT_STATE_NORMAL);
    CHECK(sensor_fault_evaluation_allowed(&fault));
    CHECK(sensor_fault_measurement_due(&fault, 21000U));

    /* The next valid measurement reports nothing new. */
    CHECK(sensor_fault_update(&fault, WEIGHT_MEASURE_OK, 22000U) ==
          SENSOR_FAULT_ACTION_NONE);
    return 0;
}

/* A new fault after recovery is reported again. */
static int test_second_fault_is_reported_again(void)
{
    sensor_fault_t fault;

    sensor_fault_init(&fault);
    (void)sensor_fault_update(&fault, WEIGHT_MEASURE_SENSOR_FAULT, 1000U);
    (void)sensor_fault_update(&fault, WEIGHT_MEASURE_OK, 11000U);
    CHECK(sensor_fault_update(&fault, WEIGHT_MEASURE_SENSOR_FAULT, 12000U) ==
          SENSOR_FAULT_ACTION_ENTER_FAULT);
    CHECK(!sensor_fault_measurement_due(&fault, 21999U));
    CHECK(sensor_fault_measurement_due(&fault, 22000U));
    return 0;
}

/*
 * Fail closed: argument and initialisation errors from weight_measure() are
 * handled like a sensor fault, so no dose is evaluated.
 */
static int test_programming_errors_count_as_fault(void)
{
    static const weight_measure_status_t statuses[] = {
        WEIGHT_MEASURE_SENSOR_FAULT, WEIGHT_MEASURE_INVALID_ARGUMENT,
        WEIGHT_MEASURE_NOT_INITIALIZED};
    sensor_fault_t fault;
    size_t index;

    for (index = 0U; index < (sizeof(statuses) / sizeof(statuses[0])); ++index)
    {
        sensor_fault_init(&fault);
        CHECK(sensor_fault_update(&fault, statuses[index], 0U) ==
              SENSOR_FAULT_ACTION_ENTER_FAULT);
        CHECK(!sensor_fault_evaluation_allowed(&fault));
    }
    return 0;
}

/* Missing arguments change nothing and never allow an evaluation. */
static int test_null_arguments(void)
{
    sensor_fault_init(NULL);
    CHECK(sensor_fault_update(NULL, WEIGHT_MEASURE_SENSOR_FAULT, 0U) ==
          SENSOR_FAULT_ACTION_NONE);
    CHECK(!sensor_fault_measurement_due(NULL, 0U));
    CHECK(!sensor_fault_evaluation_allowed(NULL));
    return 0;
}

/* Run the sensor-fault state machine tests on the host. */
int main(void)
{
    if ((test_initial_state() != 0) ||
        (test_valid_measurements_stay_normal() != 0) ||
        (test_fault_suspends_evaluation_and_waits_10_s() != 0) ||
        (test_failed_rechecks_report_once() != 0) ||
        (test_valid_recheck_resumes() != 0) ||
        (test_second_fault_is_reported_again() != 0) ||
        (test_programming_errors_count_as_fault() != 0) ||
        (test_null_arguments() != 0))
    {
        (void)fputs("sensor fault tests failed\n", stderr);
        return EXIT_FAILURE;
    }

    (void)puts("sensor fault tests passed");
    return EXIT_SUCCESS;
}
