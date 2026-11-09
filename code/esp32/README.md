# ESP32 firmware

Build this project with ESP-IDF from this directory. The initial application
only confirms startup; it does not read the sensor or evaluate doses.

Planned module boundaries:

- `weight`: HX711 reads, calibration, filtering, and plausibility checks.
- `dosing`: schedule table and dose-state evaluation.
- `led`: RGB status handling and startup self-test.
- `ble`: NimBLE transport, heartbeat, reconnect, and buffered results.
- `secure_channel`: authenticated key agreement, encryption, and replay checks.
- `persistence`: calibration and commissioning data in NVS.

Keep dose evaluation and user-visible safety feedback local to the ESP32, as
specified in the documentation under "Software Architecture".
