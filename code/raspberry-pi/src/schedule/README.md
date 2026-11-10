# Schedule module

Load and validate the medication and intake entries from the device
configuration. Compute each intake's target weight from tablet weight and
quantity. Reject invalid or missing configuration; do not send a schedule to
the ESP32 in that case.

For BLE startup transfer, provide a `startup_daily_schedule_t` containing only
today's intakes, ordered by `intake_time_seconds` since midnight, with each
target weight in milligrams. `startup_message_encode()` rejects empty,
unordered, out-of-day, or zero-weight entries, and `ble_send_startup_data()`
rejects schedules larger than one protected message (510 intakes). The shared
protocol encodes these entries and current Unix time into the payload sent to
the ESP32.

`schedule_load()` and `schedule_check_due()` (Design Document, Chapter 2,
Section 2.1) are not implemented yet; add `schedule.c` with them.
