# Medication dispenser

The implementation lives under `code/`, with ESP32 firmware, a Raspberry Pi
gateway, and shared protocol documentation. See [the design specification](docs/design/start.tex)
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
portable C11 built with CMake and a POSIX toolchain. **TODO:** The specification does not
pin tool versions, so use versions supported by the selected ESP-IDF release
and Raspberry Pi OS.

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

### Raspberry Pi gateway

- Raspberry Pi OS or another supported Linux environment with a C11 compiler,
  CMake, and POSIX threads.
- Development libraries/tools for BlueZ (including its D-Bus API), mbedTLS,
  OpenSSL 3, and libcurl built with the OpenSSL TLS backend.
- Build from the repository root:

  ```sh
  cmake -S code/raspberry-pi -B code/raspberry-pi/build
  cmake --build code/raspberry-pi/build
  ```

### Static analysis

Both device builds register the `misra_c_screening` CTest. Install cppcheck
with its standard configuration files and MISRA addon available, then run:

```sh
ctest --test-dir code/raspberry-pi/build -R misra_c_screening --output-on-failure
ctest --test-dir code/esp32/build -R misra_c_screening --output-on-failure
```

The ESP32 test command requires an ESP-IDF build directory to have been
configured. The cppcheck MISRA addon is a preliminary screen, not proof of
MISRA C compliance; see [Code style](docs/code-style.md).

## Sample configuration

`code/raspberry-pi/config/device.example.json` illustrates the
intended configuration shape. Copy it to `device.json`, replace all
placeholders, and restrict access to the service account (mode `0600`) before
running on Linux. Never commit `device.json` or real credentials.
