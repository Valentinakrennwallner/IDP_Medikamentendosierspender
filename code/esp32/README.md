# ESP32 firmware

Build this project with ESP-IDF from this directory.

## Start-up (`main/app_main.c`)

1. RGB LED initialisation and self-test (red, green, blue).
2. `persistence_init()` and `persistence_calibration_load()`. Any result
   other than `PERSISTENCE_OK` counts as "no calibration".
3. Without a valid calibration the serial calibration (`weight_calibrate()`)
   runs and no evaluation takes place (fail closed, Requirement 2.8). After a
   successful calibration the ESP32 restarts (`esp_restart()`) so the new
   values are loaded through the normal path. After a cancellation or an
   error the red LED flashes (`LED_STATUS_FAULT_FLASH`) and the dialog is
   offered again after 1 s.
4. With a valid calibration the weight-measurement thread is started. If it
   cannot be started, the red LED flashes and nothing is evaluated.

Deviation from the flowchart (Design Document, Chapter 3): the calibration is
loaded before the BLE connection, because BLE is not wired into `app_main`
yet. The order is revisited when BLE is added (task 21).

## Threads (Design Document, Chapter 2, Section 2.3, Table 2)

Priorities in `main/app_tasks.h`: "high" = 5, "medium" = 4, "low" = 2.

| Thread | Priority | State |
|---|---|---|
| Weight measurement (`main/weight_task.c`) | 5 | implemented, stack 4096 bytes |
| Dose evaluation and LED status display | 5 | open (task 21) |
| Bluetooth communication | 4 | open (task 21) |
| Heartbeat | 4 | open (tasks 19, 21) |
| Logging | 2 | open (task 21) |

### Weight-measurement thread

- A periodic `esp_timer` (1 s, `WEIGHT_TASK_PERIOD_MS`) only notifies the
  thread. The period is not given in the Design Document; the group chose
  1 s, so a stable weight (5 measurements) is available after about 5 s. The
  timer callback runs in the esp_timer task (`ESP_TIMER_TASK`), not in an
  interrupt.
- Each cycle: `weight_measure()` (with the two retries of Requirement 2.5),
  then `components/weight_monitor` (stability window, sensor-fault state,
  reference for the 5 g check).
- Sensor fault: `LED_STATUS_FAULT_FLASH`, measurement only every 10 s, the
  stable-weight mailbox is emptied. Valid values again: `LED_STATUS_OFF` until
  the dose evaluation sets the LED, and the log line "Sensor recovered:
  evaluation resumed" (without weight).
- Start and end of a sensor fault are queued as FAULT reports
  (`REPORT_STATUS_SENSOR_FAULT`, active / cleared) in the report buffer
  (`components/report_buffer`); they are sent by the BLE thread now or after
  the reconnect.
- Interface for the dose evaluation: `weight_task_get_stable()` returns the
  newest stable weight from a one-element queue (`xQueueOverwrite()`). No
  weight is delivered during a sensor fault.
- Weights are never logged (Requirement 4.4).

The intake window for the 5 g change check (Requirement 2.4) comes from the
ESP32 clock (`components/wall_clock`,
`wall_clock_service_intake_window_active()`). Without a valid time (state
"no time", Requirement 3.3), without a registered table of expected weights,
or after the day end it is "outside an intake window" (fail closed).

**Until the time is set from the STARTUP message (task 18) and the table is
registered after the filling check (task 21), every weight change above 5 g
counts as a sensor fault.** A real removal therefore causes a 10 s sensor
fault; afterwards the new weight is accepted.

Not yet implemented: dose evaluation (`dosing_evaluate()`, which also queues
the RESULT reports) and the BLE thread that sends the buffered reports
(tasks 18, 19).

## Modules

- `weight`: HX711 reads, calibration constants, plausibility checks and
  stability window.
- `weight_monitor`: per-cycle logic of the weight-measurement thread.
- `sensor_fault`: sensor-fault state machine (Requirements 2.6, 2.7).
- `calibration`: serial commissioning calibration (Requirement 2.8).
- `persistence`: calibration data in NVS.
- `dosing`: schedule table and dose-state evaluation.
- `report_buffer`: ring buffer of RESULT, FAULT and DAY_END reports
  (exactly once, in order, buffered while disconnected).
- `wall_clock`: ESP32 clock (Unix time from the Raspberry Pi plus esp_timer,
  Austrian local time, state "no time", current row, intake window).
- `led`: RGB status handling and startup self-test.
- `ble`: NimBLE transport, heartbeat, reconnect, and buffered results.
- `secure_channel`: authenticated key agreement, encryption, and replay checks.

Keep dose evaluation and user-visible safety feedback local to the ESP32, as
specified in the documentation under "Software Architecture".
