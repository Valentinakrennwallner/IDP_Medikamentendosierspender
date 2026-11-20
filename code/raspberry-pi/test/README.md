# Gateway tests

The heartbeat monitor and ESP32 heartbeat client have deterministic tests for
acknowledgements, timeouts, one-time alerting, end-of-day suppression, and
five-second reconnect intervals. Tests use callbacks and do not require BLE
hardware.

The secure-channel tests perform a real OpenSSL handshake, verify the PSK,
exercise protected messages (including the empty heartbeat and acknowledgement
payloads) in both directions, and reject replay and authentication-tag
tampering.

`secure_channel_interop_tests` (`secure_channel_interop_test.c`) checks the
gateway OpenSSL backend against the ESP32 mbedTLS backend
(`../../esp32/components/ble/secure_channel_mbedtls.c`) on the host: the Pi
handshake as client with the ESP32 as server, startup data and
acknowledgement, empty heartbeat and acknowledgement, replay rejection by the
ESP32, PSK mismatch, and tag tampering in both directions. Both backends
define the same public functions, so CMake compiles the mbedTLS backend with
compile definitions that rename them to `esp32_secure_channel_*`; neither
backend is changed for the test. `esp_fill_random()` is declared by the mock
`../../esp32/test/mocks/esp_random.h` and implemented in the test with
OpenSSL `RAND_bytes()`. The test needs the host mbedTLS 3.6 package
(`libmbedtls-dev`; ESP-IDF v5.5 bundles mbedTLS 3.6 as well) and is skipped
with a CMake warning when it is missing. It does not replace an
interoperability check on the real hardware.

The startup-transfer tests cover the schedule/time payload, rejection of
invalid schedules or unsynchronized time, and the size limit of one protected
message. The HX711 weighing tests live in `../../esp32/test`.

Add tests for schedule validation and target-weight calculation, time-sync
recovery, notification formatting, and secure protocol handling.

The `telegram_tests` target covers invalid credentials/arguments and refusal
to send when no trusted certificate time is available. It does not contact the
live Telegram service; end-to-end notification checks require a controlled
test bot and must never use real medication data.
