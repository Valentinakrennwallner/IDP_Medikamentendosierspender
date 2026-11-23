# Weight-monitor component

Per-cycle logic of the weight-measurement thread (Design Document, Chapter 2,
Section 2.1; Chapter 3, ESP32 flowchart; Chapter 6, Section 6.1,
Requirements 2.4 to 2.7). It has no FreeRTOS or hardware dependencies, so it
is unit-tested on the host (`weight_monitor_tests`). The FreeRTOS task that
calls it is `main/weight_task.c`.

It combines three existing parts:

- the sensor-fault state machine (`components/sensor_fault`): measure every
  cycle in the normal state, every 10 s during a fault;
- the stability window (`weight_stability_add()`/`_reset()` in
  `components/weight`): a weight is only passed on once 5 consecutive
  measurements lie at most 0.04 g apart;
- the reference weight for the 5 g change check of `weight_measure()`
  (Requirement 2.4): the last **plausible** measurement. After a faulty
  measurement there is no reference until the next plausible one.

## Use

```c
if (weight_monitor_measurement_due(&monitor, now_ms))
{
    has_previous = weight_monitor_previous_weight(&monitor, &previous_grams);
    status = weight_measure(&calibration, window_active, has_previous,
                            previous_grams, &measurement);
    (void)weight_monitor_process(&monitor, status, &measurement, now_ms, &output);
    /* output.led_request, output.report_sensor_fault, output.sensor_recovered,
       output.stable_weight_available / stable_weight_grams */
}
```

`weight_monitor_process()` per result:

| Result | Stability window / reference | Output |
|---|---|---|
| first faulty result (normal -> fault) | emptied / dropped | `WEIGHT_MONITOR_LED_FAULT_FLASH`, `report_sensor_fault` (once per fault) |
| faulty recheck during a fault | emptied / dropped | nothing; next recheck in 10 s |
| first valid recheck (fault -> normal) | value added / new reference | `WEIGHT_MONITOR_LED_OFF`, `sensor_recovered` (once per fault) |
| valid result, window not yet stable | value added / new reference | nothing |
| valid result, window stable | value added / new reference | `stable_weight_available` with the mean |

Every `weight_measure()` status other than `WEIGHT_MEASURE_OK` counts as a
fault (fail closed). A stable weight is only passed on in the normal state.
After a fault, 5 fresh measurements are needed before the next stable
weight; until then the LED stays off (the dose evaluation sets it from the
next stable weight).
