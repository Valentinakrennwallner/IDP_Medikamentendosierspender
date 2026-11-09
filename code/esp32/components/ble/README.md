# BLE component

The `heartbeat_client` component provides transport-independent timing for
the ESP32 NimBLE connection lifecycle. Call `heartbeat_client_on_connected()`
after the authenticated session is ready, pass authenticated and
non-replayed acknowledgements to `heartbeat_client_on_acknowledgement()`, and
poll `heartbeat_client_tick()` using a monotonic millisecond clock. The
callbacks send an authenticated heartbeat every 30 seconds, disconnect and
enter autonomous operation after two minutes without an acknowledgement, and
attempt reconnection every five seconds. Call
`heartbeat_client_on_disconnected()` on a BLE stack disconnect; after a
successful reconnection, perform the buffered-result delivery and current-time
transfer before starting the new heartbeat interval.

The component does not implement the NimBLE transport or secure-channel
message encoding. Callbacks must send/accept only authenticated messages and
must preserve protocol sequence-number checks.

The shared `startup_protocol` ESP-IDF component provides the startup
schedule/time decoder. After AES-GCM authentication succeeds, pass the
decrypted payload to `ble_startup_receiver_accept_authenticated_payload()`.
Only use the returned schedule and Unix time when that function succeeds.
Connect this receiver to the NimBLE write/notification handler as part of the
BLE transport implementation; never call it with unauthenticated bytes.
