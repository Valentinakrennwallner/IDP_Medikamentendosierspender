# MISRA C deviations and screening triage

MISRA C is the project's safety baseline (see [Code style](code-style.md) and
Design Document, Chapter 6, Section 6.3, Requirement 6.1). This document
records every rule the project has deliberately not followed and lists the
findings that are still open.

The `misra_c_screening` test runs the cppcheck MISRA addon, which checks only
a subset of the rules. A passing run is not a claim of compliance.

## How deviations are managed

The process is described in [Code style](code-style.md). There are two
kinds of records:

- **Advisory deviations** (`## Rule N.M - <title>`). The rules are listed in
  `code/tests/misra/deviated_rules.cmake` and passed to cppcheck as
  suppressions. They are suppressed for the whole code base, so the scope
  limits in each record are enforced by review, not by the tool.
- **Required deviations** (`## Required deviation: Rule N.M - <title>`),
  following MISRA Compliance:2020. Each record states the rationale, a scope
  limited to named files, a risk assessment, the mitigation, and the group's
  approval. These rules are **not** suppressed: their findings stay in the
  screening report, and reviewers check that every finding lies within the
  recorded scope. Mandatory rules are never deviated.

The `misra_deviation_list` test fails if:

- `deviated_rules.cmake` and the `## Rule N.M` records differ, or an
  advisory record lacks a category, justification, or conditions;
- a rule in `deviated_rules.cmake` is not Advisory;
- a `Required deviation` record is not for a Required rule, its rule is
  listed in `deviated_rules.cmake`, or it lacks one of its fields. A
  pending approval is reported but does not fail the test.

Categories are checked against `code/tests/misra/rule_categories.txt`
(MISRA C:2012); the cppcheck addon itself does not print them.

## Rule 15.1 - goto statements

**Category:** Advisory

**Justification:** Functions that acquire several resources (OpenSSL and
mbedTLS contexts, libcurl handles, heap blocks, D-Bus messages) release them
on one path at the end of the function. The alternatives are nested
conditions or duplicated cleanup, which are harder to read and to check for
leaks.

**Conditions:**

- A `goto` jumps forward only, to the single cleanup label (`cleanup`,
  `failure`, or `finish`) at the end of its own function, so rules 15.2 and
  15.3 are met.
- Every variable used after the label is declared at the top of the function
  and initialised before the first `goto`; no jump skips an initialised
  declaration.
- No jump leaves or enters a loop or `switch`.
- `telegram_send_message()` hides its `goto` in the function-local macro
  `TELEGRAM_SETOPT`. This is accepted for now; new code must not hide a
  `goto` in a macro.

**Evidence (2026-11-19):** All 46 `goto` statements, in
`secure_channel_mbedtls.c`, `bluez_central.c`, `telegram.c`,
`secure_channel.c`, and `time_sync_test.c`, are forward jumps to one label in
the same function. GCC 13 `-Wjump-misses-init` reports nothing for
`bluez_central.c`, `secure_channel.c`, and `time_sync_test.c`. `telegram.c`
needs libcurl headers that are not installed in the WSL build environment, so
it was checked by reading. The screening reports no 15.2 or 15.3 findings.

## Rule 15.5 - single point of exit

**Category:** Advisory

**Justification:** Functions validate their arguments first and return an
error status immediately ("validate all external input before using it" in
[Code style](code-style.md)). Forcing one exit would nest the normal path or
add status flags, which increases complexity.

**Conditions:**

- No `return` occurs while a resource is held. Every early `return` in a
  region guarded by a mutex is preceded by the unlock, and no `return` occurs
  between `portENTER_CRITICAL()` and `portEXIT_CRITICAL()`.
- A function that holds heap memory, library contexts, or handles uses the
  single cleanup label allowed by the Rule 15.1 record.
- Every early `return` carries an error status; a failed check is never
  silently ignored.

