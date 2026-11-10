#ifndef WEIGHT_H
#define WEIGHT_H

#include <stdbool.h>
#include <stdint.h>

#define WEIGHT_READ_TIMEOUT_MS UINT32_C(200)
#define WEIGHT_MINIMUM_GRAMS (-5.0F)
#define WEIGHT_MAXIMUM_GRAMS 100.0F
#define WEIGHT_MAXIMUM_OUTSIDE_WINDOW_CHANGE_GRAMS 5.0F

typedef enum
{
    WEIGHT_RESULT_OK = 0,
    WEIGHT_RESULT_INVALID_ARGUMENT = -1,
    WEIGHT_RESULT_NOT_INITIALIZED = -2,
    WEIGHT_RESULT_TIMEOUT = -3,
    WEIGHT_RESULT_SENSOR_FAULT = -4,
    WEIGHT_RESULT_IMPLAUSIBLE = -5
} weight_result_t;

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

#endif
