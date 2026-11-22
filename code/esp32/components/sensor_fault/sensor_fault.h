#ifndef SENSOR_FAULT_H
#define SENSOR_FAULT_H

#include <stdbool.h>
#include <stdint.h>

#include "weight.h"

/* Interval between sensor rechecks during a sensor fault (Requirement 2.7). */
#define SENSOR_FAULT_RECHECK_INTERVAL_MS UINT64_C(10000)

typedef enum
{
    /* Measurements are valid; dose evaluation may run. */
    SENSOR_FAULT_STATE_NORMAL = 0,
    /* Sensor fault: evaluation suspended, sensor rechecked every 10 s. */
    SENSOR_FAULT_STATE_FAULT = 1
} sensor_fault_state_t;

/* What the caller must do after sensor_fault_update(). */
typedef enum
{
    /* Nothing changes. */
    SENSOR_FAULT_ACTION_NONE = 0,
    /*
     * A sensor fault starts: suspend the dose evaluation, set the LED to
     * LED_STATUS_FAULT_FLASH, and report REPORT_STATUS_SENSOR_FAULT to the
     * Raspberry Pi. Returned once per fault, not on every recheck.
     */
    SENSOR_FAULT_ACTION_ENTER_FAULT = 1,
    /* Valid values again: resume the dose evaluation, which sets the LED. */
    SENSOR_FAULT_ACTION_RESUME = 2
} sensor_fault_action_t;

typedef struct
{
    sensor_fault_state_t state;
    /* Monotonic time of the next recheck; only used in the fault state. */
    uint64_t next_check_ms;
} sensor_fault_t;

/*
 * Start in the normal state.
 * Design Document, Chapter 6, Section 6.1, Requirements 2.6 and 2.7.
 */
void sensor_fault_init(sensor_fault_t *fault);

/*
 * Feed the result of one weight_measure() call taken at now_ms (a monotonic
 * clock in milliseconds, injected by the caller) and return the action the
 * caller must take. Every status other than WEIGHT_MEASURE_OK is treated as a
 * sensor fault, so a programming error also stops the evaluation.
 * Design Document, Chapter 4, Section 4.1 (Hazard 2), and Chapter 6,
 * Section 6.1, Requirements 2.6 and 2.7.
 */
sensor_fault_action_t sensor_fault_update(sensor_fault_t *fault,
                                          weight_measure_status_t status,
                                          uint64_t now_ms);

/*
 * Return true if a measurement is due at now_ms: always in the normal state
 * (the periodic measurement timer sets the pace), and in the fault state only
 * once the 10 s recheck interval has elapsed.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.7.
 */
bool sensor_fault_measurement_due(const sensor_fault_t *fault, uint64_t now_ms);

/*
 * Return true if the dose evaluation may use the current measurement, that
 * is, if no sensor fault is active. A missing argument returns false.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.6.
 */
bool sensor_fault_evaluation_allowed(const sensor_fault_t *fault);

#endif
