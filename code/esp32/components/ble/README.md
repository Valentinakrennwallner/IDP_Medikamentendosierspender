# BLE component

Implement the ESP32 NimBLE connection lifecycle, heartbeat, reconnect, and
buffered-result delivery here. Use the shared protocol documentation in
`protocol/` once its framing and message schemas are defined.

The shared `startup_protocol` ESP-IDF component provides the startup
schedule/time decoder. After AES-GCM authentication succeeds, pass the
decrypted payload to `ble_startup_receiver_accept_authenticated_payload()`.
Only use the returned schedule and Unix time when that function succeeds.
Connect this receiver to the NimBLE write/notification handler as part of the
BLE transport implementation; never call it with unauthenticated bytes.
