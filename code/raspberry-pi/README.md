# Raspberry Pi gateway

This is a POSIX C starter application. Build it on a host with CMake:

```sh
cmake -S . -B build
cmake --build build
```

The current executable starts the logging module, prints the start-up
message with the software version (`0.1.0`, set by `project(... VERSION)` in
`CMakeLists.txt`) and exits. Add the gateway modules below before connecting
it to a dispenser:

- `schedule`: load and validate the daily schedule and calculate target weights.
- `time_sync`: startup/hourly sync policy, failure recovery, and last-valid-time
  persistence. The gateway must provide callbacks connected to the host NTP
  service and Telegram alert delivery.
- `ble`: BlueZ transport, heartbeats, reconnection, and dispatch of
  result/fault/day-end reports.
- `secure_channel`: the gateway half of the authenticated BLE protocol.
- `notifications`: Telegram reminders and fault alerts over verified HTTPS;
  requires libcurl with its OpenSSL backend and OpenSSL development headers.
- `logging`: status-only terminal logs with UTC timestamp, level and module,
  printed by a low-priority thread that never blocks callers; a health data
  filter rejects messages with digits or `%`. The API is not variadic
  (MISRA C:2012 Rule 17.1); see `src/logging/README.md`.

The sample configuration is illustrative only. Validate its schema and
permissions at startup before using it.