**Evidence (2026-11-19):** All 10 mutex-protected regions in
`bluez_central.c` and the critical section in `weight.c` were read; none can
return while locked. In `telegram.c`, every early `return` comes before the
first resource is acquired.

## Rule 2.5 - unused macro declarations

**Category:** Advisory

**Justification:** Two cases are reported but are not dead code.

- Macros in the protocol headers under `code/protocol/include` form the
  contract in `SECURE_CHANNEL.md` for two separately built programs. A given
  build uses only part of them.
- A function-local macro that is `#undef`ed after use is reported even though
  it is used (`TELEGRAM_SETOPT` in `telegram.c`).

**Conditions:**

- A macro in a `.c` file must be used. `BLUEZ_OBJECT_MANAGER` in
  `bluez_central.c` is unused and must be removed, not covered by this
  deviation.
- `MEDDISP_BLE_REQUIRED_ATT_MTU` and `MEDDISP_BLE_REASSEMBLY_TIMEOUT_MS` are
  unused because the ESP32 and the gateway each define an equivalent value
  (`REASSEMBLY_TIMEOUT_US` in `ble_secure_transport.c`, `BLE_FRAGMENT_TIMEOUT_MS`
  in `bluez_central.c`). Both should use the shared constants.

**Evidence (2026-11-19):** The screening reported 4 findings: the two
protocol macros, `BLUEZ_OBJECT_MANAGER`, and `TELEGRAM_SETOPT`.

## Rule 8.9 - objects defined at block scope when used by one function

**Category:** Advisory

**Justification:** Module state is declared at file scope as `static` in one
block at the top of each file. This keeps the state that survives between
calls visible in one place, which is what `reset_session()` and the init
functions must reset.

- `static const char *TAG` is the ESP-IDF logging convention. It becomes
  shared by every function in the module as logging is added (Requirement
  5.2 needs the module name in every message).
- The NimBLE GATT tables (`service_uuid`, `characteristics`, `services`)
  reference each other and must stay valid for the lifetime of the host
  stack.
- `next_message_id`, `transport_started`, `weight_mux`, and
  `curl_initialization_once` are used by one function today but are module
  state by design.

**Conditions:** The object has internal linkage (`static`), belongs to one
module, and is not shared through a header. Non-static file-scope objects are
not covered.

**Evidence (2026-11-19):** The screening reported 8 findings: 4 in
`ble_secure_transport.c`, and one each in `led_status.c`, `weight.c`,
`app_main.c`, and `telegram.c`.

## Required deviation: Rule 21.10 - standard library time and date functions

**Category:** Required

**Rationale:** The Design Document (Chapter 2, Section 2.2) names
`clock_gettime()` as the function that reads the NTP-synchronised system
time on the Raspberry Pi. There is no POSIX way to read a clock or to get
`time_t`/`struct timespec` without `<time.h>`. The gateway needs it for:

- monotonic timers for BLE timeouts, reassembly and reconnect intervals
  (`clock_gettime(CLOCK_MONOTONIC)`);
- the verification time of the Telegram server certificate (`time_t` passed
  to `X509_VERIFY_PARAM_set_time()`), which must be the synchronised time
  or the last valid time saved at shutdown (Chapter 6, Section 6.2,
  Requirement 4.10);
- the UTC timestamp of every log message (`clock_gettime(CLOCK_REALTIME)`,
  `gmtime_r()`; Chapter 6, Section 6.3, Requirement 5.2).

**Scope:** Raspberry Pi only, in these files:

| File | Use |
|---|---|
| `code/raspberry-pi/src/ble/bluez_central.c` | `clock_gettime(CLOCK_MONOTONIC)` |
| `code/raspberry-pi/src/notifications/telegram.c` | `time_t` certificate verification time |
| `code/raspberry-pi/src/logging/logging.c` | `clock_gettime(CLOCK_REALTIME)`, `gmtime_r()` |

The ESP32 code and the shared `code/protocol` code are not covered. Any
other file that includes `<time.h>` needs this record to be updated first.

