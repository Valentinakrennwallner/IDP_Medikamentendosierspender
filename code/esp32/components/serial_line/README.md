# Serial-line component

Line input and console UART binding shared by the serial commissioning
dialogs: the calibration (`components/calibration`, Requirement 2.8) and the
pre-shared key (`components/psk_commissioning`, Requirement 4.7). Design
Document, Chapter 2, Section 2.1, and Chapter 6, Sections 6.1 and 6.2.

The code was taken out of the calibration dialog (task 9) in task 17, so
that both dialogs use the same input handling; the calibration tests
(`calibration_tests`) pass unchanged.

- `serial_line.c` / `.h` (host-testable): reads one line byte by byte with
  Backspace and Delete; CR, LF and CR LF end a line; other control
  characters are ignored; lines longer than the capacity are marked
  `too_long`. Echo modes: `SERIAL_LINE_ECHO_PLAIN` (typed characters) and
  `SERIAL_LINE_ECHO_MASKED` (one `*` per character, for the key). After the
  first read or write error nothing more is read or sent.
- `serial_line_uart.c` / `serial_line_uart.h`: console UART 0 via the
  ESP-IDF UART driver (no `stdio`, MISRA C Rule 21.6): install the driver if
  needed (`serial_line_uart_open()`), read one byte, write a text.

Tests: through `calibration_tests` and `psk_commissioning_tests` (see
`code/esp32/test/README.md`); the UART binding is tested on the hardware.
