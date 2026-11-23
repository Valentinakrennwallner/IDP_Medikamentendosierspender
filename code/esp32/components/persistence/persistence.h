#ifndef PERSISTENCE_H
#define PERSISTENCE_H

#include <stdbool.h>
#include <stdint.h>

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

/*
 * Pre-shared key (Requirement 4.7): 256 bit, the same size as
 * SECURE_CHANNEL_PSK_SIZE. Blob: version, key, CRC-32 (40 bytes), stored in
 * its own NVS namespace.
 */
#define PERSISTENCE_PSK_SIZE 32U
#define PERSISTENCE_PSK_VERSION UINT32_C(1)
#define PERSISTENCE_PSK_BLOB_SIZE 40U
#define PERSISTENCE_SECURITY_NAMESPACE "security"
#define PERSISTENCE_PSK_KEY "psk"

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
    /*
     * Factor zero, not finite or too large, or tare outside the 24-bit range;
     * or a pre-shared key that is obviously not random.
     */
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
 * Erase the whole NVS partition and initialise it again. This deletes the
 * calibration and every other NVS entry (later also the pre-shared key).
 * Only to be called after the user has explicitly confirmed it in the
 * commissioning calibration; never automatically.
 * Design Document, Chapter 2, Section 2.2, and Chapter 6, Section 6.1,
 * Requirement 2.8.
 */
persistence_result_t persistence_erase_and_init(void);

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

/*
 * Reject keys that are obviously not random: all bytes equal (including all
 * zero) or rising or falling by exactly 1 per byte. Design Document,
 * Chapter 6, Section 6.2, Requirements 4.7 and 4.9.
 */
bool persistence_psk_is_acceptable(const uint8_t psk[PERSISTENCE_PSK_SIZE]);

/*
 * Load the pre-shared key (loader for ble_secure_transport_start(), task 18).
 * psk is written only on PERSISTENCE_OK and overwritten with zeros on every
 * other result; without PERSISTENCE_OK BLE must stay disabled (fail closed).
 * Design Document, Chapter 6, Section 6.2, Requirements 4.7 and 4.9.
 */
persistence_result_t persistence_psk_load(uint8_t psk[PERSISTENCE_PSK_SIZE]);

/*
 * Validate and store the pre-shared key; nothing is written for an
 * unacceptable key. Design Document, Chapter 6, Section 6.2, Requirement 4.7.
 */
persistence_result_t persistence_psk_save(const uint8_t psk[PERSISTENCE_PSK_SIZE]);

#endif
