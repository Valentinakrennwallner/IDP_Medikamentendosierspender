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
tag-tamper, and replay checks.