**Risk assessment:** The rule exists because several `<time.h>` functions
have unspecified, implementation-defined or undefined behaviour: `time()`
and `mktime()` may return `(time_t)-1`, `localtime()`/`gmtime()` return a
shared static buffer (not thread-safe), `strftime()`/`asctime()` can
overflow buffers, the range and encoding of `time_t` are
implementation-defined, and the wall clock can jump when NTP corrects it.
In this system, a wrong time could start a timer too late, accept an
expired certificate, or produce a wrong log timestamp. The dose evaluation
runs on the ESP32 and does not depend on these files.

**Mitigation:**

- Only `clock_gettime()`, `gmtime_r()` and the types `time_t`,
  `struct timespec` and `struct tm` are used. `time()`, `mktime()`,
  `localtime()`, `gmtime()`, `strftime()`, `asctime()` and `ctime()` are not
  used (checked 2026-11-20).
- Every `clock_gettime()` and `gmtime_r()` result is checked. On failure,
  `bluez_central.c` returns 0 ms, `logging.c` prints the fixed timestamp
  `0000-00-00T00:00:00Z`, and `telegram.c` refuses to send with a
  verification time that is not positive.
- Timers use `CLOCK_MONOTONIC`, which NTP corrections do not move. The wall
  clock is only used for the certificate check and log timestamps.
- The gateway targets 64-bit Ubuntu Server on the Raspberry Pi 4B, where
  `time_t` has 64 bits (no year-2038 overflow).
- The screening keeps reporting every 21.10 finding; a finding in a file
  not listed above is a violation.

**Approved by:** _pending: group approval (names, date)_

## Required deviation: Rule 21.5 - the standard header file `<signal.h>`

**Category:** Required

**Rationale:** On Linux, writing to a closed terminal or pipe raises
`SIGPIPE`, whose default action terminates the process. Requirement 5.4
(Chapter 6, Section 6.3) says that operation must continue without logging
if the log output fails, so the logging thread blocks `SIGPIPE`; `write()`
then fails with `EPIPE` and logging switches itself off. In addition, the
gateway must save the last valid system time at shutdown (Chapter 6,
Section 6.2, Requirement 4.10), which needs the termination request
(`SIGTERM`, for example from systemd) to end the program in an orderly way.
POSIX offers no way to block or handle signals without `<signal.h>`.

**Scope:** Raspberry Pi only, in these files:

| File | Use |
|---|---|
| `code/raspberry-pi/src/logging/logging.c` | `sigemptyset()`, `sigaddset()`, `pthread_sigmask(SIG_BLOCK)` for `SIGPIPE` in the logging thread |
| `code/raspberry-pi/src/main.c` (planned) | handling of `SIGTERM` for a clean shutdown; not implemented yet |

The ESP32 code and the shared `code/protocol` code are not covered. The
`main.c` entry must be completed (functions used) when it is implemented.

**Risk assessment:** The rule exists because signal handling has undefined
and implementation-defined behaviour: a handler that calls functions that
are not async-signal-safe or accesses shared data can corrupt state,
`signal()` semantics differ between implementations, and `raise()` with a
handler can re-enter code. Blocking a signal in the wrong thread could also
hide a signal another module relies on.

**Mitigation:**

- `logging.c` installs no signal handler. It only blocks `SIGPIPE` in its
  own thread with `pthread_sigmask()`, which is thread-safe; the other
  threads are not affected. `signal()` and `raise()` are not used.
- libcurl does not use signals in `telegram.c` (`CURLOPT_NOSIGNAL`).
- For `main.c`: termination signals must be received with `sigaction()`
  and a handler that only sets a `volatile sig_atomic_t` flag (or with
  `sigwait()` in a dedicated thread). Saving the last valid time happens
  in normal program context, never in the handler.
- The screening keeps reporting every 21.5 finding; a finding in a file not
  listed above is a violation.

