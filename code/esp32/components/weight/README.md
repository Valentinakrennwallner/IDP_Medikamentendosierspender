# Weight component

`weight_init()` configures HX711 data on GPIO 19 and clock on GPIO 18.
`weight_read()` waits up to 200 ms for a conversion, reads the 24-bit sample
inside a short critical section, and rejects HX711 limit values, weights
outside -5 g to 100 g, and changes above 5 g outside an intake window. Supply
the commissioning tare offset and finite, non-zero signed counts-per-gram
calibration and serialize calls from the weight-measurement task.

`weight_measure()` wraps `weight_read()` with the retries of Requirement 2.5:
after a faulty read (timeout, HX711 limit value, or implausible weight) the
sensor is read up to two more times, and `WEIGHT_MEASURE_SENSOR_FAULT` is
returned only if both repeated reads are faulty as well. Invalid arguments and
a missing `weight_init()` are returned at once without reading the sensor. The
measurement is written only on `WEIGHT_MEASURE_OK`. Use `weight_measure()` in
the application; `weight_read()` remains the single-read building block.

The caller remains responsible for 10-second fault recovery, fault
indication/reporting, calibration persistence, and filling checks.

This component implements the weight-measurement module and HX711 timing/pin
requirements in the Design Document, Chapter 2, Sections 2.1, 2.3, and 2.4,
and Chapter 6, Section 6.1, Requirements 2.1-2.5. Calibration and recovery
integration for Requirements 2.6-2.10 remains to be implemented by the
application.
