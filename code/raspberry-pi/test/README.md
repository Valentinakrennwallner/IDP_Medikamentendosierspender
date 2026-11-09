# Gateway tests

The heartbeat monitor and ESP32 heartbeat client have deterministic tests for
acknowledgements, timeouts, one-time alerting, end-of-day suppression, and
five-second reconnect intervals. Tests use callbacks and do not require BLE
hardware.

Add tests for schedule validation and target-weight calculation, time-sync
recovery, notification formatting, and secure protocol handling.

The `telegram_tests` target covers invalid credentials/arguments and refusal
to send when no trusted certificate time is available. It does not contact the
live Telegram service; end-to-end notification checks require a controlled
test bot and must never use real medication data.
