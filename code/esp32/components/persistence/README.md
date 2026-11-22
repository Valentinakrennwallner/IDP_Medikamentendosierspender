# Persistence component

Stores the commissioning calibration of the weight measurement (tare offset
and counts per gram, `weight_calibration_t` from `components/weight`) in the
non-volatile storage (NVS) of the ESP32 with `nvs_set_blob()` and
`nvs_get_blob()`. This implements the storage part of Requirement 2.8
(Design Document, Chapter 2, Sections 2.1 and 2.2, Chapter 3, and Chapter 6,
Section 6.1, Requirement 2.8).

## API

- `persistence_init()` initialises the NVS partition with `nvs_flash_init()`.
  The partition is **never erased** here. A full or outdated partition
  (`ESP_ERR_NVS_NO_FREE_PAGES`, `ESP_ERR_NVS_NEW_VERSION_FOUND`) returns
  `PERSISTENCE_STORAGE_ERROR`, so the device stays fail-closed and the
  calibration and, later, the pre-shared key are never lost silently.
  Whether to erase is decided by the commissioning calibration (task 9).
- `persistence_calibration_load()` reads and validates the calibration.
  `*calibration` is written only on `PERSISTENCE_OK`.
- `persistence_calibration_save()` validates the calibration, writes the blob
  and calls `nvs_commit()`. Invalid values are rejected with
  `PERSISTENCE_INVALID_VALUE` and nothing is written.

## Fail-closed (Requirement 2.8)

Every result of `persistence_calibration_load()` other than `PERSISTENCE_OK`
means that no valid calibration is available: missing data
(`PERSISTENCE_MISSING`), wrong length, wrong version, wrong checksum, invalid
values or an NVS error. The caller must then not perform any evaluation and
must start the serial calibration. There is **no fallback** to a built-in
factor: `WEIGHT_COUNTS_PER_GRAM` (17660 counts per gram) in `weight.h` is
only the documented commissioning value of the assembled load cell.

## Stored data

NVS namespace `calibration`, key `weight_cal`, one blob of 16 bytes,
big-endian:

| Offset | Type | Content |
|---|---|---|
| 0 | `uint32` | layout version, currently 1 |
| 4 | `int32` | tare offset in HX711 counts |
| 8 | `int32` | factor in counts per kilogram (counts per gram x 1000) |
| 12 | `uint32` | CRC-32 (IEEE 802.3, as in zlib) over bytes 0 to 11 |

- The factor is stored as a fixed-point integer with a resolution of
  0.001 count per gram, so no float is reinterpreted as an integer (MISRA C).
  17660 counts per gram are stored as 17 660 000.
- Valid values: the factor must be finite, must not round to zero and must
  fit into `int32_t`. The tare offset must lie in the signed 24-bit HX711
  range without the two limit values that mark a faulty conversion
  (-8 388 607 to 8 388 606). No plausibility window around 17660 is checked,
  so a replaced load cell can be commissioned.
- The CRC-32 detects corrupted or foreign data. It is no protection against
  deliberate manipulation.

## Not part of this component

- Starting the serial calibration when no valid calibration is loaded, and
  computing the factor (task 9, `weight_calibrate()`).
- Calling `persistence_init()` and `persistence_calibration_load()` at
  start-up before any evaluation (task 21, `app_main`).
- Storing the pre-shared key (task 17); it can reuse `persistence_init()`.

## Tests

`code/esp32/test/persistence_test.c` (ctest `persistence_tests`, built by the
host CMake project in `code/raspberry-pi`) replaces the NVS API with an
in-memory store; see `code/esp32/test/README.md`.
