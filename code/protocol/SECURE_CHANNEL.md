# BLE secure-channel wire contract

This contract is shared by the Raspberry Pi BlueZ central and ESP32 NimBLE
peripheral. The Pi is always the ECDHE client. The ESP32 accepts one BLE
central at a time. The Pi must connect only to the configured ESP32 address.

## GATT

The ESP32 advertises service `7e400001-b5a3-f393-e0a9-e50e24dcca9e` and a
single bidirectional characteristic
`7e400002-b5a3-f393-e0a9-e50e24dcca9e`, with Write and Notify properties.
The Pi enables notifications and writes handshake/application fragments to
that characteristic. Neither endpoint handles application payloads before
the secure-channel handshake completes.
Negotiate ATT MTU 191 or greater before starting the handshake; disconnect
if the negotiated MTU is below 191.

## Handshake

All handshake integers and fields use network byte order. Handshake messages
are fixed-size and unfragmented:

| Message | Fields |
|---|---|
| ClientHello (102 bytes) | ASCII `MDP1`, type `1`, 32-byte random nonce, 65-byte uncompressed P-256 public key |
| ServerHello (134 bytes) | ASCII `MDP1`, type `2`, 32-byte random nonce, 65-byte uncompressed P-256 public key, 32-byte HMAC-SHA-256 |
| ClientFinish (37 bytes) | ASCII `MDP1`, type `3`, 32-byte HMAC-SHA-256 |

The server HMAC covers the complete ClientHello followed by the ServerHello
fields excluding its HMAC, prefixed with the ASCII string
`meddispenser-server-v1`. The client HMAC covers the complete ClientHello and
ServerHello, including the server HMAC, prefixed with
`meddispenser-client-v1`. Both HMACs use the 32-byte commissioning PSK.
Messages with invalid framing, key, or HMAC terminate the session.

Both sides derive the P-256 ECDH shared secret and run HKDF-SHA-256 with the
PSK as salt, SHA-256 of the full authenticated handshake transcript as info,
and 40 output bytes. Bytes 0–31 are the AES-256-GCM key, bytes 32–35 are the
client nonce prefix, and bytes 36–39 are the server nonce prefix. Each
direction has an independent 64-bit sequence number
starting at 1. The 12-byte GCM nonce is its directional 4-byte prefix followed
by the sequence number in network order.

## Protected messages

The 20-byte authenticated header is `MDP1`, version `1`, message type, two
zero reserved bytes, 64-bit sequence number, and 32-bit plaintext length.
Header bytes are AES-GCM additional authenticated data. The header is followed
by ciphertext and a 16-byte GCM tag. The receiver accepts only known message
types, exact lengths, valid tags, and sequence numbers strictly greater than
the last accepted number in that direction. An authentication or replay
failure closes the BLE connection; callers must not pass the payload onward.

Application message types `2` (heartbeat), `3` (acknowledgement), and `5`
(day-end) have empty plaintext. Type `1` contains the startup payload defined
in `README.md`; types `4` (result) and `6` (fault) are reserved for the
documented dose-result and fault encodings. An unknown or malformed
application payload is rejected by the application dispatcher.

## GATT fragmentation

Each GATT value contains an 8-byte fragment header followed by up to 180 bytes
of message data: 16-bit message identifier, 16-bit fragment index, 16-bit
fragment count, and 16-bit fragment data length. Headers and data lengths must
be exact; fragments must arrive in order, and the complete framed message may
not exceed 8192 bytes. Any malformed, overlapping, out-of-order, or timed-out
fragment sequence is discarded and the session is closed. Fragment fields
are big-endian. The sender reduces fragment data to `ATT_MTU - 3 - 8` when
the negotiated MTU is below 191. An incomplete message expires after five
seconds.

The shared C declarations are in `include/secure_channel.h` and
`include/ble_wire.h`. This protocol is a project-specific contract and must
receive independent cryptographic review before deployment.
