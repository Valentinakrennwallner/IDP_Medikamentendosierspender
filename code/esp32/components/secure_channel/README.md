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

Session state is kept in a fixed static pool of `SECURE_CHANNEL_POOL_SIZE`
slots (default 1: the ESP32 accepts one BLE connection). `secure_channel_init()`
takes a free slot and fails when none is left; `secure_channel_clear()` erases
and releases it. The backend allocates no heap memory itself (MISRA C:2012
Rule 21.3). `secure_channel_init()` and `secure_channel_clear()` are not
thread-safe; they run only in the NimBLE host task (`ble_secure_transport.c`:
connect, disconnect, and connection-close paths).

Known limitation: Mbed TLS itself still allocates heap memory internally
(bignum limbs of the ECP group, private key, and points; GCM contexts). Rule
21.3 is applied to the project's own code only (see
`docs/misra-deviations.md`).

Do not treat the implementation as production-certified. Commissioning,
hardware interoperability testing, and independent cryptographic review are
still required before deployment (Design Document, Chapter 6, Section 6.2,
Requirements 4.1–4.3 and 4.7–4.9). The gateway and ESP32 backends are
checked against each other on the host by the CTest
`secure_channel_interop_tests` (handshake, protected messages including empty
payloads, tampering, replay, and PSK mismatch; see
`code/raspberry-pi/test/README.md`). It uses the host mbedTLS 3.6 library, not
the ESP-IDF build or the hardware RNG; repeat the check on target hardware.