**Approved by:** _pending: group approval (names, date)_

## Open screening findings (2026-11-19)

Run with cppcheck 2.14, the project include paths, and the four deviated
rules skipped. The `misra_c_screening` test still fails. Without the include
paths and deviations the baseline was 860 reported items; it is now 395,
of which 242 are MISRA rule findings, 78 are `misra-config` notices, and 75
are other cppcheck messages.

### Mandatory rules

| Rule | Total | Production | Test | Assessment |
|---|---|---|---|---|
| 17.3 implicit declaration | 94 | 81 | 13 | Not a violation. See below. |

All 94 findings are calls to SDK or system functions whose headers cppcheck
cannot see (`dbus_*`, `pthread_*`, `mbedtls_*`, `esp_*`, `EVP_*`) or the
`INT64_C`/`UINT64_C` macros from `<stdint.h>`. None names a project function,
and every Raspberry Pi source and test builds with
`-Wall -Wextra -Wpedantic -Werror`, which rejects implicit declarations. The
ESP32 BLE transport was type-checked against API stubs. There are no real
mandatory violations. The findings go away only when the ESP-IDF and system
headers are given to cppcheck.

### Required rules, production code

| Rule | Count | Assessment | Where |
|---|---|---|---|
| 10.2 | 1 | Fix: remove the upper-casing; the later `strcasecmp()` is already case-insensitive. | `bluez_central.c:1421` |
| 10.7 | 1 | Fix. | `secure_channel.c:76` |
| 10.8 | 2 | Fix: add the two `int` lengths before the cast. | `secure_channel.c:672`, `:763` |
| 11.8 | 2 | Fix: copy to a local buffer instead of casting away `const` for the OpenSSL calls. | `secure_channel.c:158`, `:757` |
| 15.7 | 7 | Fix: add a final `else`. | `ble_secure_transport.c:502`, `:515`; `heartbeat_client.c:179`; `secure_channel_mbedtls.c:411`; `bluez_central.c:178`; `telegram.c:87`; `secure_channel.c:571` |
| 17.7 | 1 | Fix: `main()` ignores the result of `puts()`. Fixed (2026-11-20): `main()` prints the start-up message through the logging module. | `main.c:6` |
| 21.14 | 4 | Fix: `memcmp()` compares the 4-byte `"MDP1"` magic; use a byte array. | `secure_channel_mbedtls.c:399`, `:510`; `secure_channel.c:337`, `:724` |
| 8.4 | 1 | Fix: `app_main()` has no prior declaration. | `app_main.c:7` |
| 21.3 | 7 | Fix: `malloc`/`calloc`/`free` in `bluez_central.c` and `telegram.c`. Fixed in both secure channels (2026-11-20): session state is a static pool, see below. | `bluez_central.c:1404`, `:1410`, `:1496`; `telegram.c:248`, `:251` |
| 21.6 | 4 | Fix, or deviate for `time_sync.c`, which persists the last valid time with file I/O. Fixed in `main.c` (2026-11-20); the logging module writes with `write()` and formats numbers itself, without `<stdio.h>`. | `bluez_central.c:10`, `telegram.c:9`, `time_sync.c:5` |
| 21.10 | 3 | Covered by the record "Required deviation: Rule 21.10" above (2026-11-20, approval pending). Findings stay visible in the screening. | `bluez_central.c:14`, `telegram.c:12`, `logging.c:27` |
| 21.1 | 1 | Fix: pass `_POSIX_C_SOURCE` as a compile definition in CMake instead of `#define`. | `bluez_central.c:1` |
| 22.10 | 2 | Probably a false positive: `errno` is tested right after `strtoimax()` and `write()`, which set it; `errno` is set to zero before each call. Needs review. | `time_sync.c:184`, `logging.c:101` |
| 21.5 | 1 | Covered by the record "Required deviation: Rule 21.5" above (2026-11-20, approval pending). Findings stay visible in the screening. | `logging.c:25` |
| 7.4 | 5 | Probably a false positive: `char x[] = "..."` copies the literal into an array. Needs review. | `secure_channel.c:118`, `:119`, `:152`, `:191`, `:263` |
| 7.2 | 1 | Artefact: `UINT64_C()` adds the suffix once `<stdint.h>` is visible. | `startup_message.c:16` |
| 9.5 | 2 | Probably a false positive: the designators initialise struct members, not array elements. | `ble_secure_transport.c:416`, `:427` |
| 5.6 | 4 | Artefact: the ESP32 and the gateway define the same typedef names and are never linked together. | `heartbeat_client.h:14`, `heartbeat_monitor.h:12`, `secure_channel_mbedtls.c:26`, `secure_channel.c:19` |
| 5.7 | 2 | Artefact: same cause as 5.6, for struct tags. | `secure_channel_mbedtls.c:27`, `secure_channel.c:20` |
| 8.6 | 11 | Artefact: both `secure_channel` backends define the same five functions, and `main()` is defined in `main.c` and in the test executables. | `secure_channel_mbedtls.c`, `secure_channel.c`, `main.c` |

