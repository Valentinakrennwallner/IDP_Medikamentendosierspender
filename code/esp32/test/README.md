# ESP32 tests

`weight_test.c` checks the HX711 driver (`components/weight`) against mocked
ESP-IDF GPIO, timer, and FreeRTOS headers in `mocks/`: the 25-pulse read,
24-bit sign decoding, the 24-bit limit values, the -5 g to 100 g range, the
5 g change limit outside an intake window, and the 200 ms conversion timeout.
It is built and run by the host CMake project in `../raspberry-pi`
(`weight_tests`), like the heartbeat client tests.

Add host-testable tests for dose evaluation, plus hardware-in-the-loop tests
for HX711, NVS, LED, BLE, and deep-sleep behavior.
