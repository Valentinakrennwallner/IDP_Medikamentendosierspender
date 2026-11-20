# Dosing component

Implements the dose evaluation and intake monitoring module (Design Document,
Chapter 2, Section 2.1, safety module 2) as pure logic without hardware
dependencies, so it is unit-tested on the host (`dosing_tests`).

- `dosing_table_create()` builds the table of expected weights from the
  received schedule (Requirement 2.11, Table 6): an initial row before the
  first intake, one 1 h window row per intake and a row for each gap between
  windows. Rows are contiguous from midnight to the end of the last window.
- `dosing_filling_is_correct()` performs the start-up filling check
  (Requirement 2.9).
- `dosing_evaluate()` compares the measured weight with the row for the
  current time (Requirements 1.6, 1.8, 2.12, 2.13) and returns a result that
  maps to the LED states of Table 1. Outside an intake window, a weight that
  does not correspond to W_after is classified by the sign of the deviation
  (all three results are red and count as deviations):
  - below W_after: `DOSING_RESULT_UNSCHEDULED_REMOVAL`;
  - above W_after after an ended window (gap row): `DOSING_RESULT_MISSED`;
  - above W_after before the first window (initial row):
    `DOSING_RESULT_UNEXPECTED_INCREASE`.
- `dosing_result_is_deviation()` marks results that must be reported to the
  Raspberry Pi as a warning.

Inputs are seconds since local midnight and weights in milligrams. The caller
converts the HX711 reading (grams) and the received Unix time accordingly and
only passes plausible measurements (Requirement 2).

## Assumptions not fixed in the Design Document

- Tolerance for "corresponds to": `DOSING_TOLERANCE_MILLIGRAMS` (20 mg). To be
  verified against the noise of the real load cell.
- Weight of one tablet for the blue/red decision (Requirement 1.8):
  `DOSING_MIN_TABLET_WEIGHT_MILLIGRAMS` (100 mg, lightest tablet in Table 6).
  The ESP32 does not receive per-tablet weights; sending them from the
  Raspberry Pi would require a protocol change.
- Schedules are rejected if they contain more than `DOSING_MAX_INTAKES`
  intakes, overlapping intake windows, a window extending past midnight, or a
  total weight above the 100 g load cell capacity.
- Before the first intake window, a correctly filled container evaluates as
  "taken" (green), because Requirement 2.12 compares with W_after first.
- A weight above W_after before the first intake window is reported as
  `DOSING_RESULT_UNEXPECTED_INCREASE`, not as a removal or a missed dose. The
  filling check (Requirements 2.9, 2.10) only runs at start-up, so it does not
  cover an increase during normal operation.

## Known limitations

- The evaluation is stateless (current row and current weight only). An
  overdose inside an intake window (`DOSING_RESULT_WRONG_AMOUNT`) therefore
  becomes `DOSING_RESULT_UNSCHEDULED_REMOVAL` once the window has ended.
