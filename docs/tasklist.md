# Open tasks

Status of the implementation against the Design Document (`docs/design/start.pdf`).
Reviewed on 2026-11-20 against the repository snapshot of that day.

Numbers follow the order of the original task list. Requirement numbers in
brackets refer to Design Document, Chapter 6.

Status: **in progress (name)** = reserved, someone is working on it, **done** = implemented and host tests pass, **partial** = parts exist,
see note, **open** = not started.

## Rules for every task

Follow `docs/code-style.md` (MISRA C, a purpose comment on every function with
"Design Document, Chapter N, Section N.M" references per start.pdf). Add host
tests, keep ctest and the WSL host build green, and update the affected README.
Change one thing at a time, document the change and state if further changes
are needed. Don't change unrelated code. Log the prompt in
`docs/audit-logs/prompts.md` and update the status in this file.

Preamble to put in front of every prompt (original, unchanged):

```text
Follow docs/code-style.md (MISRA C, a purpose comment on every function with "Design Document, Chapter N, Section N.M" references per start.pdf). Add host tests, keep ctest and the WSL host build green, and update the affected README. Change one thing at a time, document the change and state if further changes are needed. Don't change unrelated code.
```

To work on a task: give the AI the preamble plus the task prompt, review the result, then update the status here.

## Overview

| # | Task | Area | Status |
|---|------|------|--------|
| 1 | Configurable tolerance | ESP32 | partial |
| 2 | MISRA: dynamic allocation | both | done |
| 3 | MISRA: stdio | Pi | open |
| 4 | Pi logging module | Pi | done |
| 5 | ESP32 logging wrapper | ESP32 | open |
| 6 | Result and fault payloads | protocol | done |
| 7 | Gateway message dispatch | Pi | done |
| 8 | Calibration data and NVS | ESP32 | in progress (Valentina) |
| 9 | Serial calibration | ESP32 | open |
| 10 | Measurement with retries | ESP32 | done (Johannes) |
| 11 | Sensor-fault state | ESP32 | done (Johannes) |
| 12 | Filling check | ESP32 | partial |
| 13 | Expected-weight table | ESP32 | partial |
| 14 | Dose evaluation | ESP32 | partial |
| 15 | Result reporting and buffering | ESP32 | open |
| 16 | Day end and deep sleep | ESP32 | open |
| 17 | PSK commissioning | ESP32 | open |
| 18 | Start-up connection | ESP32 | partial |
| 19 | Heartbeat and reconnect task | ESP32 | partial |
| 20 | ESP32 clock | ESP32 | open |
| 21 | FreeRTOS tasks and app_main | ESP32 | open |
| 22 | Config loader | Pi | partial |
| 23 | Schedule build | Pi | partial |
| 24 | Reminders | Pi | partial |
| 25 | NTP integration | Pi | partial |
| 26 | Alert mapping | Pi | open |
| 27 | Gateway main | Pi | open |
| 28 | Loopback integration test | verification | open |
| 29 | Interop CI script | verification | partial |
| 30 | Traceability matrix | verification | open |
| 31 | Hardware test plan | verification | open |
| 32 | Crypto review package | verification | open |

Summary: 6 done, 11 partial, 15 open.

Suggested order: finish the dosing logic (1, 12–14), then the ESP32 sensor
path (8–11), then wire both applications (20, 21 and 22–27), then verification.

## Tasks

### 1. Configurable tolerance — partial
Prompt (original, unchanged):

```text
Define a tolerance percentage as a configurable option for filling-check and dose comparison.
```

- Note: `DOSING_TOLERANCE_MILLIGRAMS` (20 mg) exists as a fixed absolute value in
  `code/esp32/components/dosing/dosing.h`. A percentage and the agreed value are
  still missing. Decide the value as a team first (see task 12).

### 2. MISRA: dynamic allocation — done
Prompt (original, unchanged):

```text
MISRA: dynamic allocation. "Replace calloc/free in both secure_channel backends with caller-provided fixed state (API change in secure_channel.h), or document a deviation. Keep the interop check passing."
```

