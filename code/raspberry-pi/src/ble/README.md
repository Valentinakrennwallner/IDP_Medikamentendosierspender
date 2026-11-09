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
