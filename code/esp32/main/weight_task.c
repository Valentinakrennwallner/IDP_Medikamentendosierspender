/*
 * Weight-measurement thread of the ESP32 (task 21, measurement part).
 *
 * A periodic esp_timer only notifies the thread (Design Document, Chapter 2,
 * Section 2.3); the thread measures with weight_measure(), passes the
 * result to weight_monitor (stability window, sensor-fault state, 5 g
 * reference) and publishes each new stable weight in a one-element mailbox
 * queue for the dose evaluation.
 *
 * The esp_timer callback runs in the esp_timer task (ESP_TIMER_TASK), not
 * in an interrupt; it does nothing but notify, as Section 2.3 requires of
 * the interrupt routine.
 *
 * The intake window comes from the ESP32 clock (components/wall_clock,
 * task 20); without a valid time or table it is "no window" (fail closed).
 *
 * Still open in task 21 (marked "TODO(task 21)" below): FAULT report over
 * BLE (also task 15), dose evaluation (dosing_evaluate() as consumer of
 * weight_task_get_stable(), it also registers the table with the clock) and
 * the result buffer.
 *
 * Weights are health data and are never logged (Design Document, Chapter 6,
 * Section 6.2, Requirement 4.4).
 */
#include "weight_task.h"

#include <stddef.h>

#include "app_tasks.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "led_status.h"
#include "wall_clock_service.h"
#include "weight_monitor.h"

static const char *WEIGHT_TASK_LOG_TAG = "weight_task";

static weight_calibration_t task_calibration;
static weight_monitor_t task_monitor;
static QueueHandle_t stable_weight_mailbox = NULL;
static TaskHandle_t weight_task_handle = NULL;
static esp_timer_handle_t period_timer = NULL;

/*
 * Whether an intake window is active, for the 5 g change check of
 * weight_measure(): asks the ESP32 clock with the registered table of
 * expected weights. Without a valid time ("no time", Requirement 3.3),
 * without a registered table or after the day end the result is "no
 * window" (fail closed), so every weight change above 5 g counts as a
 * sensor fault. Design Document, Chapter 6, Section 6.1, Requirements 2.4,
 * 2.11 and 3.3.
 */
static bool weight_task_intake_window_active(void)
{
    return wall_clock_service_intake_window_active();
}

/*
 * Report the start of a sensor fault to the Raspberry Pi. TODO(task 21,
 * task 15): send REPORT_STATUS_SENSOR_FAULT over BLE or buffer it while
 * disconnected. Until then only a log line without health data is written.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.6.
 */
static void weight_task_report_sensor_fault(void)
{
    ESP_LOGW(WEIGHT_TASK_LOG_TAG, "Sensor fault: evaluation suspended, rechecking every 10 s");
}

/*
 * Log that valid values have returned after a sensor fault and the
 * evaluation resumes. No weight is logged (Requirement 4.4).
 * Design Document, Chapter 6, Section 6.1, Requirement 2.7.
 */
static void weight_task_log_sensor_recovered(void)
{
    ESP_LOGI(WEIGHT_TASK_LOG_TAG, "Sensor recovered: evaluation resumed");
}

/*
 * Apply the LED change requested by weight_monitor: flashing red when a
 * sensor fault starts, off when valid values return (the dose evaluation
 * sets the LED again later). Design Document, Chapter 2, Section 2.1 (LED
 * status display), and Chapter 6, Section 6.1, Requirement 2.6.
 */
static void weight_task_apply_led(weight_monitor_led_t led_request)
{
    esp_err_t result = ESP_OK;

    if (led_request == WEIGHT_MONITOR_LED_FAULT_FLASH)
    {
        result = led_status_set(LED_STATUS_FAULT_FLASH);
    }
    else if (led_request == WEIGHT_MONITOR_LED_OFF)
    {
        result = led_status_set(LED_STATUS_OFF);
    }
    else
    {
        /* WEIGHT_MONITOR_LED_NONE: keep the current LED state. */
    }

    if (result != ESP_OK)
    {
        ESP_LOGE(WEIGHT_TASK_LOG_TAG, "LED update failed: %s", esp_err_to_name(result));
    }
}

/*
 * Timer callback of the measurement period: only notifies the
 * weight-measurement thread. Design Document, Chapter 2, Section 2.3.
 */
