# Secure-channel component

The ESP32 mbedTLS implementation is currently compiled as part of the BLE
component in `components/ble/secure_channel_mbedtls.c`, because NimBLE and the
secure session lifecycle are managed together there. It follows
`code/protocol/SECURE_CHANNEL.md` and implements P-256 ECDHE, PSK transcript
authentication, HKDF-SHA-256, AES-256-GCM, and replay checks.

Do not treat the implementation as production-certified. Commissioning,
hardware interoperability testing, and independent cryptographic review are
still required before deployment (Design Document, Chapter 6, Section 6.1,
Requirements 4.1–4.3 and 4.7–4.9).
