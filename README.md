# Medication dispenser

The implementation lives under `code/`, with ESP32 firmware, a Raspberry Pi
gateway, and shared protocol documentation. See [the design specification](docs/design/start.pdf)
for system behavior and safety/security requirements.

## Repository layout

- `code/esp32/` contains the ESP-IDF sensor-node application and
  planned firmware modules.
- `code/raspberry-pi/` contains the POSIX C gateway application and
  planned gateway modules.
- `code/protocol/` is reserved for the shared, documented BLE message format
  and cross-device test vectors.
- `docs/` contains the design specification.

See [Code style](docs/code-style.md) for C formatting, safety, and security
conventions.

**Not for clinical use:** The repository is still under development. The
secure BLE wire contract and Raspberry Pi secure-channel implementation are
present, but dose-safety behavior, complete device integration, hardware
testing, and independent security review are still required before any use
with medication.

## Development software requirements

The project has two separate build environments. The ESP32 firmware must be
built with ESP-IDF and its supported toolchain; the Raspberry Pi gateway is
portable C11 built with CMake and a POSIX toolchain.

### Tested versions

The project was built and tested with the following versions. Use the same
versions to avoid API differences.

| Component | Version |
|---|---|
| ESP-IDF | v5.5.6 (installed with the ESP-IDF Installation Manager) |
| ESP32 toolchain | xtensa-esp-elf GCC 14.2.0 |
| Raspberry Pi OS | Ubuntu Server 26.04.1 LTS (arm64) |
| Raspberry Pi compiler | GCC 15.2.0 |
| OpenSSL | 3.5.5 |
| libcurl | 8.18.0 (OpenSSL backend) |
| D-Bus | 1.16.2 |

Do not use ESP-IDF v6.x: it moves to Mbed TLS 4 and the PSA Crypto API, while
the ESP32 secure channel uses the Mbed TLS 3 functions listed in the Design
Document (`mbedtls_ecdh_*`, `mbedtls_gcm_*`, `mbedtls_hkdf`).

### Cryptographic libraries

No cryptographic algorithm is implemented in this project; existing
implementations are used (sources are also named in the file headers):

- ESP32 (`code/esp32/components/ble/secure_channel_mbedtls.c`): Mbed TLS 3 as
  bundled with ESP-IDF v5.5.6, https://github.com/Mbed-TLS/mbedtls
- Raspberry Pi (`code/raspberry-pi/src/secure_channel/secure_channel.c`):
  OpenSSL 3, https://github.com/openssl/openssl

**Deviation from the Design Document (Chapter 2, Section 2.2):** the Design
Document names mbedTLS for both devices. On the Raspberry Pi, OpenSSL is used
instead because it is already required there by libcurl for the Telegram HTTPS
connection (Requirement 4.10), so no second cryptographic library is needed.
Algorithms (P-256 ECDHE, HKDF-SHA-256, HMAC-SHA-256, AES-256-GCM) and the wire
format (`code/protocol/SECURE_CHANNEL.md`) are identical on both devices.

### ESP32 firmware

- ESP-IDF with its ESP32 compiler toolchain, Python environment, and CMake/Ninja
  build tools installed as directed by the ESP-IDF installation guide.
- Configure the ESP-IDF environment before building:

  ```sh
  cd code/esp32
  idf.py set-target esp32
  idf.py build
  ```

- ESP-IDF provides the firmware dependencies specified by the design,
  including FreeRTOS, NimBLE, and mbedTLS.
- `code/esp32/sdkconfig.defaults` enables Bluetooth (NimBLE). If a build fails
  with `host/ble_att.h: No such file or directory`, delete
  `code/esp32/sdkconfig` and run `idf.py fullclean` so the defaults are
  applied again.

### Raspberry Pi gateway

- Raspberry Pi OS or another supported Linux environment with a C11 compiler,
  CMake, and POSIX threads.
- Development libraries/tools for BlueZ (including its D-Bus API), mbedTLS,
  OpenSSL 3, libcurl built with the OpenSSL TLS backend, and json-c. On
  Ubuntu:

  ```sh
  sudo apt install git cmake build-essential pkg-config bluez libssl-dev \
      libcurl4-openssl-dev libdbus-1-dev libjson-c-dev cppcheck
  ```
- Build from the repository root:

  ```sh
  cmake -S code/raspberry-pi -B code/raspberry-pi/build
  cmake --build code/raspberry-pi/build
  ```

### Static analysis

Both device builds register the `misra_c_screening` CTest. Install cppcheck
with its standard configuration files and MISRA addon available (Python is
needed for the addon), build the device project first, then run:

```sh
ctest --test-dir code/raspberry-pi/build -R misra_c_screening --output-on-failure
ctest --test-dir code/esp32/build -R misra_c_screening --output-on-failure
```

On Windows (ESP32 build), install cppcheck with the official installer, add
`C:\Program Files\Cppcheck` to the user `Path`, and run `ctest` from the
ESP-IDF PowerShell (it provides Python). The Windows installer does not
include the Python addons: download the source archive of the **same**
cppcheck version from https://github.com/danmar/cppcheck/releases and copy
its `addons` folder to `C:\Program Files\Cppcheck\addons`. On Ubuntu, the
`cppcheck` package already contains the addons. Tested with cppcheck 2.13
(Ubuntu) and 2.22 (Windows).

The screen (`code/tests/misra/check.cmake`) checks one device at a time: only
production code of that device and the shared protocol, with the include
paths from the build's `compile_commands.json`; test code and mocks are
excluded. Findings are classified with `code/tests/misra/rule_categories.txt`
(rule numbers and categories only; the MISRA rule texts are not reproduced).

- The test **fails on findings of Mandatory rules** (the assignment requires
  the mandatory MISRA C rules to be followed).
- Required and Advisory findings are printed per rule and listed in
  `<build>/misra_report_<device>.txt`. They are to be fixed or documented as
  deviations.
- Rule 17.3 (implicit function declarations) is enforced by the compiler
  (GCC 14 and later reject implicit declarations) instead of cppcheck, which
  reports false positives when SDK headers are not fully parsed.

The cppcheck MISRA addon is a preliminary screen covering a subset of the
rules, not proof of MISRA C compliance; see [Code style](docs/code-style.md).

## Sample configuration

`code/raspberry-pi/config/device.example.json` illustrates the
intended configuration shape. Copy it to `device.json`, replace all
placeholders, and restrict access to the service account (mode `0600`) before
running on Linux. Never commit `device.json` or real credentials.
