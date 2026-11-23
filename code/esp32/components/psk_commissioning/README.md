# PSK-commissioning component

Serial commissioning of the 256-bit pre-shared key (PSK) of the ESP32
(Design Document, Chapter 2, Sections 2.1 and 2.2; Chapter 5, Section 5.1,
Threat 6; Chapter 6, Section 6.2, Requirements 4.7 and 4.9). The key is
stored once in NVS via the serial interface; the Raspberry Pi holds the same
key as `psk_hex` in its configuration file.

## Generating the key

Generate the key randomly, once per device pair, for example on the
Raspberry Pi:

```sh
openssl rand -hex 32
```

Put the 64 hexadecimal characters into `psk_hex` of the Raspberry Pi
configuration file (file mode 600, Requirement 4.6) and type the same text
in the ESP32 dialog. Never use an example key, and do not keep the key in a
shell history, ticket or chat.

## Dialog (`psk_commission()`)

1. NVS is initialised (`persistence_init()`). If that fails, nothing is
   saved (`PSK_COMMISSIONING_STORAGE_ERROR`). The NVS erase is offered only
   by the calibration dialog (`ERASE`); it also deletes the key.
2. If a valid key is already stored, it is only replaced after typing
   `REPLACE`; anything else cancels and keeps the stored key.
3. The key is typed twice: exactly 64 hexadecimal characters, lower or upper
   case, no spaces, no `0x`. Invalid input, obviously non-random keys (all
   bytes equal, sequences rising or falling by 1; see
   `persistence_psk_is_acceptable()`) and two different entries are
   rejected and asked again.
4. The key is saved and verified by reloading and comparing it.

`q` (or `Q`) cancels at every prompt without saving. Backspace works, lines
end with CR, LF or CR LF (shared line input, `components/serial_line`).

## Secrecy of the key

- The typed characters are echoed as `*` only. No output contains a
  character, byte, length (other than the 64 stars), checksum or
  fingerprint of the key, and nothing is written to the log.
- Every buffer that held the key or its hex text (line buffer, both entries,
  the reloaded key, the copy loaded for the `REPLACE` check) is overwritten
  with `mbedtls_platform_zeroize()` as soon as it is no longer needed, also
  on cancel and error paths. A plain `memset()` on a buffer that is not read
  afterwards is a dead store that the compiler may remove; the Mbed TLS
  function is guaranteed not to be optimised away.
- The two entries are compared without an early exit.

## Files

- `psk_commissioning.c` / `.h`: hex parsing (`psk_commissioning_parse_hex()`)
  and the dialog (`psk_commissioning_dialog_run()`) over
  `psk_commissioning_io_t` (serial I/O and storage as function pointers),
  unit-tested on the host (`psk_commissioning_tests`).
- `psk_commissioning_uart.c`: `psk_commission()` connects the dialog to the
  console UART (`components/serial_line`) and the NVS functions of
  `components/persistence`.

## Use (tasks 18 and 21)

At start-up, load the key with `persistence_psk_load()`. Only on
`PERSISTENCE_OK` may `ble_secure_transport_start()` be called; otherwise BLE
stays disabled (fail closed) and `psk_commission()` can be run. Calling it
from `app_main()` and the order relative to the calibration belong to tasks
18 and 21.

## Known limitations

- NVS is not encrypted. A person with physical access can read the key
  (Design Document, Chapter 5, Section 5.2, Threat 1; flash encryption is
  not part of this project).
- Only obviously non-random keys are detected; a weak but irregular key
  (e.g. a typed phrase in hex) is accepted. The key must be generated
  randomly.
- There is no inactivity timeout: without a valid key BLE stays disabled
  anyway, so waiting is safe.
