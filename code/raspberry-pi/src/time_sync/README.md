# Time synchronization module

`time_sync.c` implements the retry/state policy from requirements 1.1–1.3.
Call `time_sync_init()` with platform callbacks and call `time_sync_step()`
immediately at startup, then again after the returned delay. The
`request_sync` callback must perform an NTP attempt and return only after it
has completed. `read_synchronized_time` must reject clocks that are not
confirmed synchronized by the host NTP service. Configure that service and
provide these callbacks as part of Raspberry Pi deployment.

After three consecutive failures, `time_sync_step()` reports the unavailable
time condition through `send_unavailable_alert`, marks operation stopped, and
returns a 300-second retry delay. The gateway must not send a schedule, time,
or medication reminders while `operation_stopped` is true. A successful
attempt clears the stopped state and returns to a 3600-second interval.

Load the saved epoch using `time_sync_load_last_valid_time()` before the first
Telegram HTTPS request so the TLS integration can use it for certificate
validation before initial NTP synchronization. After each successful sync the
service retains the epoch; call `time_sync_save_last_valid_time()` during
graceful shutdown. File errors are returned to the caller.
