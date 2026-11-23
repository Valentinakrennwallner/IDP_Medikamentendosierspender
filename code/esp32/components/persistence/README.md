# Persistence component

Stores the commissioning calibration of the weight measurement (tare offset
and counts per gram, `weight_calibration_t` from `components/weight`) and
the 256-bit pre-shared key in the non-volatile storage (NVS) of the ESP32
with `nvs_set_blob()` and `nvs_get_blob()`. This implements the storage part
of Requirements 2.8 and 4.7 (Design Document, Chapter 2, Sections 2.1 and
2.2, Chapter 3, and Chapter 6, Section 6.1, Requirement 2.8, and
Section 6.2, Requirement 4.7).

## API

- `persistence_init()` initialises the NVS partition with `nvs_flash_init()`.
  The partition is **never erased** here. A full or outdated partition
  (`ESP_ERR_NVS_NO_FREE_PAGES`, `ESP_ERR_NVS_NEW_VERSION_FOUND`) returns
  `PERSISTENCE_STORAGE_ERROR`, so the device stays fail-closed and the
  calibration and the pre-shared key are never lost silently.
- `persistence_erase_and_init()` erases the whole NVS partition
  (`nvs_flash_erase()`) and initialises it again. This deletes the
  calibration, the pre-shared key and every other NVS entry. It
  is only called by the commissioning calibration (`weight_calibrate()`,
  component `calibration`) after the user has explicitly typed `ERASE`.
- `persistence_calibration_load()` reads and validates the calibration.
  `*calibration` is written only on `PERSISTENCE_OK`.
- `persistence_calibration_save()` validates the calibration, writes the blob
  and calls `nvs_commit()`. Invalid values are rejected with
  `PERSISTENCE_INVALID_VALUE` and nothing is written.
- `persistence_psk_save()` / `persistence_psk_load()` store and load the
  pre-shared key (see below). `persistence_psk_load()` is the loader for
  `ble_secure_transport_start()` (task 18).
- `persistence_psk_is_acceptable()` rejects obviously non-random keys.

Both kinds of data use the same internal NVS helpers (`read_blob()`,
`write_blob()`), so the error mapping is identical.

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

## Pre-shared key (Requirement 4.7)

NVS namespace `security`, key `psk`, one blob of 40 bytes, big-endian:

| Offset | Type | Content |
|---|---|---|
| 0 | `uint32` | layout version, currently 1 |
| 4 | 32 bytes | pre-shared key |
| 36 | `uint32` | CRC-32 (IEEE 802.3) over bytes 0 to 35 |

- **Rejected keys** (when saving and when loading): all 32 bytes equal
  (including all zero and all `0xFF`) and sequences that rise or fall by
  exactly 1 per byte (modulo 256), e.g. `00 01 02 … 1F`. Only obviously
  non-random keys are caught; the key must be generated randomly (see
  `components/psk_commissioning`).
- **Fail closed:** `persistence_psk_load()` writes the key only on
  `PERSISTENCE_OK`. On every other result (missing, wrong length, version or
  checksum, rejected key, NVS error) the output is overwritten with zeros,
  so a caller can never use a stale or invalid key. Without
  `PERSISTENCE_OK` BLE must stay disabled.
- **Overwriting copies:** the local blob that holds the key is overwritten
  with `mbedtls_platform_zeroize()` before returning. A plain `memset()` on a
  buffer that is not read afterwards is a dead store that the compiler may
  remove (C "as-if" rule); `mbedtls_platform_zeroize()` is guaranteed not to
  be optimised away, and the BLE transport already uses it. The host tests
  use a stand-in with volatile writes (`test/mocks_mbedtls`), so they do not
  need an installed Mbed TLS.
- The key is never logged. NVS is not encrypted: a person with physical
  access can read the key (Design Document, Chapter 5, Section 5.2,
  Threat 1, no countermeasure in this project).

## Not part of this component

- Computing the factor and the serial dialog (`weight_calibrate()` in the
  component `calibration`). Starting it when no valid calibration is loaded
  is done in `main/app_main.c` (task 21).
- Calling `persistence_init()` and `persistence_calibration_load()` at
  start-up before any evaluation (`main/app_main.c`, task 21). Every result
  other than `PERSISTENCE_OK` counts as "no calibration".
- The serial dialog for the pre-shared key (`psk_commission()` in the
  component `psk_commissioning`, task 17) and loading the key before the BLE
  start (tasks 18, 21).

## Tests

`code/esp32/test/persistence_test.c` (ctest `persistence_tests`, built by the
host CMake project in `code/raspberry-pi`) replaces the NVS API with an
in-memory store; see `code/esp32/test/README.md`.
