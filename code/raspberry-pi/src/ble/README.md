# BLE module

This module implements the Bluetooth communication module described in the
Design Document, Chapter 2, Section 2.1. Its connection recovery and heartbeat
monitoring support Chapter 6, Section 6.1, Requirements 3.1-3.5; its
authenticated key exchange, encryption, replay protection, configured peer
address, and shared key support Chapter 6, Section 6.2, Requirements 4.1-4.3
and 4.7-4.9. Configuration loading and file-permission enforcement remain
application responsibilities.

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

Handshake messages are written and received as single unfragmented GATT
values. Every protected frame is carried in the shared BLE fragment
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

## Report dispatch

`message_dispatch.h` is the gateway handler for authenticated RESULT (4),
DAY_END (5) and FAULT (6) messages (Design Document, Chapter 2, Section 2.1,
and Chapter 6, Section 6.1, Requirements 1.6, 1.7, 2.6, 2.10 and 2.13). Keep
one `message_dispatch_t` in static storage, initialize it with
`message_dispatch_init()` and four required callbacks, and wire it into the
central:

```c
static message_dispatch_t dispatch;

(void)message_dispatch_init(&dispatch, &callbacks);
config.on_message = message_dispatch_on_message;
config.callback_context = &dispatch;
```

After `ble_send_startup_data()` succeeds, call
`message_dispatch_set_entry_count(&dispatch, schedule.entry_count)` on the
BLE worker thread (for example from the connected-event callback), so that
intake indices can be checked against the schedule that was sent. The
dispatcher has no lock; do not call it from other threads while the
central is running.

The dispatcher decodes RESULT and FAULT with `report_message_decode()` and
applies the report sequence rule from
[`SECURE_CHANNEL.md`](../../../protocol/SECURE_CHANNEL.md#report-sequence-number):
number `0` and the first report after the gateway starts are always
accepted, otherwise only a distance of 1–127 to the last accepted number.
Duplicates return zero without a callback. The state survives
disconnections, so reports resent after a reconnect are still recognised.

| Event | Callback |
|---|---|
| RESULT, index below today's entry count, or `0xFF` (unscheduled removal) | `on_result(context, status, intake_index)` |
| RESULT, index not below today's entry count, or no schedule sent yet | `on_report_unassigned(context, status)` |
| FAULT | `on_fault(context, status, fault_state)` |
| empty DAY_END | entry count reset to 0, then `on_day_end(context)` |

A valid DAY_END ends today's schedule: results received afterwards are
unassigned until the next `message_dispatch_set_entry_count()`. The report
sequence state is kept, so resent reports stay duplicates across the day end.

An unassigned RESULT is accepted like any other report instead of closing the
connection (which would make the ESP32 resend it after every reconnect) or
dropping it (which could lose a missed dose). Malformed payloads, non-empty
DAY_END messages and any other message type return nonzero, so the central
closes the session. Callbacks return nothing, so a failed notification never
closes the BLE connection. They receive only status, intake index and fault
state; mapping them to Telegram texts is the application's job. The
dispatcher logs nothing.
