# Schedule module

Load and validate the medication and intake entries from the device
configuration. Compute each intake's target weight from tablet weight and
quantity. Reject invalid or missing configuration; do not send a schedule to
the ESP32 in that case.
