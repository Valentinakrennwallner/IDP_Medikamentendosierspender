# ESP32 tests

`weight_test.c` checks the HX711 driver (`components/weight`) against mocked
ESP-IDF GPIO, timer, and FreeRTOS headers in `mocks/`: the 25-pulse read,
24-bit sign decoding, the 24-bit limit values, the -5 g to 100 g range, the
5 g change limit outside an intake window, and the 200 ms conversion timeout.
For `weight_measure()` the mock delivers a sequence of conversions, one per
read (a conversion can also never become ready, which makes the read time
out). The tests cover a valid first read, a valid first or second repetition,
three faulty reads (sensor fault, no fourth read, result not written), a
lasting change above 5 g outside an intake window, and argument and
initialisation errors that must not read the sensor (Requirement 2.5).
It is built and run by the host CMake project in `../raspberry-pi`
(`weight_tests`), like the heartbeat client tests.

`mocks/esp_random.h` declares `esp_fill_random()` for the host build of the
mbedTLS secure channel backend used by `secure_channel_interop_tests` in
`../../raspberry-pi` (see `../../raspberry-pi/test/README.md`).

Add host-testable tests for dose evaluation, plus hardware-in-the-loop tests
for HX711, NVS, LED, BLE, and deep-sleep behavior.
