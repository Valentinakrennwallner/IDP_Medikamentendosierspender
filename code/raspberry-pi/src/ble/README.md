# BLE module

`bluez_central.h` exposes the Pi's BlueZ central lifecycle. Configure the
ESP32's canonical Bluetooth address and the 32-byte provisioned PSK in
`ble_central_config_t`; there are no built-in credentials or partner
addresses. The PSK must not be all zero. Supply the required
`on_heartbeat_timeout` callback (for example
`telegram_heartbeat_timeout_alert()`). The module scans with BlueZ, connects
only when the exact configured
address is present, waits for service resolution, locates the shared service
and characteristic UUIDs, enables notifications, then performs the client
ECDHE/PSK handshake before accepting application sends.

The worker owns its system-D-Bus connection and uses BlueZ ObjectManager,
Adapter1, Device1, GattCharacteristic1, and Properties signals. It retries
failed connections every five seconds until stopped. The CMake build therefore
requires the `dbus-1` development package in addition to the existing Pi
dependencies.

## Lifecycle and callbacks

Create, configure, start, stop, and destroy the client from application code.
`ble_central_create()` copies the address and PSK, but callback functions and
their context must remain valid until `ble_central_destroy()`. Start returns
after launching the event thread, not after connecting. Stop waits for that
thread to finish; stop/destroy must be called from a thread other than a BLE
callback. Callbacks run serially on the worker thread and must not wait for
another BLE event or destroy the client.

The gateway starts the two-minute reachability timer when the worker starts,
including while it is still looking for the first connection. After a valid
handshake, each heartbeat resets that timer.

`on_event` reports authenticated connection/disconnection and fatal/setup
errors without exposing the configured peer address. `on_message` receives authenticated message types other than heartbeat;
its payload is borrowed and valid only until the callback returns. Heartbeats
must have an empty payload and are acknowledged automatically with a protected
acknowledgement. Day-end messages must also be empty and stop heartbeat
monitoring. `on_message` must return nonzero for invalid application payloads;
the central then closes the session. The required timeout callback is invoked after two minutes without
a valid heartbeat; a nonzero callback result closes the session.

`ble_central_send()` seals and writes application messages only while the
session is authenticated. Calls from other threads synchronously wait for all
fragment writes; callback-thread calls execute directly. `ble_central_secure_send_payload()`
adapts the transport to `ble_send_startup_data()` and sends a protected
`STARTUP` message. The caller must still invoke that startup helper only after
the connected event. All secure sequence management is delegated to the
`secure_channel` API.

Every handshake and protected frame is carried in the shared BLE fragment
format. Incoming fragments are accepted only in strict index order, with
consistent identifiers/counts, exact lengths, bounded total size, and a
five-second assembly timeout. The handshake also has a 15-second deadline. Any
malformed fragment, failed GATT operation, invalid or timed-out handshake, or
secure-channel authentication/replay failure closes the session and clears key
state.

After each BLE connection has completed its authenticated key exchange, call
`ble_send_startup_data()` with the prepared current-day schedule and the active
time-sync service, using `ble_central_secure_send_payload()` as its send
callback. Do not send either value before authentication succeeds. The helper
validates and encodes a versioned schedule/time payload; the callback seals it
and returns an error if encryption or transport fails. It consumes the payload
synchronously; the helper releases the buffer when the callback returns.

The central integrates `heartbeat_monitor` with its monotonic event loop.
Supply `telegram_heartbeat_timeout_alert()` as `on_heartbeat_timeout` when
using Telegram alerts, with a `telegram_heartbeat_alert_context_t` containing
protected bot credentials and the time-sync service. The central polls the
monitor once per second and retries the configured device address every five
seconds.