- Done: static session pool in both backends, see `docs/misra-deviations.md`.
- Follow-up: `malloc`/`calloc` remain in `bluez_central.c` and `telegram.c` (Rule 21.3, listed as open finding).

### 3. MISRA: stdio — open
Prompt (original, unchanged):

```text
MISRA: stdio. "Remove stdio use from production sources outside the logging module, and fix the required-rule findings in telegram.c and time_sync.c."
```

- Note: `<stdio.h>` is still used in `telegram.c`, `time_sync.c` and `bluez_central.c`.

### 4. Pi logging module — done
Prompt (original, unchanged):

```text
Pi logging module. "Add src/logging with log_info/warn/error(module, ...) printing timestamp, module and level to the terminal, never stored. Reject health data (no formats for names, doses or weights). Add a start-up message with the software version. Run output in a low-priority thread that never blocks and keeps going if
```

- Note: the sentence was cut off in the original list; the ending above follows Requirement 5.4.
- Done: `code/raspberry-pi/src/logging`, tests in `test/logging_test.c`.

### 5. ESP32 logging wrapper — open
Prompt (original, unchanged):

```text
ESP32 logging wrapper. "Add an ESP32 app_log component over ESP-IDF logging with module name, level and timestamp, a version start-up message, and a rule that no health data is logged. Use it in led, weight and ble."
```

### 6. Result and fault payloads — done
Prompt (original, unchanged):

```text
Result and fault payloads. "Define message types 4 (RESULT) and 6 (FAULT) in protocol/: a versioned fixed-size encoding with status enum (taken, not yet taken, wrong amount, missed, unscheduled removal, filling error, sensor fault, no time) and no weights or times. Add encode/decode, size limits, validation and tests. Update SECURE_CHANNEL.md."
```

- Done: `code/protocol/include/report_message.h`, `src/report_message.c`, `SECURE_CHANNEL.md`.

### 7. Gateway message dispatch — done
Prompt (original, unchanged):

```text
Gateway message dispatch. "Add a gateway handler for authenticated RESULT, FAULT and DAY_END messages that decodes them and calls alert callbacks. Plug it into ble_central_config_t.on_message and test it with fake payloads."
```

- Done: `code/raspberry-pi/src/ble/message_dispatch.c`.
- Follow-up: the handler is wired into `main()` in task 27.

### 8. Calibration data and NVS — in progress (Valentina)
Prompt (original, unchanged):

```text
Calibration data and NVS. "Add persistence (NVS blob with version and checksum) for tare offset and counts-per-gram. Load/validate/save API, and fail closed when data is missing or corrupt (2.8)."
```

- Note: `code/esp32/components/persistence` contains only a README.

### 9. Serial calibration — open
Prompt (original, unchanged):

```text
Serial calibration. "Add weight_calibrate(): a serial UART dialog that measures the empty container and a reference weight, computes the factor, and stores it. Add a pure computation function with host tests."
```

### 10. Measurement with retries — done (Johannes)
Prompt (original, unchanged):

```text
Measurement with retries. "Add weight_measure(): on a faulty result, poll twice more and report a sensor fault only if both fail (2.5). Reuse weight_read(). Return a typed status and test it with mocked sequences."
```

- Done: `weight_measure()` in `code/esp32/components/weight`, tests in `code/esp32/test/weight_test.c`.
- Follow-up: the application must call `weight_measure()` instead of `weight_read()` (tasks 11 and 21).

### 11. Sensor-fault state — done (Johannes)
Prompt (original, unchanged):

```text
Sensor-fault state. "Add a small state machine for sensor-fault handling (2.6, 2.7): suspend evaluation, flash red, emit a fault event, recheck every 10 s, and resume when valid. It must be testable with an injected clock."
```

- Done: `code/esp32/components/sensor_fault` (state machine with injected millisecond clock), tests in `code/esp32/test/sensor_fault_test.c`.
- Follow-up: the LED call (`LED_STATUS_FAULT_FLASH`), the FAULT report to the Raspberry Pi and the measurement task are wired in task 21; the FAULT report also needs task 15.

