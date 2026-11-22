#ifndef CALIBRATION_H
#define CALIBRATION_H

/*
 * Serial commissioning calibration of the weight measurement. Logically part
 * of the module "Weight measurement" (Design Document, Chapter 2,
 * Section 2.1, function weight_calibrate()); it is a separate ESP-IDF
 * component only because it needs the persistence component, which itself
 * depends on the weight component.
 */

#include <stdbool.h>
#include <stdint.h>

#include "persistence.h"
#include "weight.h"

/* Accepted reference weight: at least 1.000 g, at most the load cell capacity. */
#define CALIBRATION_MINIMUM_REFERENCE_MILLIGRAMS 1000U
#define CALIBRATION_MAXIMUM_REFERENCE_MILLIGRAMS 100000U

/* Longest accepted input line (characters without the line end). */
#define CALIBRATION_MAXIMUM_LINE_LENGTH 16U

typedef enum
{
    /* Calibration saved, verified by reloading it, control measurement valid. */
    CALIBRATION_OK = 0,
    /* Cancelled by the user (q); nothing was saved. */
    CALIBRATION_CANCELLED = 1,
    /* Sensor not usable; nothing was saved, or the control measurement failed. */
    CALIBRATION_SENSOR_FAULT = 2,
    /* The measured values give no valid factor; nothing was saved. */
    CALIBRATION_INVALID_FACTOR = 3,
    /* NVS could not be initialised, erased or written. */
    CALIBRATION_STORAGE_ERROR = 4,
    /* The saved calibration could not be reloaded unchanged. */
    CALIBRATION_VERIFY_FAILED = 5,
    /* Missing or incomplete I/O description. */
    CALIBRATION_INVALID_ARGUMENT = 6,
    /* Reading from or writing to the serial interface failed. */
    CALIBRATION_IO_ERROR = 7
} calibration_result_t;

/*
 * Serial interface, sensor and storage used by the dialog. weight_calibrate()
 * connects them to the UART driver, the weight component and the
 * persistence component; the host tests connect them to scripted fakes.
 */
typedef struct
{
    /* Wait for one received byte; false on a read error. */
    bool (*read_byte)(uint8_t *byte);
    /* Send a zero-terminated text; false on a write error. */
    bool (*write_text)(const char *text);
    persistence_result_t (*storage_init)(void);
    persistence_result_t (*storage_erase_and_init)(void);
    weight_result_t (*measure_tare)(int32_t *tare_offset_counts);
    weight_result_t (*measure_raw_mean)(int32_t *mean_counts);
    persistence_result_t (*save)(const weight_calibration_t *calibration);
    persistence_result_t (*load)(weight_calibration_t *calibration);
    /* One calibrated measurement (intake window treated as active). */
    weight_measure_status_t (*measure)(const weight_calibration_t *calibration,
                                       weight_measurement_t *measurement);
} calibration_io_t;

/*
 * Parse a weight in grams typed by the user ("7.5", "7,5", "20", at most
 * three decimals) into milligrams without floating point. Returns false for
 * empty or malformed text; the range is checked by the caller.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
bool calibration_parse_grams(const char *text, uint32_t *milligrams);

/*
 * Compute counts per gram from the raw mean of the empty container, the raw
 * mean with the reference weight and the reference weight. Returns false
 * (and leaves *counts_per_gram unchanged) for a reference weight outside
 * 1.000 g to 100.000 g, raw values outside the valid 24-bit HX711 range, no
 * difference between the raw values, or a factor that cannot be stored.
 * Design Document, Chapter 2, Section 2.1, and Chapter 6, Section 6.1,
 * Requirement 2.8.
 */
bool calibration_compute_counts_per_gram(int32_t tare_counts,
                                         int32_t reference_counts,
                                         uint32_t reference_milligrams,
                                         float *counts_per_gram);

/*
 * Run the commissioning dialog over io: optional NVS erase after explicit
 * confirmation, tare of the empty tray, reference weight, factor, save,
 * reload and one control measurement. Entering q at any prompt cancels
 * without saving.
 * Design Document, Chapter 2, Sections 2.1 and 2.2, and Chapter 6,
 * Section 6.1, Requirement 2.8.
 */
calibration_result_t calibration_dialog_run(const calibration_io_t *io);

/*
 * Run the commissioning calibration on the serial console (UART 0) with the
 * HX711 and NVS. Blocks until the dialog ends; there is no inactivity
 * timeout, because without a valid calibration no evaluation takes place.
 * Design Document, Chapter 2, Sections 2.1 and 2.2, and Chapter 6,
 * Section 6.1, Requirement 2.8.
 */
calibration_result_t weight_calibrate(void);

#endif
