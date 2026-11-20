# Logging module

Log startup, BLE connection/authentication state, reconnect attempts, and
transport faults without medication names, intake times, dose quantities,
weights, PSKs, ephemeral keys, session keys, or message payloads. Do not log
the configured peer address or raw GATT data.

Requirements: Design Document, Chapter 6, Section 6.2, Requirement 4.4 and
Section 6.3, Requirements 5.1-5.4.

## Output

Log lines go to the terminal (standard output) only and are never written
to a file (Requirement 5.1). Each line has a UTC timestamp, the level and
the module name (Requirement 5.2):

```
2026-11-20T10:15:03Z [INFO] [main] Gateway started, version 0.1.0
2026-11-20T10:15:04Z [WARN] [ble] Reconnect failed
2026-11-20T10:15:09Z [ERROR] [telegram] Send failed (code 7)
```

`log_init()` puts the start-up message with the software version into the
queue before the logging thread is created, so it cannot be dropped and is
always the first line (Requirement 5.3). The version is the CMake
`project(... VERSION 0.1.0)` value, passed to the code as
`GATEWAY_SOFTWARE_VERSION`.

## API

```c
log_result_t log_init(const log_output_t *output); /* NULL = terminal */
void log_info(const char *module, const char *message);
void log_warn(const char *module, const char *message);
void log_error(const char *module, const char *message);
void log_error_code(const char *module, const char *message, int32_t code);
uint32_t log_get_dropped_count(void);
void log_shutdown(void);
```

The API is deliberately **not variadic**. A printf-style
`log_info(module, ...)` needs `<stdarg.h>`, which MISRA C:2012 Rule 17.1
(Required) does not allow, and only advisory rules may be deviated (see
`docs/code-style.md`). Messages are fixed texts. The only number a caller
can add is the integer code of `log_error_code()`, meant for status/error
codes and counters (for example an `errno` value), never for doses,
weights or intake times.

`log_output_t` lets the host test replace the terminal with a fake output.
The gateway passes `NULL`.

## Health data filter (Requirement 4.4)

Every message is checked before it is queued:

- module name: `[a-z_]`, 1-15 characters;
- message: printable ASCII, 1-120 characters, **no digits and no `%`**.

Without digits, a message cannot contain intake times (`08:00`), doses
(`2 tablets`) or weights (`12.5 g`), and without `%` it cannot be a format
string. A message that fails is replaced by
`[ERROR] [<module>] Log message rejected` (module `logging` if the module
name itself is invalid), so the faulty call is visible without leaking
anything. The filter cannot recognise medication names: callers must never
pass configuration text (medication names, Telegram texts, BLE addresses)
as a message.

## Thread and queue (Requirement 5.4)

- Callers copy their entry into a static ring buffer of 32 entries. There
  is no dynamic allocation.
- Callers never wait: they try the mutex at most 100 times
  (`LOG_LOCK_ATTEMPTS`, `pthread_mutex_trylock()`, no sleep) and drop the
  message if it stays busy or the queue is full. Drops are counted, and the
  logging thread prints one `[WARN] [logging] Log messages dropped (code N)`
  line.
- The logging thread holds the mutex only while copying one entry out of
  the buffer, never during `write()`, so a slow terminal cannot make
  callers drop messages.
- The logging thread lowers itself to `SCHED_IDLE`, the lowest Linux
  priority, which needs no root rights (Table 2 in Section 2.3 gives
  logging the lowest priority). If that is refused, it keeps running at the
  default priority. `SCHED_IDLE` is a Linux extension; CMake passes
  `_GNU_SOURCE` for `logging.c` only.
- If `write()` fails, logging switches itself off; every later `log_*`
  call returns at once and the gateway continues. `SIGPIPE` is blocked in
  the logging thread, so a closed terminal or pipe makes `write()` fail
  instead of ending the process. If `log_init()` cannot create the thread,
  logging stays off as well.
- Numbers and timestamps are formatted by hand and written with `write()`,
  so the module does not use `<stdio.h>` (MISRA C:2012 Rule 21.6).
- `log_shutdown()` stops accepting messages, lets the thread print what is
  queued, and joins it.

## Tests

`test/logging_test.c` (`ctest -R logging_tests`) checks the line format and
the start-up message, every filter rule, that callers can fill the whole
queue while `write()` is blocked (the mutex is free during `write()`), that
a message logged from inside `write()` is accepted, drop counting and
reporting, that an output failure switches logging off, that the start-up
message is the first line even while four threads log at once, and the
init/shutdown lifecycle. All waits in the test are bounded.

## Known limitations

- MISRA screening: `<signal.h>` (Rule 21.5, for blocking `SIGPIPE`) and
  `<time.h>` (Rule 21.10) are covered by Required deviation records in
  `docs/misra-deviations.md` (group approval pending). Their findings stay
  visible in the screening report.
- The other gateway modules do not call the logging API yet.
