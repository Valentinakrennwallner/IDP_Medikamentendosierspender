#ifndef WEIGHT_MONITOR_H
#define WEIGHT_MONITOR_H

/*
 * Per-cycle logic of the weight-measurement thread, without FreeRTOS or
 * hardware dependencies: combines the sensor-fault state machine
 * (components/sensor_fault), the stability window (components/weight) and
 * the reference weight of the 5 g change check. The FreeRTOS task in
 * main/weight_task.c calls weight_measure() and passes the result here.
 * Design Document, Chapter 2, Section 2.1 (weight measurement module) and
 * Section 2.3 (Table 2, thread priorities), and Chapter 3 (ESP32 flowchart).
 */

#include <stdbool.h>
#include <stdint.h>

#include "sensor_fault.h"
#include "weight.h"

/* LED change the caller must make after weight_monitor_process(). */
typedef enum
{
    /* Leave the LED as it is. */
    WEIGHT_MONITOR_LED_NONE = 0,
    /* A sensor fault starts: LED_STATUS_FAULT_FLASH (Requirement 2.6). */
    WEIGHT_MONITOR_LED_FAULT_FLASH = 1,
    /*
     * Valid values again: LED_STATUS_OFF until the dose evaluation sets the
     * LED from the next stable weight.
     */
    WEIGHT_MONITOR_LED_OFF = 2
} weight_monitor_led_t;

typedef struct
{
    sensor_fault_t fault;
    weight_stability_t stability;
    /* Last plausible measurement, reference of the 5 g check (Req. 2.4). */
    bool has_previous_measurement;
    float previous_weight_grams;
} weight_monitor_t;

/* Result of one measurement cycle for the caller. */
typedef struct
{
    weight_monitor_led_t led_request;
    /* True once per fault: report the sensor fault to the Raspberry Pi. */
    bool report_sensor_fault;
    /* True once when valid values return after a fault (evaluation resumes). */
    bool sensor_recovered;
    /* True if a new stable weight may be passed to the dose evaluation. */
    bool stable_weight_available;
    /* Only valid if stable_weight_available is true. */
    float stable_weight_grams;
} weight_monitor_output_t;

/*
 * Start in the normal state with an empty stability window and no
 * reference weight. Design Document, Chapter 2, Section 2.1, and Chapter 6,
 * Section 6.1, Requirements 2.4 and 2.7.
 */
void weight_monitor_init(weight_monitor_t *monitor);

/*
 * True if a measurement is due: every cycle in the normal state, every 10 s
 * during a sensor fault. False on a missing argument.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.7.
 */
bool weight_monitor_measurement_due(const weight_monitor_t *monitor, uint64_t now_ms);

/*
 * Return true and write the reference weight for the 5 g change check of
 * weight_measure() if a plausible measurement exists since the last fault;
 * otherwise return false (no reference, check skipped).
 * Design Document, Chapter 6, Section 6.1, Requirement 2.4.
 */
bool weight_monitor_previous_weight(const weight_monitor_t *monitor,
                                    float *previous_weight_grams);

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
                            weight_monitor_output_t *output);

#endif
