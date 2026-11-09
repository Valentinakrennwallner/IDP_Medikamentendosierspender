# Shared BLE protocol

Document the versioned message framing, message types, serialization,
authentication inputs, sequence-number rules, and error handling here before
implementing either BLE endpoint. Keep platform-specific BLE APIs outside
this directory.

Add shared test vectors so the ESP32 and Raspberry Pi implementations can be
checked against the same valid, invalid, and replayed messages.
