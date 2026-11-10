# Shared BLE protocol

The BLE GATT service, fragmentation, ECDHE/PSK handshake, AES-GCM framing,
and replay-counter contract are defined in [`SECURE_CHANNEL.md`](SECURE_CHANNEL.md).
The portable API is declared in `include/secure_channel.h`; platform-specific
cryptography must implement that API without changing the wire format.

`include/startup_message.h` and `src/startup_message.c` define the startup
schedule/time application payload shared by the Raspberry Pi and ESP32. All
multi-byte integers use big-endian byte order:

- Byte 0: protocol version (`1`).
- Byte 1: message type (`1`, schedule and time).
- Bytes 2–9: positive Unix time in seconds (`uint64`).
- Bytes 10–13: number of schedule entries (`uint32`, from 1 through 86400).
- Bytes 14–15: reserved; must be zero.
- Each entry: intake time in seconds since midnight (`uint32`), followed by
  target weight in milligrams (`uint32`).

Entries must be non-empty, ordered by strictly increasing intake time within
the day, and have non-zero target weights. The payload does not include
medication names or quantities.

The secure-channel layer must authenticate and encrypt the complete payload
and apply the specified anti-replay sequence number. The BLE adapter is
responsible for transporting/reassembling the payload if the negotiated
characteristic MTU requires fragmentation. Do not send this message over an
unauthenticated connection.

Add cross-device test vectors for this format, including malformed lengths,
unsupported versions/types, invalid times, and replay handling.