The artefacts exist because the screening analyses the ESP32 and gateway code
together as one program. Screening each program separately would remove them.

#### Rule 21.3 and third-party libraries (known limitation)

Both secure-channel backends keep their session state in a fixed static pool
(`SECURE_CHANNEL_POOL_SIZE`, default 1 per device; only host tests override
it) instead of `calloc`/`free`. Rule 21.3 is applied to the project's own
code only: OpenSSL (Raspberry Pi) and Mbed TLS (ESP32) still allocate heap
memory internally, for example for `EVP_PKEY` and `EVP_*_CTX` objects and
for Mbed TLS bignum limbs of keys and points. Every such allocation is
released on all paths (see Rule 15.1), and an allocation failure in the
library is returned as an error that ends the session. Removing it would
need a library configuration without heap use (for example an Mbed TLS
`MBEDTLS_MEMORY_BUFFER_ALLOC_C` static buffer), which is not part of the
current design. The cppcheck screen does not analyse library code.

### Required rules, test code

These are 8.6 (7, one `main()` per test executable), 9.3 (6), 10.8 (1), and
21.6 (7, `stdio` for test output). MISRA C governs the delivered software, so
a decision is needed whether `test/` and `mocks/` stay in the screening.

### Advisory rules not covered by a deviation

| Rule | Count (production / test) |
|---|---|
| 11.5 void pointer conversion | 14 / 15 |
| 5.9 internal identifiers unique | 3 / 8 |
| 12.1 explicit operator precedence | 11 / 0 |
| 8.7 external linkage used by one unit | 8 / 0 (public API not yet called) |
| 13.3 `++` mixed with other side effects | 2 / 0 |
| 13.4 assignment result used | 2 / 0 |
| 18.4 pointer arithmetic | 2 / 0 |
| 15.4 more than one `break`/`goto` in a loop | 1 / 0 |
| 17.8 parameter modified | 1 / 0 |
| 20.5 `#undef` | 1 / 0 |

Each needs either a fix or its own deviation record.

### Other cppcheck messages

- 78 `misra-config` notices: SDK constants cppcheck cannot see (`CURLE_OK`,
  `DBUS_TYPE_*`, `ESP_OK`, `MBEDTLS_*`, `EVP_*`).
- 55 `unusedStructMember`: struct members declared in headers.
- 8 `normalCheckLevelMaxBranches`: informational.
- 3 `variableScope`, 3 `constVariablePointer`, 2 `unreadVariable`, 1
  `constParameterPointer`, 1 `constParameterCallback`: small cleanups.
- 2 `autoVariables` in `ble_central_send()`: the send request lives on the
  caller's stack while the worker thread sends it. The caller waits until the
  request completes, so this is intended, but it should be kept in review.
