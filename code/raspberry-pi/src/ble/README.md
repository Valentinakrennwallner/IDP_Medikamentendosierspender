# BLE module

Implement the Raspberry Pi BlueZ connection lifecycle, partner address
restriction, heartbeat monitoring, and secure-channel integration.

After each BLE connection has completed its authenticated key exchange, call
`ble_send_startup_data()` with the prepared current-day schedule and the active
time-sync service. The helper validates the schedule and encodes a
versioned schedule/time payload from `code/protocol/`. Its secure-send callback
must encrypt/authenticate that complete payload and transmit it over the
authenticated session. Do not send either value before authentication
succeeds. The callback must return an error if encryption or transport fails.
It must consume the payload synchronously; the buffer is released when the
callback returns.

Use `heartbeat_monitor` with the authenticated BLE event loop and a monotonic
millisecond clock. Call `heartbeat_monitor_on_connected()` after session
authentication, `heartbeat_monitor_on_heartbeat()` only for a valid,
non-replayed heartbeat (its callback sends the protected acknowledgement),
and `heartbeat_monitor_on_day_end()` when the ESP32 reports completion. Poll
`heartbeat_monitor_tick()` at least once per second. It requests one
dispenser-unreachable alert at two minutes without a heartbeat and requests
reconnection immediately and then every five seconds. Supply the alert
callback as `telegram_heartbeat_timeout_alert()` with a
`telegram_heartbeat_alert_context_t` containing the protected bot credentials
and time-sync service; callback errors are returned to the caller.
