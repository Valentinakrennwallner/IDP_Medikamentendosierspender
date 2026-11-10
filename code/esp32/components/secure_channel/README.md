# Secure-channel component

The ESP32 mbedTLS implementation is currently compiled as part of the BLE
component in `components/ble/secure_channel_mbedtls.c`, because NimBLE and the
secure session lifecycle are managed together there. It follows
`code/protocol/SECURE_CHANNEL.md` and implements P-256 ECDHE, PSK transcript
authentication, HKDF-SHA-256, AES-256-GCM, and replay checks. The ESP32 is
always the ECDHE server, so this backend implements only that role:
`secure_channel_init()` rejects the client role and `secure_channel_start()`
is provided by the gateway backend only. Authentication, framing, or replay
failures end the session; the caller must close the connection.

Do not treat the implementation as production-certified. Commissioning,
hardware interoperability testing, and independent cryptographic review are
still required before deployment (Design Document, Chapter 6, Section 6.2,
Requirements 4.1–4.3 and 4.7–4.9). The gateway and ESP32 backends were
checked against each other on a host build (handshake, protected messages
including empty payloads, tampering, replay, and PSK mismatch); repeat that
check on target hardware.
