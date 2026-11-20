# Gateway tests

The heartbeat monitor and ESP32 heartbeat client have deterministic tests for
acknowledgements, timeouts, one-time alerting, end-of-day suppression, and
five-second reconnect intervals. Tests use callbacks and do not require BLE
hardware.

The secure-channel tests perform a real OpenSSL handshake, verify the PSK,
exercise protected messages (including the empty heartbeat and acknowledgement
payloads) in both directions, and reject replay and authentication-tag
tampering. They also check the static session pool: the test is built with
`SECURE_CHANNEL_POOL_SIZE=2`, a third session is refused, `secure_channel_clear()`
frees a slot for reuse, and a cleared handle is rejected.

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
with a CMake warning when it is missing. It keeps the production pool size of
1 for both backends and checks that a second session is refused and that a
full handshake works again after `secure_channel_clear()`. It does not replace an
interoperability check on the real hardware.

`report_message_tests` (`report_message_test.c`) cover the shared result and
fault payloads (`../../protocol/src/report_message.c`): exact byte layout and
round trip of every status, intake index `0xFF` only for an unscheduled
removal, fault start/clear, rejection of wrong length, version, type, status,
index and fault state, and that `DOSING_MAX_INTAKES` of the ESP32 does not
exceed `REPORT_MESSAGE_MAX_INTAKES`.

`message_dispatch_tests` (`message_dispatch_test.c`) feed fake RESULT, FAULT
and DAY_END payloads into `message_dispatch_on_message()` with counting
callbacks: required callbacks and entry-count limit, results within today's
schedule (including `0xFF`), unassigned results (index not below the entry
count, or no entry count set) that still update the sequence state, fault
start/clear, empty and non-empty day-end, entry-count reset on day-end
with sequence state kept, malformed payloads and unknown types
returning nonzero without callbacks, and the report sequence rule (first
report after start, duplicates, distance 127/128, seq `0` reset, wrap from 255
to 1).

The startup-transfer tests cover the schedule/time payload, rejection of
invalid schedules or unsynchronized time, and the size limit of one protected
message. The HX711 weighing tests live in `../../esp32/test`.

Add tests for schedule validation and target-weight calculation, time-sync
recovery, notification formatting, and secure protocol handling.

The `telegram_tests` target covers invalid credentials/arguments and refusal
to send when no trusted certificate time is available. It does not contact the
live Telegram service; end-to-end notification checks require a controlled
test bot and must never use real medication data.

`logging_tests` (`logging_test.c`) use a fake terminal output: line format
(UTC timestamp, level, module, code) and the start-up message with the
software version, every health data filter rule (digits, `%`, control
characters, length, module names), that callers can fill the whole queue
while `write()` is blocked, that a message logged from inside `write()` is
accepted, drop counting and the drop report, that an output failure switches
logging off, that the start-up message is always the first line while four
threads log at once, and the init/shutdown lifecycle. Every wait is bounded,
and ctest adds a 60 s timeout.
