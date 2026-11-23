#include "weight_monitor.h"

#include <stddef.h>

/*
 * Start in the normal state with an empty stability window and no
 * reference weight. Design Document, Chapter 2, Section 2.1, and Chapter 6,
 * Section 6.1, Requirements 2.4 and 2.7.
 */
void weight_monitor_init(weight_monitor_t *monitor)
{
    if (monitor != NULL)
    {
        sensor_fault_init(&monitor->fault);
        weight_stability_reset(&monitor->stability);
        monitor->has_previous_measurement = false;
        monitor->previous_weight_grams = 0.0F;
    }
}

/*
 * True if a measurement is due: every cycle in the normal state, every 10 s
 * during a sensor fault. False on a missing argument.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.7.
 */
bool weight_monitor_measurement_due(const weight_monitor_t *monitor, uint64_t now_ms)
{
    bool due = false;

    if (monitor != NULL)
    {
        due = sensor_fault_measurement_due(&monitor->fault, now_ms);
    }
    return due;
}

/*
 * Return true and write the reference weight for the 5 g change check of
 * weight_measure() if a plausible measurement exists since the last fault;
 * otherwise return false (no reference, check skipped).
 * Design Document, Chapter 6, Section 6.1, Requirement 2.4.
 */
bool weight_monitor_previous_weight(const weight_monitor_t *monitor,
                                    float *previous_weight_grams)
{
    bool available = false;

    if ((monitor != NULL) && (previous_weight_grams != NULL))
    {
        available = monitor->has_previous_measurement;
        *previous_weight_grams = available ? monitor->previous_weight_grams : 0.0F;
    }
    return available;
}

/*
 * Process the result of one weight_measure() call and fill *output.
 * A faulty status empties the stability window and drops the reference
 * weight; a plausible measurement becomes the new reference and enters the
 * stability window. A stable weight is only passed on while the evaluation
 * is allowed (no sensor fault). Returns false and changes nothing on a
 * missing argument. Design Document, Chapter 3 (ESP32 flowchart), Chapter 4,
 * Section 4.1 (Hazard 2), and Chapter 6, Section 6.1, Requirements 2.4,
 * 2.5, 2.6 and 2.7.
 */
bool weight_monitor_process(weight_monitor_t *monitor,
                            weight_measure_status_t status,
                            const weight_measurement_t *measurement,
                            uint64_t now_ms,
                            weight_monitor_output_t *output)
{
    sensor_fault_action_t action;
    float stable_weight_grams = 0.0F;

    if ((monitor == NULL) || (measurement == NULL) || (output == NULL))
    {
        return false;
    }

    output->led_request = WEIGHT_MONITOR_LED_NONE;
    output->report_sensor_fault = false;
    output->sensor_recovered = false;
    output->stable_weight_available = false;
    output->stable_weight_grams = 0.0F;

    action = sensor_fault_update(&monitor->fault, status, now_ms);
    if (action == SENSOR_FAULT_ACTION_ENTER_FAULT)
    {
        output->led_request = WEIGHT_MONITOR_LED_FAULT_FLASH;
        output->report_sensor_fault = true;
    }
    else if (action == SENSOR_FAULT_ACTION_RESUME)
    {
        output->led_request = WEIGHT_MONITOR_LED_OFF;
        output->sensor_recovered = true;
    }
    else
    {
        /* SENSOR_FAULT_ACTION_NONE: no LED change. */
    }

    if (status != WEIGHT_MEASURE_OK)
    {
        /* No faulty value may count towards a stable weight (Req. 2.5). */
        weight_stability_reset(&monitor->stability);
        monitor->has_previous_measurement = false;
        monitor->previous_weight_grams = 0.0F;
    }
    else
    {
        monitor->has_previous_measurement = true;
        monitor->previous_weight_grams = measurement->grams;
        if (weight_stability_add(&monitor->stability, measurement->grams,
                                 &stable_weight_grams) &&
            sensor_fault_evaluation_allowed(&monitor->fault))
        {
            output->stable_weight_available = true;
            output->stable_weight_grams = stable_weight_grams;
        }
    }
    return true;
}
