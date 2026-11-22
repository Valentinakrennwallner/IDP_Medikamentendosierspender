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

/*
 * Calibration factor of the assembled load cell, determined during
 * commissioning with a 1-euro coin (7.5 g) as reference weight and verified
 * with a 2-euro coin, a 10-cent coin and tablets to +-0.01 g. Documented
 * commissioning value only: at run time the factor is loaded from NVS
 * (persistence component) and there is no fallback to this constant.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
#define WEIGHT_COUNTS_PER_GRAM 17660.0F

/* Number of raw HX711 samples averaged for the tare offset. */
#define WEIGHT_TARE_SAMPLE_COUNT 10U

/*
 * A weight is only valid once this many consecutive plausible measurements
 * lie at most WEIGHT_STABLE_MAXIMUM_SPREAD_GRAMS apart (+-0.02 g).
 */
#define WEIGHT_STABLE_SAMPLE_COUNT 5U
#define WEIGHT_STABLE_MAXIMUM_SPREAD_GRAMS 0.04F

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

typedef struct
{
    float samples_grams[WEIGHT_STABLE_SAMPLE_COUNT];
    uint32_t sample_count;
    uint32_t next_index;
} weight_stability_t;

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

/*
 * Measure the tare offset of the empty container as the mean of
 * WEIGHT_TARE_SAMPLE_COUNT raw HX711 samples. Part of the commissioning
 * calibration; the caller stores the result in NVS. Any faulty sample aborts
 * the measurement and *tare_offset_counts is left unchanged.
 * Design Document, Chapter 2, Section 2.1, and Chapter 6, Section 6.1,
 * Requirements 2.1, 2.2 and 2.8.
 */
weight_result_t weight_measure_tare(int32_t *tare_offset_counts);

/*
 * Empty the stability window. Call at start-up and after every faulty
 * measurement. Design Document, Chapter 2, Section 2.1.
 */
void weight_stability_reset(weight_stability_t *stability);

/*
 * Add one plausible measurement to the stability window. Returns true and
 * writes the mean of the last WEIGHT_STABLE_SAMPLE_COUNT measurements to
 * *stable_weight_grams only if they lie at most
 * WEIGHT_STABLE_MAXIMUM_SPREAD_GRAMS apart; otherwise returns false so that
 * short outliers do not change the LED state. A non-finite weight resets the
 * window. Design Document, Chapter 1, Section 1.2, and Chapter 2,
 * Section 2.1.
 */
bool weight_stability_add(weight_stability_t *stability,
                          float weight_grams,
                          float *stable_weight_grams);

#endif
