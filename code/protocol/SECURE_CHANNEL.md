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
the secure-channel handshake completes. Until the handshake completes, every
GATT value is exactly one handshake message without the fragment header;
afterwards every value is a fragment of a protected message.
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
in `README.md`; types `4` (result) and `6` (fault) carry the report payloads
defined below. An unknown or malformed application payload is rejected by the
application dispatcher.

## Result and fault payloads

Both are sent by the ESP32 only, are exactly 5 bytes (`REPORT_MESSAGE_SIZE`)
and contain no weights, times, medication names or doses. Declarations are in
`include/report_message.h`.

| Byte | RESULT (type `4`) | FAULT (type `6`) |
|---|---|---|
| 0 | version `1` | version `1` |
| 1 | type `4` | type `6` |
| 2 | report sequence number | report sequence number |
| 3 | status `1`–`5` | status `6`–`8` |
| 4 | intake index | fault state |

Status values (one numbering for both types, `0` is invalid):

| Value | Status | Type |
|---|---|---|
| 1 | taken | RESULT |
| 2 | not yet taken | RESULT |
| 3 | wrong amount (red or blue LED) | RESULT |
| 4 | missed (including the end-of-day check) | RESULT |
| 5 | unscheduled removal | RESULT |
| 6 | filling error | FAULT |
| 7 | sensor fault | FAULT |
| 8 | no time (connected, but no valid startup message) | FAULT |

The intake index is the 0-based position of the intake in today's schedule
(startup message entry order), `0`–`15` (`REPORT_MESSAGE_MAX_INTAKES` = 16).
Status `5` always carries `0xFF`, and only status `5` may: a removal outside
an intake window is not attributed to a dose. The Raspberry Pi maps the index
to medication names locally and must additionally reject an index that is not
below the entry count of the schedule it sent. The fault state is `1` when the
fault starts and `0` when it is cleared (sensor delivers valid values again,
filling correct again).

The decoder rejects: a length other than 5, a version other than `1`, a type
byte that differs from the protected header's message type, a status outside
`1`–`8` or of the other message type, an invalid intake index, and a fault
state other than `0` or `1`. A later layout change increments the version;
version-1 receivers reject it.

### Report sequence number

RESULT and FAULT share one 8-bit counter on the ESP32, independent of the
secure-channel sequence number, so that buffered reports resent after a
reconnect can be recognised. The first report after ESP32 start-up carries
`0`; later reports count `1`–`255` and wrap from `255` to `1` (never back to
`0`). Resending a buffered report keeps its number.

Receiver rule: a report with number `0` is always accepted and becomes the
last accepted number (the ESP32 has restarted). The first report the Pi
receives after its own start is also always accepted, whatever its number,
because the Pi has no last accepted number yet (the ESP32 may have kept
running and counting). For numbers `1`–`255`, with
`last` the last accepted number, the Pi computes
`d = (seq − last + 255) mod 255` when `last` is non-zero, or `d = seq` when
`last` is `0`; it accepts the report if `1 ≤ d ≤ 127` and drops it as a
duplicate otherwise. Accepted reports update `last`. Therefore the ESP32 must
never hold more than `REPORT_MESSAGE_MAX_PENDING` (127) unsent reports.

Known edge cases: if the report with number `0` is resent after a reconnect,
the Pi accepts it twice; likewise, a report already handled before a Pi
restart is accepted again if it is the first one received afterwards. Both
can only cause one duplicate warning, never a lost one, which is the safe
direction for this device.

### Receiver dispatch on the Raspberry Pi

A payload that fails decoding, a non-empty day-end message, and any message
type other than `4`, `5` or `6` reaching the application dispatcher close
the BLE connection. Duplicates (see above) are dropped without an event.

A RESULT whose intake index is not below the entry count of today's schedule
that the Pi sent (index other than `0xFF`), and any RESULT received before the
Pi has sent a schedule, is not treated as malformed: closing the connection
would make the ESP32 resend the buffered report after every reconnect, and
dropping it could lose a missed dose. Such a report is accepted (the sequence
rule applies and updates `last` as usual) and reported as an *unassigned*
result: the status is passed on, but not attributed to a dose of the
schedule.

A valid (empty) day-end message ends today's schedule on the Pi: the entry
count is reset to `0` before the day-end event is reported, so any later
RESULT is unassigned until the Pi sends the next schedule. The report
sequence state is not reset by a day-end.

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
