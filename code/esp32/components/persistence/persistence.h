#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include "weight.h"

/* Version of the calibration blob layout stored in NVS. */
#define PERSISTENCE_CALIBRATION_VERSION UINT32_C(1)

/* Size of the calibration blob in bytes (version, tare, factor, CRC-32). */
#define PERSISTENCE_CALIBRATION_BLOB_SIZE 16U

/* NVS namespace and key of the calibration blob. */
#define PERSISTENCE_NAMESPACE "calibration"
#define PERSISTENCE_CALIBRATION_KEY "weight_cal"

/*
 * Valid tare offsets: the signed 24-bit HX711 range without the two limit
 * values 0x7FFFFF and 0x800000, which mark a faulty conversion.
 */
#define PERSISTENCE_TARE_MINIMUM_COUNTS (-8388607)
#define PERSISTENCE_TARE_MAXIMUM_COUNTS 8388606

typedef enum
{
    PERSISTENCE_OK = 0,
    /* No calibration stored yet (never commissioned). */
    PERSISTENCE_MISSING = 1,
    /* Stored blob has the wrong length. */
    PERSISTENCE_INVALID_SIZE = 2,
    /* Stored blob has an unknown layout version. */
    PERSISTENCE_INVALID_VERSION = 3,
    /* Stored blob fails the CRC-32 check. */
    PERSISTENCE_INVALID_CHECKSUM = 4,
    /* Factor zero, not finite or too large, or tare outside the 24-bit range. */
    PERSISTENCE_INVALID_VALUE = 5,
    /* Missing argument. */
    PERSISTENCE_INVALID_ARGUMENT = 6,
    /* Any other NVS error (including a full or outdated NVS partition). */
    PERSISTENCE_STORAGE_ERROR = 7
} persistence_result_t;

/*
 * Initialise the NVS flash partition. The partition is never erased here: a
 * full or outdated partition returns PERSISTENCE_STORAGE_ERROR, so the
 * device stays fail-closed and the commissioning calibration decides about
 * erasing. Design Document, Chapter 2, Section 2.2, and Chapter 6,
 * Section 6.1, Requirement 2.8.
 */
persistence_result_t persistence_init(void);

/*
 * Load the commissioning calibration (tare offset and counts per gram) from
 * NVS. *calibration is written only on PERSISTENCE_OK. Every other result
 * means that no valid calibration is available: the caller must not perform
 * any evaluation and must start the serial calibration (fail-closed). There
 * is no fallback to a built-in calibration factor.
 * Design Document, Chapter 2, Sections 2.1 and 2.2, Chapter 3, and
 * Chapter 6, Section 6.1, Requirement 2.8.
 */
persistence_result_t persistence_calibration_load(weight_calibration_t *calibration);

/*
 * Validate the calibration and store it in NVS (blob plus commit). Invalid
 * values are rejected with PERSISTENCE_INVALID_VALUE and nothing is written.
 * The counts per gram are stored with a resolution of 0.001 count per gram.
 * Design Document, Chapter 2, Sections 2.1 and 2.2, and Chapter 6,
 * Section 6.1, Requirement 2.8.
 */
persistence_result_t persistence_calibration_save(const weight_calibration_t *calibration);

#endif
