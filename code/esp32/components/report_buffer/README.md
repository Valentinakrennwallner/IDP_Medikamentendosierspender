# Report-buffer component

Ring buffer of the reports the ESP32 sends to the Raspberry Pi (Design
Document, Chapter 3, ESP32 flowchart; Chapter 6, Section 6.1, Requirements
1.6, 1.7, 2.6, 2.10, 2.13 and 3.2):

- RESULT (type 4): taken, not yet taken, wrong amount, missed (also at the
  end of the day), unscheduled removal;
- FAULT (type 6): filling error, sensor fault, no time; active or cleared;
- DAY_END (type 5): end of the day, empty.

The payload format and the report sequence number are defined in
`code/protocol/SECURE_CHANNEL.md` (encoders in `report_message.h`).

## Behaviour

- **Each report once, in order:** every queued report is handed to the
  send function until it succeeds once, oldest first. A second flush sends
  nothing.
- **Buffering (Requirement 3.2):** while there is no connection the send
  function fails, and the reports stay in the buffer. After the reconnect
  they are sent in the order they were queued.
- **Report sequence number:** assigned at the **first send attempt**, not
  when queuing: `0` for the first report since start-up, then `1` to `255`,
  wrapping from `255` to `1`. A failed send keeps the number for the resend.
  DAY_END has no number. Because no number is used for a report that is
  dropped before its first send, the numbers the Raspberry Pi sees are
  always consecutive, also after an overflow, so its duplicate rule (accept
  a distance of 1 to 127) never drops a new report.
- **Capacity and overflow:** 127 reports (`REPORT_MESSAGE_MAX_PENDING`,
  about 1 KB). When the buffer is full, the **oldest** report is dropped (the
  current state is kept) and a warning is logged.
- **Validation:** a report is checked with the shared encoders before it is
  queued; invalid reports are rejected and use no number.
- **Change detection** ("each new result", Requirement 2.13) is the job of
  the caller (dose evaluation, task 21; `weight_monitor` reports a sensor
  fault once). The buffer queues every report it is given.

## Files

- `report_buffer.c` / `report_buffer.h`: pure logic, unit-tested on the host
  (`report_buffer_tests`). `report_buffer_peek()` encodes the oldest report
  without removing it, `report_buffer_commit(id)` removes it after a
  successful send (only if it is still the same entry),
  `report_buffer_flush()` combines both for a single thread.
- `report_buffer_esp32.c` / `report_buffer_service.h`: the single buffer on
  the ESP32, protected by a spinlock. The lock is held only around peek,
  commit and push, never while the send function runs.

Send interface for the BLE transport (tasks 18 and 19):

```c
typedef bool (*report_buffer_send_fn)(void *context, uint8_t message_type,
                                      const uint8_t *payload, size_t length);
```

It returns true only if the transport accepted the message. The BLE thread
calls `report_buffer_service_flush()` after every (re)connect and when new
reports wait while connected.

| Producer | Report |
|---|---|
| weight-measurement thread | FAULT sensor fault, active / cleared (implemented) |
| dose evaluation (task 21) | RESULT, FAULT filling error / no time |
| day end (task 16) | RESULT missed (last window), DAY_END |

Only "buffer full: oldest report dropped" and "invalid report not queued"
are logged, never the content of a report (Requirement 4.4).

## Known limitations

- **No acknowledgement from the Raspberry Pi:** the protocol has no
  application-level acknowledgement for reports. A report counts as sent
  when the transport accepted it. A report lost on the air at the moment
  the connection breaks is therefore not resent. Closing this gap would need
  a protocol change.
- **Overflow drops the oldest reports.** This only happens after a long
  time without connection and many events (127 reports). A dropped report
  is not recovered.
- **RAM only:** the buffer is lost on a reset and in deep sleep. If there is
  no connection at the end of the day, buffered reports and the DAY_END
  would be lost before deep sleep (follow-up, task 16).