### 12. Filling check — partial
Prompt (original, unchanged):

```text
Filling check. "Add weight_check_filling() (2.9, 2.10): compare the measured weight with the sum of the day's target weights within a tolerance, flash red and report on failure, and recheck every 10 s. First ask me for the tolerance value, and put it in one constant."
```

- Note: the comparison exists as `dosing_filling_is_correct()`. Missing: the agreed
  tolerance value, red flashing, reporting and the 10 s recheck.

### 13. Expected-weight table — partial
Prompt (original, unchanged):

```text
Expected-weight table. "Add table_create() (2.11): from the received schedule build rows (start, end, W_before, W_after) for each 1 h intake window and each gap. Cover boundaries (adjacent windows, midnight) in tests."
```

- Note: implemented as `dosing_table_create()` with tests for adjacent windows and
  midnight. Check against the Design Document and close, or rename to `table_create()`.

### 14. Dose evaluation — partial
Prompt (original, unchanged):

```text
Dose evaluation. "Add pure dose_evaluate() (2.12, 2.13, 1.8) returning status and LED state. Check W_after first, then W_before inside a window, then wrong amount (blue if within one tablet weight, else red), and missed/unscheduled outside a window. It needs the tablet weight for the blue rule; extend the startup message if required. Add full table-driven tests."
```

- Note: implemented as `dosing_evaluate()` with tests. The blue rule uses a fixed
  `DOSING_MIN_TABLET_WEIGHT_MILLIGRAMS` (100 mg) instead of the real tablet weight
  from the startup message.

### 15. Result reporting and buffering — open
Prompt (original, unchanged):

```text
Result reporting and buffering. "Add a fixed-size ring buffer of results (3.2). Report each new result once; buffer while disconnected; flush in order after reconnect. Test overflow behaviour and an exactly-once guarantee."
```

### 16. Day end and deep sleep — open
Prompt (original, unchanged):

```text
Day end and deep sleep. "Add the end-of-day sequence (1.6, 1.7): last-row check, red LED plus hold on a missed dose (led_status_prepare_for_deep_sleep), report missed dose and end of day, close BLE, esp_deep_sleep_start(). Wake only on the EN reset."
```

- Note: the results `DOSING_RESULT_DAY_ENDED_OK` and `DOSING_RESULT_DAY_ENDED_MISSED` exist.

### 17. PSK commissioning — open
Prompt (original, unchanged):

```text
ESP32 connectivity and application - PSK commissioning. Add serial-based commissioning of the 32-byte PSK into NVS (4.7). Reject an all-zero key, never log it, and add a loader for ble_secure_transport_start()."
```

### 18. Start-up connection — partial
Prompt (original, unchanged):

```text
Startup connection.

ESP32 Implement start-up connection (3.1, 3.3): three attempts 5 s apart, then no evaluation, flashing red, and keep retrying. Wait for STARTUP data and decode it with ble_startup_receiver.
```

- Note: `startup_receiver` exists; the retry logic and wiring are missing.

### 19. Heartbeat and reconnect task — partial
Prompt (original, unchanged):

```text
ESP32 Implement Heartbeat and reconnect task. Wire heartbeat_client to the transport with a monotonic clock: send heartbeats every 30 s, handle acknowledgements, disconnect after 2 min, and enter autonomous mode (3.2, 3.4). After reconnect, flush buffered results and apply the received time.
```

- Note: `heartbeat_client` exists with tests; the task and wiring are missing.

### 20. ESP32 clock — open
Prompt (original, unchanged):

```text
ESP32 clock. Add a time source set from the received Unix time plus esp_timer. Provide 'current row' and 'window active' queries for the table, and handle the 'no time' state (3.3).
```

### 21. FreeRTOS tasks and app_main — open
Prompt (original, unchanged):

```text
ESP32 Create the FreeRTOS tasks and priorities from Design Document Table 2.3 (weight and dose high; BLE and heartbeat medium; logging low). Interrupts only notify tasks. Replace the app_main stub and keep the LED self-test first.
```

