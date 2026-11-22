# Weight component

`weight_init()` configures HX711 data (DT) on GPIO 33 and clock (SCK) on
GPIO 32.
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

The 10-second recheck and the fault/resume decisions after
`WEIGHT_MEASURE_SENSOR_FAULT` are handled by the `sensor_fault` component
(Requirements 2.6 and 2.7). The application remains responsible for the
fault indication (LED) and reporting, calibration persistence, and filling
checks.

`weight_measure_tare()` averages `WEIGHT_TARE_SAMPLE_COUNT` (10) raw samples
of the empty container and returns the tare offset; a faulty sample aborts
the measurement. It is part of the commissioning calibration (Requirement
2.8); the caller stores the tare offset in NVS (`persistence` component).
`WEIGHT_COUNTS_PER_GRAM` (17660 counts per gram) is the calibration factor of
the assembled load cell, determined with a 1-euro coin (7.5 g) and verified
with a 2-euro coin, a 10-cent coin and tablets to +-0.01 g.

`weight_stability_add()` keeps the last `WEIGHT_STABLE_SAMPLE_COUNT` (5)
plausible weights. Only if they lie at most
`WEIGHT_STABLE_MAXIMUM_SPREAD_GRAMS` (0.04 g, i.e. +-0.02 g) apart does it
return true and the mean of the five values; use only this value for the
filling check and the dose evaluation. An outlier (placing or removing
tablets, a finger on the tray) stays in the window for five measurements, so
it never changes the LED state. Call `weight_stability_reset()` at start-up
and after every `WEIGHT_MEASURE_SENSOR_FAULT` from `weight_measure()` (single
faulty reads are already retried inside `weight_measure()`).

## Deviations from the Design Document

- HX711 on GPIO 33 (DT) and GPIO 32 (SCK) instead of GPIO 19 and GPIO 18
  (Chapter 2, Section 2.4, Table 4 and Figure 3), because GPIO 18/19 cannot
  be reached on the breadboard of the assembled hardware.

This component implements the weight-measurement module and HX711 timing/pin
requirements in the Design Document, Chapter 2, Sections 2.1, 2.3, and 2.4,
and Chapter 6, Section 6.1, Requirements 2.1-2.5. Calibration and recovery
integration for Requirements 2.6-2.10 remains to be implemented by the
application.
