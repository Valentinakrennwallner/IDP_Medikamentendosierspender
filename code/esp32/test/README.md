# ESP32 tests

`weight_test.c` checks the HX711 driver (`components/weight`) against mocked
ESP-IDF GPIO, timer, and FreeRTOS headers in `mocks/`: the 25-pulse read,
24-bit sign decoding, the 24-bit limit values, the -5 g to 100 g range, the
5 g change limit outside an intake window, the 200 ms conversion timeout, the
10-sample tare mean (and its abort on a faulty sample), the 17660 counts/g
calibration constant, and the 5-sample stability window (0.04 g spread,
short outliers, reset after faulty measurements). The GPIO mock models the
HX711 on GPIO 33/32 and delivers a queue of consecutive conversions, one per
read (a conversion can also never become ready, which makes the read time
out); the tare tests use the same queue.
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

`persistence_test.c` checks the calibration storage (`components/persistence`)
against an in-memory NVS (`mocks/nvs.h`, `mocks/nvs_flash.h`,
`mocks/esp_err.h`) in which every call can be made to fail: save and load of
the commissioning values, the exact 16-byte blob layout with CRC-32, rounding
to 0.001 count per gram and the tare range limits, missing data, a wrong
checksum, version, value or length (output left unchanged), invalid values
that must not be written, NVS errors on open, read, write and commit, NVS
init errors that are not repaired by erasing, and missing arguments
(Requirement 2.8). It is built and run by the host CMake project in
`../raspberry-pi` (`persistence_tests`). The GPIO mock takes `esp_err_t` and
`ESP_OK` from `mocks/esp_err.h`, as in ESP-IDF.

`mocks/esp_random.h` declares `esp_fill_random()` for the host build of the
mbedTLS secure channel backend used by `secure_channel_interop_tests` in
`../../raspberry-pi` (see `../../raspberry-pi/test/README.md`).

Add host-testable tests for dose evaluation, plus hardware-in-the-loop tests
for HX711, NVS, LED, BLE, and deep-sleep behavior.
