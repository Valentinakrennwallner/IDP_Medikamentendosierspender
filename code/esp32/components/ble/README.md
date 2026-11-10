# BLE component

This transport implements the Bluetooth communication module described in
the Design Document, Chapter 2, Section 2.1. Its connection recovery and
heartbeat behavior support Chapter 6, Section 6.1, Requirements 3.1-3.4;
its authenticated key exchange, encryption, replay protection, provisioned
key, and single-central policy support Chapter 6, Section 6.2,
Requirements 4.1-4.3 and 4.7-4.9. Application startup, schedule evaluation,
and key provisioning remain the responsibility of the application.

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

`ble_secure_transport_start()` starts a NimBLE peripheral only after the
application explicitly supplies its commissioned 32-byte PSK. It advertises
the shared service/characteristic, permits one central, requires notification
subscription and a negotiated MTU large enough for 180-byte fragments, and
closes the connection on malformed, out-of-order, timed-out, replayed, or
unauthenticated input. The transport rejects an all-zero PSK and disconnects
if the secure-channel handshake does not finish within 15 seconds.
`ble_secure_transport_send()` encrypts typed messages
and emits ordered GATT fragments; call it from the NimBLE host task, such as
inside the authenticated message callback.

The mbedTLS backend implements the shared P-256 ECDHE/PSK handshake,
HKDF-SHA-256 derivation, and AES-256-GCM framing in
`secure_channel_mbedtls.c`. Application startup currently has no commissioned
key loader, so `app_main()` deliberately does not start advertising. Wire the
start API only to a trusted provisioning source; do not embed a default key.
Heartbeat scheduling remains transport-independent and is not wired to the
transport until the application provides monotonic-clock and autonomous-mode
callbacks.

The shared `startup_protocol` ESP-IDF component provides the startup
schedule/time decoder. In the authenticated-message callback, pass a
`SECURE_CHANNEL_MESSAGE_STARTUP` plaintext to
`ble_startup_receiver_accept_authenticated_payload()` and use the returned
schedule and Unix time only when that function succeeds. The callback receives
plaintext only after secure-channel authentication and replay checks.
The callback must return nonzero for unknown or malformed application
messages, which closes the connection.
