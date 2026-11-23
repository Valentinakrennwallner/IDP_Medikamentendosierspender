#ifndef WEIGHT_TASK_H
#define WEIGHT_TASK_H

/*
 * Weight-measurement thread of the ESP32: measures every
 * WEIGHT_TASK_PERIOD_MS, handles sensor faults (flashing red LED, 10 s
 * recheck) and publishes each new stable weight for the dose evaluation.
 * Design Document, Chapter 2, Sections 2.1 and 2.3, and Chapter 3.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "weight.h"

/*
 * Measurement period. Not specified in the Design Document; agreed by the
 * group: 1 s, so a stable weight (5 measurements) is available after
 * about 5 s.
 */
#define WEIGHT_TASK_PERIOD_MS UINT32_C(1000)

/* One stable weight for the dose evaluation (health data: never log it). */
typedef struct
{
    float weight_grams;
    /* Monotonic time of the last measurement of the stable window. */
    uint64_t measured_at_ms;
} weight_task_sample_t;

/*
 * Initialise the HX711 and start the periodic measurement timer and the
 * weight-measurement thread with the loaded calibration. Must be called
 * only once, after a valid calibration was loaded from NVS (Requirement
 * 2.8). Design Document, Chapter 2, Sections 2.1 and 2.3.
 */
esp_err_t weight_task_start(const weight_calibration_t *calibration);

/*
 * Interface for the dose evaluation (still open in task 21): wait at most
 * timeout_ms for the newest stable weight and remove it from the mailbox.
 * Returns false on timeout, before weight_task_start() or on a missing
 * argument. During a sensor fault no weight is delivered, and the mailbox
 * is emptied when a fault starts, so the evaluation cannot use a weight
 * from before the fault. Design Document, Chapter 3 (ESP32 flowchart), and
 * Chapter 6, Section 6.1, Requirement 2.6.
 */
bool weight_task_get_stable(weight_task_sample_t *sample, uint32_t timeout_ms);

#endif
