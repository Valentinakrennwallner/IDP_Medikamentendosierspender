# Secure-channel module

`secure_channel.c` implements the Raspberry Pi side of the P-256 ECDHE/PSK
handshake, transcript authentication, HKDF-SHA-256 key derivation, AES-256-GCM
message protection, and monotonically increasing sequence checks. It follows
the wire contract in `code/protocol/SECURE_CHANNEL.md`; callers must terminate
the BLE session after any handshake, authentication, framing, or replay
failure.

The caller supplies the commissioned 32-byte PSK at session initialization.
The module does not load or log credentials and does not provide BLE transport.
Run `secure_channel_tests` for handshake, wrong-PSK, bidirectional encryption,
tag-tamper, replay, and session-pool checks, and
`secure_channel_interop_tests` for the check against the ESP32 mbedTLS
backend.

Session state is kept in a fixed static pool of `SECURE_CHANNEL_POOL_SIZE`
slots (default 1: the gateway has one ESP32 connection). `secure_channel_init()`
takes a free slot and fails when none is left; `secure_channel_clear()` erases
and releases it. This file allocates no heap memory itself (MISRA C:2012
Rule 21.3). Only host tests override the pool size with a compile definition
(`secure_channel_tests` uses 2 for a client and a server session).
`secure_channel_init()` and `secure_channel_clear()` are not thread-safe; they
run only on the BLE worker thread of `../ble/bluez_central.c`.

Known limitation: OpenSSL itself still allocates heap memory internally
(`EVP_PKEY`, `EVP_*_CTX`, KDF and MAC objects). Rule 21.3 is applied to the
project's own code only (see `docs/misra-deviations.md`).