- Note: `app_main()` only runs the LED self-test.

### 22. Config loader — partial
Prompt (original, unchanged):

```text
Raspberry Pi Config loader. Add schedule_load() (1.9, 1.10, 4.6, 4.7): strict JSON parsing of device.example.json, schema and range validation, and require file mode 600. Fail with an alert if missing or invalid. No secrets in logs.
```

- Note: `schedule_load()` and `schedule_parse()` exist. Check the file-mode-600 check and the alert.

### 23. Schedule build — partial
Prompt (original, unchanged):

```text
Raspberry Pi Schedule build. Compute target weights (quantity × tablet weight) in milligrams. Build today's startup_daily_schedule_t and keep names on the Pi (1.5, 1.10).
```

- Note: `schedule_build_startup()` exists. Check against the requirements and close.

### 24. Reminders — partial
Prompt (original, unchanged):

```text
Raspberry Pi Reminders. Add schedule_check_due() and the reminder text (names and quantities only, minimal content, 1.11, 4.5). Make the clock injectable and the Telegram send a callback.
```

- Note: `schedule_check_due()` and `schedule_format_reminder()` exist. Check and close.

### 25. NTP integration — partial
Prompt (original, unchanged):

```text
Raspberry Pi NTP integration. Implement the time_sync_ops_t callbacks against the host NTP service, run the time_sync_step loop in its own thread, and load/save the last valid time (1.1–1.3, 4.10).
```

- Note: the time_sync logic exists with tests; the real NTP callbacks and the thread are missing.

### 26. Alert mapping — open
Prompt (original, unchanged):

```text
Raspberry Pi Alert mapping. Map events to Telegram messages: NTP failure, bad config, sensor fault, filling error, deviation, missed dose, device unreachable (1.2, 1.6, 2.6, 2.10, 2.13, 3.5). Use minimal text with no weights.
```

### 27. Gateway main — open
Prompt (original, unchanged):

```text
Raspberry Pi Gateway main. Wire main(): logging, config, time sync, BLE central, startup transfer after the 'connected' event (1.4), dispatcher, reminders. Stop operation while time is invalid. Add clean shutdown with last-valid-time saved.
```

- Note: `main()` only starts logging.

### 28. Loopback integration test — open
Prompt (original, unchanged):

```text
Verification and release - Loopback integration test. Add a host test connecting the gateway startup transfer and message handling to the ESP32 logic over an in-memory transport (handshake, startup, results, heartbeats, day end, reconnect). Use the real secure-channel backends.
```

### 29. Interop CI script — partial
Prompt (original, unchanged):

```text
Verification and release Interop CI script. Add a script and CMake target for the ESP32 (mbedTLS) ↔ Pi (OpenSSL) interop check, plus a Linux CI job for host tests, WSL-style strict builds, and misra_c_screening.
```

- Note: the interop ctest `secure_channel_interop_test` exists; the script and CI job are missing.

### 30. Traceability matrix — open
Prompt (original, unchanged):

```text
Verification and release Traceability matrix. Generate docs/traceability.md mapping each Design Document requirement (1.1–6.2) to code and tests, flagging gaps.
```

### 31. Hardware test plan — open
Prompt (original, unchanged):

```text
Verification and release Hardware test plan. Write a hardware-in-the-loop checklist (BLE MTU, handshake, heartbeats, jamming, power loss, deep-sleep LED hold, calibration, filling check, intake-window scenarios) with pass/fail criteria.
```

### 32. Crypto review package — open
Prompt (original, unchanged):

```text
Verification and release Crypto review package. Prepare the independent review package for SECURE_CHANNEL.md and both backends (threat mapping, test vectors, known limitations).
```

## Open decisions

- Tolerance value for filling check and dose comparison (tasks 1 and 12).
- Whether the ESP32 receives the tablet weight for the blue rule, or the fixed
  100 mg stays (task 14).
- After the system runs: walk through the Design Document manually and compare
  with this list, since the list came from an automatic coverage analysis.
