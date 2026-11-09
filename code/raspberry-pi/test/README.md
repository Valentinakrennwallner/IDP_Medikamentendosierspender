# Gateway tests

Add tests for schedule validation and target-weight calculation, time-sync
recovery, heartbeat alert behavior, notification formatting, and secure
protocol handling.

The `telegram_tests` target covers invalid credentials/arguments and refusal
to send when no trusted certificate time is available. It does not contact the
live Telegram service; end-to-end notification checks require a controlled
test bot and must never use real medication data.
