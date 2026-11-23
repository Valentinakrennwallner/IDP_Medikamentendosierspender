# Wall-clock component (ESP32 clock)

Time source of the ESP32 (Design Document, Chapter 2, Sections 2.1 and 2.2;
Chapter 3, Table 6; Chapter 6, Section 6.1, Requirements 1.4, 1.6, 2.11, 3.2
and 3.3).

- The time is set as Unix time, received from the Raspberry Pi with the
  STARTUP message on every connection (Requirements 1.4, 3.2), and then
  advanced with the monotonic esp_timer counter.
- Until a valid time is set, the clock is in the state **"no time"**
  (Requirement 3.3): no current row, no intake window.
- Queries: current time (Unix time, local day, seconds since local
  midnight), current row of the table of expected weights, and whether an
  intake window is active.

## Files

- `wall_clock.c` / `wall_clock.h`: pure logic. The monotonic time is passed
  in as milliseconds, so everything is unit-tested on the host
  (`wall_clock_tests`).
- `wall_clock_esp32.c` / `wall_clock_service.h`: the single clock instance
  on the hardware, advanced with `esp_timer_get_time()` and protected by a
  spinlock (`portENTER_CRITICAL`), because the time is written by the BLE
  thread and read by the weight-measurement and dose-evaluation threads.

| Function | Caller |
|---|---|
| `wall_clock_service_set_unix_time()` | STARTUP handling (task 18), on every connection |
| `wall_clock_service_set_table()` | dose evaluation after the filling check (task 21) |
| `wall_clock_service_now()` | dose evaluation, day end (tasks 16, 21) |
| `wall_clock_service_current_row()` | dose evaluation, day end (tasks 16, 21) |
| `wall_clock_service_intake_window_active()` | weight-measurement thread (5 g check, Requirement 2.4) |

Only "Time set" or "Received time rejected" is logged, never the time
(Requirement 4.4).

## Local time

The table of expected weights uses seconds since **local** midnight, the
STARTUP message carries Unix time (UTC). The clock converts with a fixed
rule for **Austrian time**:

- CET (UTC+1), and CEST (UTC+2) from the last Sunday in March, 01:00 UTC, to
  the last Sunday in October, 01:00 UTC (EU summer-time rule).
- Pure integer calculation, without `<time.h>` (no Rule 21.10 deviation on
  the ESP32).
- **The intake times in the Raspberry Pi schedule must be Austrian local time
  (CET/CEST) as well.**

The conversion was compared with the IANA zone Europe/Vienna (Python
`zoneinfo`) for every hour from 2026 to 2099 and one second around every
switch: no difference.

## Decisions (not fixed in the Design Document)

- Accepted time range: 2026-01-01T00:00:00Z to 2099-12-31T23:59:59Z. A time
  outside the range is rejected and puts the clock into "no time" (fail
  closed); a time that runs past the range while advancing also ends the
  valid time.
- A new time on a reconnect replaces the old one, without a limit on the
  jump. Protection against a forged but valid time is not part of the
  project (Design Document, Chapter 5, Threat 3).
- The table is only valid for the local day on which it was registered:
  - at or after the end of the last window, and on any later local day:
    `WALL_CLOCK_ROW_DAY_ENDED` (the first row of the old table never applies
    again after midnight);
  - a day before the table's day (time set back across midnight):
    `WALL_CLOCK_ROW_NO_TIME` (fail closed).
- Without a valid time or table, `wall_clock_intake_window_active()` returns
  false ("outside an intake window"), so the 5 g change check stays active.

## Known limitations

- **Summer-time switch days:** the clock uses the local wall time. On the
  last Sunday in March the hour 02:00 to 03:00 does not exist, so an intake
  window in that hour is skipped; on the last Sunday in October the hour
  02:00 to 03:00 is passed twice. The Raspberry Pi schedule should reject
  windows in that hour (follow-up, task 22).
- esp_timer runs from the crystal (typically some ppm drift, about a few
  seconds per day). The time is set again on every connection.
- The time is lost in deep sleep and after a reset; the ESP32 is in "no time"
  until the next connection (Requirements 1.7, 3.3).

## Not part of this component

Setting the time from the STARTUP message (task 18) and creating and
registering the table of expected weights (task 21).
