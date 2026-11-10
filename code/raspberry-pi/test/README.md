# Gateway tests

The heartbeat monitor and ESP32 heartbeat client have deterministic tests for
acknowledgements, timeouts, one-time alerting, end-of-day suppression, and
five-second reconnect intervals. Tests use callbacks and do not require BLE
hardware.

The secure-channel tests perform a real OpenSSL handshake, verify the PSK,
exercise protected messages (including the empty heartbeat and acknowledgement
payloads) in both directions, and reject replay and authentication-tag
tampering.

The startup-transfer tests cover the schedule/time payload, rejection of
invalid schedules or unsynchronized time, and the size limit of one protected
message. The HX711 weighing tests live in `../../esp32/test`.

Add tests for schedule validation and target-weight calculation, time-sync
recovery, notification formatting, and secure protocol handling.

The `telegram_tests` target covers invalid credentials/arguments and refusal
to send when no trusted certificate time is available. It does not contact the
live Telegram service; end-to-end notification checks require a controlled
test bot and must never use real medication data.
