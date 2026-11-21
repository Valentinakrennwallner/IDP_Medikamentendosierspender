#ifndef WEIGHT_H
#define WEIGHT_H

#include <stdbool.h>
#include <stdint.h>

#define WEIGHT_READ_TIMEOUT_MS UINT32_C(200)
#define WEIGHT_MINIMUM_GRAMS (-5.0F)
#define WEIGHT_MAXIMUM_GRAMS 100.0F
#define WEIGHT_MAXIMUM_OUTSIDE_WINDOW_CHANGE_GRAMS 5.0F
/* Additional reads after a faulty measurement (Requirement 2.5). */
#define WEIGHT_MEASURE_RETRIES 2U

typedef enum
{
    WEIGHT_RESULT_OK = 0,
    WEIGHT_RESULT_INVALID_ARGUMENT = -1,
    WEIGHT_RESULT_NOT_INITIALIZED = -2,
    WEIGHT_RESULT_TIMEOUT = -3,
    WEIGHT_RESULT_SENSOR_FAULT = -4,
    WEIGHT_RESULT_IMPLAUSIBLE = -5
} weight_result_t;

/* Outcome of a measurement including the retries of Requirement 2.5. */
typedef enum
{
    WEIGHT_MEASURE_OK = 0,
    /* The first read and both repeated reads were faulty. */
    WEIGHT_MEASURE_SENSOR_FAULT = 1,
    /* Programming error: missing argument or invalid calibration. */
    WEIGHT_MEASURE_INVALID_ARGUMENT = 2,
    /* Programming error: weight_init() has not succeeded. */
    WEIGHT_MEASURE_NOT_INITIALIZED = 3
} weight_measure_status_t;

typedef struct
{
    int32_t tare_offset_counts;
    float counts_per_gram;
} weight_calibration_t;

typedef struct
{
    int32_t raw_counts;
    float grams;
} weight_measurement_t;

/*
 * Configure the HX711 data and clock GPIOs.
 * Design Document, Chapter 2, Sections 2.1 and 2.4.
 */
weight_result_t weight_init(void);

/*
 * Read, calibrate, and check one HX711 measurement. Call serially from the
 * weight-measurement task; sensor readiness times out after 200 ms.
 * Design Document, Chapter 2, Sections 2.1 and 2.3, and Chapter 6,
 * Section 6.1, Requirements 2.1-2.4.
 */
weight_result_t weight_read(const weight_calibration_t *calibration,
                            bool intake_window_active,
                            bool has_previous_measurement,
                            float previous_weight_grams,
                            weight_measurement_t *measurement);

/*
 * Take one measurement with weight_read(). A faulty read (timeout, HX711
 * limit value, or implausible weight) is repeated up to two more times; a
 * sensor fault is reported only if both repeated reads are faulty as well.
 * Invalid arguments and a missing initialisation are not retried.
 * *measurement is written only when WEIGHT_MEASURE_OK is returned.
 * Design Document, Chapter 2, Section 2.1, Chapter 4, Section 4.1
 * (Hazard 2), and Chapter 6, Section 6.1, Requirement 2.5.
 */
weight_measure_status_t weight_measure(const weight_calibration_t *calibration,
                                       bool intake_window_active,
                                       bool has_previous_measurement,
                                       float previous_weight_grams,
                                       weight_measurement_t *measurement);

#endif
