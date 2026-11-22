#include "sensor_fault.h"

#include <stddef.h>

/*
 * Put the state machine into the normal state: no fault, evaluation allowed.
 * Design Document, Chapter 6, Section 6.1, Requirements 2.6 and 2.7.
 */
void sensor_fault_init(sensor_fault_t *fault)
{
    if (fault != NULL)
    {
        fault->state = SENSOR_FAULT_STATE_NORMAL;
        fault->next_check_ms = 0U;
    }
}

/*
 * Advance the sensor-fault state machine with one measurement result.
 * Normal -> fault on a failed measurement (enter once: flash red, report).
 * Fault -> fault on a failed recheck (schedule the next recheck in 10 s).
 * Fault -> normal on a valid recheck (resume the evaluation).
 * A missing argument returns SENSOR_FAULT_ACTION_NONE and changes nothing.
 * Design Document, Chapter 4, Section 4.1 (Hazard 2), and Chapter 6,
 * Section 6.1, Requirements 2.6 and 2.7.
 */
sensor_fault_action_t sensor_fault_update(sensor_fault_t *fault,
                                          weight_measure_status_t status,
                                          uint64_t now_ms)
{
    sensor_fault_action_t action = SENSOR_FAULT_ACTION_NONE;
    bool measurement_valid = (status == WEIGHT_MEASURE_OK);

    if (fault == NULL)
    {
        action = SENSOR_FAULT_ACTION_NONE;
    }
    else if (fault->state == SENSOR_FAULT_STATE_NORMAL)
    {
        if (!measurement_valid)
        {
            fault->state = SENSOR_FAULT_STATE_FAULT;
            fault->next_check_ms = now_ms + SENSOR_FAULT_RECHECK_INTERVAL_MS;
            action = SENSOR_FAULT_ACTION_ENTER_FAULT;
        }
    }
    else if (measurement_valid)
    {
        fault->state = SENSOR_FAULT_STATE_NORMAL;
        fault->next_check_ms = 0U;
        action = SENSOR_FAULT_ACTION_RESUME;
    }
    else
    {
        fault->next_check_ms = now_ms + SENSOR_FAULT_RECHECK_INTERVAL_MS;
    }

    return action;
}

/*
 * Tell the measurement task whether to measure now. In the fault state the
 * sensor is only polled every SENSOR_FAULT_RECHECK_INTERVAL_MS. A missing
 * argument returns false.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.7.
 */
bool sensor_fault_measurement_due(const sensor_fault_t *fault, uint64_t now_ms)
{
    bool due = false;

    if (fault == NULL)
    {
        due = false;
    }
    else if (fault->state == SENSOR_FAULT_STATE_NORMAL)
    {
        due = true;
    }
    else
    {
        due = (now_ms >= fault->next_check_ms);
    }

    return due;
}

/*
 * Allow the dose evaluation only in the normal state, so that no dose is
 * evaluated from a faulty sensor (fail closed on a missing argument).
 * Design Document, Chapter 6, Section 6.1, Requirement 2.6.
 */
bool sensor_fault_evaluation_allowed(const sensor_fault_t *fault)
{
    bool allowed = false;

    if (fault != NULL)
    {
        allowed = (fault->state == SENSOR_FAULT_STATE_NORMAL);
    }

    return allowed;
}
