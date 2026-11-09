# Raspberry Pi gateway

This is a POSIX C starter application. Build it on a host with CMake:

```sh
cmake -S . -B build
cmake --build build
```

The current executable only confirms startup. Add the gateway modules below
before connecting it to a dispenser:

- `schedule`: load and validate the daily schedule and calculate target weights.
- `time_sync`: NTP synchronization and recovery behavior.
- `ble`: BlueZ transport, heartbeats, and reconnection.
- `secure_channel`: the gateway half of the authenticated BLE protocol.
- `notifications`: Telegram reminders and fault alerts over verified HTTPS.
- `logging`: status-only logs; never log medication names, doses, or weights.

The sample configuration is illustrative only. Validate its schema and
permissions at startup before using it.
