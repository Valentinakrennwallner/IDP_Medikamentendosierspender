# ESP32 tests

`weight_test.c` checks the HX711 driver (`components/weight`) against mocked
ESP-IDF GPIO, timer, and FreeRTOS headers in `mocks/`: the 25-pulse read,
24-bit sign decoding, the 24-bit limit values, the -5 g to 100 g range, the
5 g change limit outside an intake window, the 200 ms conversion timeout, the
10-sample raw mean and tare (abort on a faulty sample, truncation towards
zero for negative values, not initialised, missing argument), the 17660 counts/g
calibration constant, and the 5-sample stability window (0.04 g spread,
short outliers, reset after faulty measurements). The GPIO mock models the
HX711 on GPIO 33/32 and delivers a queue of consecutive conversions, one per
read (a conversion can also never become ready, which makes the read time
out); the raw mean and tare tests use the same queue.
For `weight_measure()` the tests cover a valid first read, a valid first or
second repetition, three faulty reads (sensor fault, no fourth read, result
not written), a lasting change above 5 g outside an intake window, and
argument and initialisation errors that must not read the sensor
(Requirement 2.5).
It is built and run by the host CMake project in `../raspberry-pi`
(`weight_tests`), like the heartbeat client tests.

`sensor_fault_test.c` checks the sensor-fault state machine
(`components/sensor_fault`) with an injected millisecond clock: entering the
fault on a failed measurement, the 10 s recheck interval (not due 1 ms early),
a single fault report for repeated failed rechecks, resuming on a valid
recheck, a new report for a second fault, programming errors handled as a
fault, and missing arguments (Requirements 2.6 and 2.7). It is built and run
by the host CMake project in `../raspberry-pi` (`sensor_fault_tests`).

`weight_monitor_test.c` checks the per-cycle logic of the weight-measurement
thread (`components/weight_monitor`) with injected measurement results and an
injected millisecond clock: a stable weight only after 5 consistent
measurements, a short outlier, the last plausible measurement as reference of
the 5 g check, a sensor fault that asks once for the flashing red LED and the
fault report and drops the reference, the 10 s recheck, the recovery signalled once and
the LED switched off on resume, with 5 fresh measurements needed for the next stable weight, a
second fault reported again, programming errors handled as a fault, and
missing arguments (Requirements 2.4 to 2.7). `weight.c` is linked for the
stability window only; its HX711 functions are link-only stubs that abort if
called. It is built and run by the host CMake project in `../raspberry-pi`
(`weight_monitor_tests`). The FreeRTOS task (`main/weight_task.c`) and
`app_main()` are tested on the hardware.

`wall_clock_test.c` checks the ESP32 clock (`components/wall_clock`) with an
injected millisecond counter: the state "no time" before a time is set,
advancing with the counter, a new time on reconnect, the accepted range
2026-01-01 to 2099-12-31 UTC (an invalid time returns to "no time"), the
CET/CEST conversion at both summer-time switches, a leap day and the range
limits (expected values from the IANA zone Europe/Vienna), the current row
and the intake window at the Table 6 boundaries, the day end, the day
change at local midnight, a time set back to the previous day, a missing or
invalid table and missing arguments (Requirements 1.6, 2.4, 2.11, 3.2, 3.3).
It is built and run by the host CMake project in `../raspberry-pi`
(`wall_clock_tests`). The esp_timer binding (`wall_clock_esp32.c`) is tested
on the hardware.

`persistence_test.c` checks the calibration storage (`components/persistence`)
against an in-memory NVS (`mocks/nvs.h`, `mocks/nvs_flash.h`,
`mocks/esp_err.h`) in which every call can be made to fail: save and load of
the commissioning values, the exact 16-byte blob layout with CRC-32, rounding
to 0.001 count per gram and the tare range limits, missing data, a wrong
checksum, version, value or length (output left unchanged), invalid values
that must not be written, NVS errors on open, read, write and commit, NVS
init errors that are not repaired by erasing, the explicit erase with
`persistence_erase_and_init()` (and its errors), and missing arguments
(Requirement 2.8). It is built and run by the host CMake project in
`../raspberry-pi` (`persistence_tests`). The GPIO mock takes `esp_err_t` and
`ESP_OK` from `mocks/esp_err.h`, as in ESP-IDF.

`calibration_test.c` checks the serial commissioning calibration
(`components/calibration`, Requirement 2.8): parsing of the typed reference
weight (point or comma, at most three decimals, malformed input), the factor
computation with the values of the assembled load cell (1-euro coin, tare
267585, 17660 counts per gram), the 1 g to 100 g limits, invalid raw values,
no difference and an unstorable factor, and the dialog with a scripted
terminal, a fake sensor and a fake storage: a complete run (factor and control
weight shown, no tare or raw values), `q` at every prompt (nothing saved),
repeated questions for invalid weights and the confirmation, echo, Backspace
and CR/LF handling, sensor faults with retry, no valid factor, NVS erase only
after typing `ERASE`, save, reload and control measurement errors, serial I/O
errors and an incomplete I/O description. `weight_calibrate()` (UART binding)
is tested on the hardware. It is built and run by the host CMake project in
`../raspberry-pi` (`calibration_tests`). `mocks/driver/uart.h` only lets the
MISRA screening parse `calibration_uart.c` on the host.

`mocks/esp_random.h` declares `esp_fill_random()` for the host build of the
mbedTLS secure channel backend used by `secure_channel_interop_tests` in
`../../raspberry-pi` (see `../../raspberry-pi/test/README.md`).

Add host-testable tests for dose evaluation, plus hardware-in-the-loop tests
for HX711, NVS, LED, BLE, and deep-sleep behavior.
