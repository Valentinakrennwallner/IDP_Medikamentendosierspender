# Schedule module

Implements the intake schedule part of the "Intake schedule and time control"
module (Design Document, Chapter 2, Section 2.1, safety module 4) and
Requirements 1.5, 1.9-1.11 and 4.6. Tested on the host (`schedule_tests`).

- `schedule_load()` opens the configuration file without following symbolic
  links, requires a regular file owned by the current user with mode 600
  (Requirement 4.6), and parses it. On any error the schedule is cleared and
  must not be sent to the ESP32; the caller sends the Telegram alert
  (Requirement 1.10).
- `schedule_parse()` validates the `medications` and `intakes` members of the
  configuration (see `config/device.example.json`); other members are ignored.
  The target weight of each intake is the sum of quantity x tablet weight
  (Requirement 1.10). Intakes are sorted by time.
- `schedule_build_startup()` fills the intake times and target weights sent to
  the ESP32 (Requirement 1.5).
- `schedule_check_due()` reports an intake whose time lies in
  `(previous, now]`; the caller keeps the time of its previous check, so each
  reminder is sent exactly once, also across midnight (Requirement 1.11).
- `schedule_format_reminder()` builds the reminder text with only the names
  and quantities, e.g. `Erinnerung: Bitte jetzt einnehmen: 2x Medikament A,
  1x Medikament B` (Requirements 1.11, 4.5).

## Validation rules

A configuration is rejected if any of these is violated:

- 1-16 medications with unique, non-empty ids and names (no control
  characters), tablet weight > 0 g (rounded to whole milligrams).
- 1-16 intakes with time `HH:MM`, 1-8 medications each, known ids, integer
  quantity 1-100, no medication twice in one intake.
- 1 h intake windows must not overlap or extend past midnight (same rule as
  the ESP32 `dosing` module, which would otherwise reject the schedule).
- Daily total at most 100 g (load cell capacity).

## Dependencies and notes

- JSON parsing uses json-c (https://github.com/json-c/json-c). Install
  `libjson-c-dev` on the Raspberry Pi. json-c allocates memory internally;
  parsing happens once at start-up, outside the safety-critical runtime path.
- Times are seconds since **local** midnight. The caller must convert the NTP
  (UTC) system time to local time before calling `schedule_check_due()`.
- `schedule_load()` uses a static buffer and is not reentrant; call it once at
  start-up. The buffer is cleared afterwards.
- The minimum tablet weight per intake is calculated but not yet sent to the
  ESP32 (would require a start-up message format change).
- cppcheck MISRA screen: Rule 21.1 (`_POSIX_C_SOURCE`) is a documented
  deviation; Rule 22.10 at `read()` is a false positive (POSIX `read()` sets
  `errno`); Rule 8.7 (advisory) because `schedule_parse()` is public for tests.
