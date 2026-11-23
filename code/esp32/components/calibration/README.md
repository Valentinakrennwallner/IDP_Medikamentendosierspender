# Calibration component

Serial commissioning calibration of the weight measurement (Design Document,
Chapter 6, Section 6.1, Requirement 2.8): the tare offset of the empty
container and the calibration factor are determined once with a reference
weight and stored in NVS.

This component is **part of the module "Weight measurement"** (Design
Document, Chapter 2, Section 2.1, which lists `weight_calibrate()` among the
functions of that module). It is a separate ESP-IDF component only for a
build reason: it needs the `persistence` component, and `persistence` itself
depends on `weight` (for `weight_calibration_t`). Putting
`weight_calibrate()` into `weight` would create a circular component
dependency. This is a split of the code, not a change of the design.

## Files

- `calibration.c`: pure logic without hardware access, host-tested.
  - `calibration_parse_grams()` parses the typed reference weight ("7.5",
    "7,5", "20", at most three decimals) into milligrams without floating
    point.
  - `calibration_compute_counts_per_gram()` computes
    `(reference raw - tare raw) / reference weight`. It rejects a reference
    weight outside 1.000 g to 100.000 g, raw values outside the valid 24-bit
    HX711 range, a difference of 0, and factors that
    `persistence_calibration_save()` cannot store. A negative factor is
    valid, as in `weight_read()`.
  - `calibration_dialog_run()` runs the dialog over `calibration_io_t`
    (serial I/O, sensor and storage as function pointers).
- `calibration_uart.c`: `weight_calibrate()` connects the dialog to the
  console UART 0 (ESP-IDF UART driver, 115200 baud from sdkconfig; no
  `stdio`, MISRA C Rule 21.6; binding in `components/serial_line`), the
  HX711 (`weight_init()`, `weight_measure_tare()`,
  `weight_measure_raw_mean()`, `weight_measure()`) and NVS (`persistence_*`).
  The UART driver is installed if needed and stays installed.
- The line input (echo, Backspace, CR/LF) and the UART functions are in
  `components/serial_line` and are shared with the pre-shared key dialog
  (task 17).

## Dialog

Entering `q` (or `Q`) at any prompt cancels **without saving anything**.
Lines end with CR, LF or CR LF; typed characters are echoed, Backspace and
Delete work, lines longer than 16 characters are rejected. There is no
inactivity timeout: without a valid calibration the ESP32 performs no
evaluation anyway (Requirement 2.8), so waiting is safe.

1. `persistence_init()`. If NVS is full or outdated
   (`PERSISTENCE_STORAGE_ERROR`), the dialog explains that erasing deletes the
   calibration **and the pre-shared key** and erases the NVS partition with
   `persistence_erase_and_init()` only if the user types exactly `ERASE`.
   Anything else cancels. NVS is never erased automatically.
2. Empty the tray, press Enter: tare with `weight_measure_tare()`.
3. Place the reference weight and type its weight in grams: raw mean with
   `weight_measure_raw_mean()`.
   - After a sensor fault in step 2 or 3, Enter retries and `q` cancels.
   - Invalid or out-of-range weights are asked again.
4. Compute the factor. Without a valid factor (for example no reference
   weight on the tray), nothing is saved.
5. Show the factor and ask: `y` saves, `q` cancels.
6. `persistence_calibration_save()`, then `persistence_calibration_load()`.
   The reloaded values must match (same tare, factor within the storage
   resolution of 0.001 count per gram). Then one control measurement with
   `weight_measure()` using the reloaded calibration (intake window treated
   as active, so the 5 g rule does not reject the reference weight).

Results: `CALIBRATION_OK`, `CANCELLED`, `SENSOR_FAULT` (nothing saved, or
saved but the control measurement failed), `INVALID_FACTOR`,
`STORAGE_ERROR`, `VERIFY_FAILED`, `INVALID_ARGUMENT`, `IO_ERROR`.

## Reference weight

The hard limits are **1.000 g to 100.000 g** (100 g is the capacity of the
load cell, `WEIGHT_MAXIMUM_GRAMS`). A reference weight of **at least about
5 g** is recommended, for example a 1-euro coin (7.5 g) or a 2-euro coin
(8.5 g). The noise of the raw mean and the tolerance of the reference weight
are roughly constant in absolute terms, so their relative error, and with it
the error of the factor, shrinks with a heavier reference weight. With 1 g a
noise of a few hundredths of a gram would already distort the factor by
several percent.

## Output on the serial interface

The dialog shows only the computed factor (counts per gram) and the control
weight in grams. No tare or raw values are shown. During commissioning only
the reference weight lies on the tray, so this is no health data
(Design Document, Chapter 5, Section 5.1, Threat 3, and Chapter 6,
Section 6.2, Requirement 4.4). The output is part of the interactive dialog
and is not written with the ESP-IDF logging functions.

## Not part of this component

Calling `weight_calibrate()` at start-up when `persistence_calibration_load()`
does not return `PERSISTENCE_OK`, before any evaluation, is done in
`main/app_main.c` (task 21): after success the ESP32 restarts, after a
cancellation or error the red LED flashes and the dialog is offered again.

## Tests

`code/esp32/test/calibration_test.c` (ctest `calibration_tests`, built by the
host CMake project in `code/raspberry-pi`); see `code/esp32/test/README.md`.
`weight_calibrate()` itself needs the UART driver and is tested on the
hardware.
