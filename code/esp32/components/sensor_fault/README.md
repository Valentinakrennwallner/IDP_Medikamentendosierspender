# Sensor-fault component

Implements the sensor-fault handling of Hazard 2 (Design Document, Chapter 4,
Section 4.1; Chapter 6, Section 6.1, Requirements 2.6 and 2.7) as a small
state machine without hardware dependencies. The monotonic clock is passed in
as milliseconds, so the behaviour is unit-tested on the host
(`sensor_fault_tests`).

States:

- `SENSOR_FAULT_STATE_NORMAL`: measurements are valid, the dose evaluation
  may run (`sensor_fault_evaluation_allowed()` returns true).
- `SENSOR_FAULT_STATE_FAULT`: the evaluation is suspended and the sensor is
  measured again every 10 s (`SENSOR_FAULT_RECHECK_INTERVAL_MS`).

`sensor_fault_update()` takes the status of one `weight_measure()` call (which
already includes the two repeated reads of Requirement 2.5) and returns the
action for the caller:

| Action | When | Caller |
|---|---|---|
| `SENSOR_FAULT_ACTION_ENTER_FAULT` | first failed measurement in the normal state | suspend evaluation, `led_status_set(LED_STATUS_FAULT_FLASH)`, report `REPORT_STATUS_SENSOR_FAULT` to the Raspberry Pi |
| `SENSOR_FAULT_ACTION_NONE` | valid measurement in the normal state, or failed recheck | nothing; a failed recheck schedules the next one 10 s later |
| `SENSOR_FAULT_ACTION_RESUME` | first valid recheck in the fault state | resume the dose evaluation, which sets the LED again |

The fault is reported once per fault, not on every recheck. Every
`weight_measure()` status other than `WEIGHT_MEASURE_OK`, including argument
and initialisation errors, counts as a fault, so no dose is evaluated from an
unusable sensor (fail closed).

Typical use in the weight-measurement task:

```c
if (sensor_fault_measurement_due(&fault, now_ms))
{
    status = weight_measure(&calibration, window_active, has_previous,
                            previous_grams, &measurement);
    action = sensor_fault_update(&fault, status, now_ms);
    /* ENTER_FAULT: flash red and report; RESUME: evaluation continues. */
    if (sensor_fault_evaluation_allowed(&fault))
    {
        /* dosing_evaluate() with the new measurement */
    }
}
```

Not part of this component: the LED and BLE calls and the FreeRTOS task. The
weight-measurement thread uses this state machine through
`components/weight_monitor` (`main/weight_task.c`, task 21 of
`docs/tasklist.md`); the FAULT report over BLE is still a stub.
