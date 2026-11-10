# Weight component

`weight_init()` configures HX711 data on GPIO 19 and clock on GPIO 18.
`weight_read()` waits up to 200 ms for a conversion, reads the 24-bit sample
inside a short critical section, and rejects HX711 limit values, weights
outside -5 g to 100 g, and changes above 5 g outside an intake window. Supply
the commissioning tare offset and finite, non-zero signed counts-per-gram
calibration and serialize calls from the weight-measurement task. The caller
remains responsible for retries, 10-second fault recovery, fault
indication/reporting, calibration persistence, and filling checks.

This component implements the weight-measurement module and HX711 timing/pin
requirements in the Design Document, Chapter 2, Sections 2.1, 2.3, and 2.4,
and Chapter 6, Section 6.1, Requirements 2.1-2.4. Calibration and recovery
integration for Requirements 2.5-2.10 remains to be implemented by the
application.
