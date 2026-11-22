# LED component

`led_status.c` provides `led_status_set()` for off, green, blue, red, and
flashing-red fault indication, plus initialization and the red/green/blue
startup self-test. Call `led_status_prepare_for_deep_sleep()` after the last
intake evaluation so red remains on for a missed final dose and the LED is off
when all doses were taken.

The implementation assumes common-cathode KY-009 wiring by actual LED colour:
GPIO 25 red, GPIO 26 green, and GPIO 27 blue, all active high, each through a
220 Ohm series resistor.

## Deviations from the Design Document

- The R and G silk-screen labels of the KY-009 module used are swapped. The
  wiring follows the actual colour: GPIO 25 goes to the pin labelled "G"
  (red), GPIO 26 to the pin labelled "R" (green). The code keeps the colour
  assignment of Chapter 2, Section 2.4, Table 5; do not swap the pins in code.
- All three series resistors are 220 Ohm instead of 220 Ohm (red) and
  160 Ohm (green, blue) (Chapter 2, Section 2.4, Table 5 and Figure 3).
  Green and blue are somewhat dimmer; the software is not affected.

See the LED state table in `../../../../docs/design/projectdescription.tex`
(Design Document, Chapter 1, Section 1.2), the pinout in
`../../../../docs/design/softwarearchitecture.tex` (Chapter 2, Section 2.4),
and Requirements 1.6–1.8, 2.6, 2.10, and 2.12–2.13 in Chapter 6, Section 6.1.