static void weight_task_timer_callback(void *argument)
{
    (void)argument;
    if (weight_task_handle != NULL)
    {
        (void)xTaskNotifyGive(weight_task_handle);
    }
}

/*
 * One measurement cycle: measure if due (every period, every 10 s during a
 * sensor fault), update the fault state and the stability window, set the
 * LED, report a new fault and publish a new stable weight. When a fault
 * starts the mailbox is emptied so no weight from before the fault is used.
 * Design Document, Chapter 3 (ESP32 flowchart), and Chapter 6, Section 6.1,
 * Requirements 2.4 to 2.7.
 */
static void weight_task_cycle(uint64_t now_ms)
{
    weight_measurement_t measurement = {0, 0.0F};
    weight_monitor_output_t output;
    weight_measure_status_t status;
    weight_task_sample_t sample;
    float previous_weight_grams = 0.0F;
    bool has_previous;

    if (!weight_monitor_measurement_due(&task_monitor, now_ms))
    {
        return;
    }

    has_previous = weight_monitor_previous_weight(&task_monitor, &previous_weight_grams);
    status = weight_measure(&task_calibration, weight_task_intake_window_active(),
                            has_previous, previous_weight_grams, &measurement);
    if (!weight_monitor_process(&task_monitor, status, &measurement, now_ms, &output))
    {
        return;
    }

    if (output.report_sensor_fault)
    {
        (void)xQueueReset(stable_weight_mailbox);
        weight_task_report_sensor_fault();
    }
    if (output.sensor_recovered)
    {
        weight_task_log_sensor_recovered();
    }
    weight_task_apply_led(output.led_request);

    if (output.stable_weight_available)
    {
        sample.weight_grams = output.stable_weight_grams;
        sample.measured_at_ms = now_ms;
        (void)xQueueOverwrite(stable_weight_mailbox, &sample);
    }
}

/*
 * Body of the weight-measurement thread: wait for the period notification,
 * read the monotonic clock and run one measurement cycle. Never returns.
 * Design Document, Chapter 2, Section 2.3 (Table 2, priority high).
 */
static void weight_task_main(void *argument)
{
    int64_t now_us;

    (void)argument;
    for (;;)
    {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        now_us = esp_timer_get_time();
        weight_task_cycle((uint64_t)now_us / UINT64_C(1000));
    }
}

/*
 * Initialise the HX711 and start the periodic measurement timer and the
 * weight-measurement thread with the loaded calibration. Must be called
 * only once, after a valid calibration was loaded from NVS (Requirement
 * 2.8). Design Document, Chapter 2, Sections 2.1 and 2.3.
 */
esp_err_t weight_task_start(const weight_calibration_t *calibration)
{
    const esp_timer_create_args_t timer_args = {
        .callback = weight_task_timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "weight_period",
        .skip_unhandled_events = true,
    };
    esp_err_t result;

    if (calibration == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (weight_task_handle != NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    if (weight_init() != WEIGHT_RESULT_OK)
    {
        return ESP_FAIL;
    }

    task_calibration = *calibration;
    weight_monitor_init(&task_monitor);

    stable_weight_mailbox = xQueueCreate(1U, sizeof(weight_task_sample_t));
    if (stable_weight_mailbox == NULL)
    {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(weight_task_main, "weight", APP_TASK_STACK_WEIGHT_BYTES, NULL,
                    APP_TASK_PRIORITY_WEIGHT, &weight_task_handle) != pdPASS)
    {
        weight_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    result = esp_timer_create(&timer_args, &period_timer);
    if (result == ESP_OK)
    {
        result = esp_timer_start_periodic(period_timer,
                                          (uint64_t)WEIGHT_TASK_PERIOD_MS * UINT64_C(1000));
    }
    return result;
}

/*
 * Interface for the dose evaluation (still open in task 21): wait at most
 * timeout_ms for the newest stable weight and remove it from the mailbox.
 * Returns false on timeout, before weight_task_start() or on a missing
 * argument. Design Document, Chapter 3 (ESP32 flowchart), and Chapter 6,
 * Section 6.1, Requirement 2.6.
 */
bool weight_task_get_stable(weight_task_sample_t *sample, uint32_t timeout_ms)
{
    bool received = false;

    if ((sample != NULL) && (stable_weight_mailbox != NULL))
    {
        received = (xQueueReceive(stable_weight_mailbox, sample, pdMS_TO_TICKS(timeout_ms)) ==
                    pdTRUE);
    }
    return received;
}
