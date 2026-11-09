# LED component

`led_status.c` provides `led_status_set()` for off, green, blue, red, and
flashing-red fault indication, plus initialization and the red/green/blue
startup self-test. Call `led_status_prepare_for_deep_sleep()` after the last
intake evaluation so red remains on for a missed final dose and the LED is off
when all doses were taken.

The implementation assumes the specified common-cathode KY-009 wiring:
GPIO 25 red, GPIO 26 green, and GPIO 27 blue, all active high. These GPIOs
require the series resistors shown in the Design Document.

See the LED state table in `../../../../docs/design/projectdescription.tex`
(Design Document, Chapter 1, Section 1.2), the pinout in
`../../../../docs/design/softwarearchitecture.tex` (Chapter 2, Section 2.4),
and Requirements 1.6–1.8, 2.6, 2.10, and 2.12–2.13 in Chapter 6, Section 6.1.
