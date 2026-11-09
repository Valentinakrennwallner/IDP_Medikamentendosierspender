# Schedule module

Load and validate the medication and intake entries from the device
configuration. Compute each intake's target weight from tablet weight and
quantity. Reject invalid or missing configuration; do not send a schedule to
the ESP32 in that case.

For BLE startup transfer, provide a `daily_schedule_t` containing only today's
intakes, ordered by `intake_time_seconds` since midnight, with each target
weight in milligrams. `schedule_is_valid_daily()` rejects empty, unordered,
out-of-day, or zero-weight entries before transfer. The shared protocol encodes
these entries and current Unix time into the payload sent to the ESP32.
