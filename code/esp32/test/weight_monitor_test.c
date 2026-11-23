#include <stdio.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "weight_monitor.h"

/*
 * Host tests for the per-cycle logic of the weight-measurement thread
 * (components/weight_monitor). The measurement results are passed in
 * directly and the clock is injected in milliseconds, so no HX711, timer
 * or FreeRTOS is needed. Design Document, Chapter 3 (ESP32 flowchart) and
 * Chapter 6, Section 6.1, Requirements 2.4 to 2.7.
 */

#define PERIOD_MS 1000U

/*
 * Link-only stubs for the HX711 driver in weight.c. weight_monitor only
 * uses the stability window, so the sensor is never accessed; a call
 * aborts the test.
 */
esp_err_t gpio_config(const gpio_config_t *config)
{
    (void)config;
    abort();
}

esp_err_t gpio_set_level(gpio_num_t gpio, uint32_t level)
{
    (void)gpio;
    (void)level;
    abort();
}

int gpio_get_level(gpio_num_t gpio)
{
    (void)gpio;
    abort();
}

void esp_rom_delay_us(unsigned int microseconds)
{
    (void)microseconds;
    abort();
}

int64_t esp_timer_get_time(void)
{
    abort();
}

/* Report a failed check with its line number and return 1. */
static int fail(int line)
{
    (void)fprintf(stderr, "weight_monitor_test: check failed at line %d\n", line);
    return 1;
}

#define CHECK(condition)           \
    do                             \
    {                              \
        if (!(condition))          \
        {                          \
            return fail(__LINE__); \
        }                          \
    } while (0)

/* Feed one valid measurement of the given weight. */
static bool feed_ok(weight_monitor_t *monitor, float grams, uint64_t now_ms,
                    weight_monitor_output_t *output)
{
    weight_measurement_t measurement = {0, grams};

    return weight_monitor_process(monitor, WEIGHT_MEASURE_OK, &measurement, now_ms, output);
}

/* Feed one failed measurement (sensor fault after the retries). */
static bool feed_fault(weight_monitor_t *monitor, uint64_t now_ms,
                       weight_monitor_output_t *output)
{
    weight_measurement_t measurement = {0, 0.0F};

    return weight_monitor_process(monitor, WEIGHT_MEASURE_SENSOR_FAULT, &measurement, now_ms,
                                  output);
}

/* After init: measurement due, no reference weight, nothing stable. */
static int test_initial_state(void)
{
    weight_monitor_t monitor;
    float previous = 1.0F;

    weight_monitor_init(&monitor);
    CHECK(weight_monitor_measurement_due(&monitor, 0U));
    CHECK(!weight_monitor_previous_weight(&monitor, &previous));
    CHECK(previous == 0.0F);
    return 0;
}

/*
 * A stable weight is passed on only after WEIGHT_STABLE_SAMPLE_COUNT
 * consistent measurements, and then once per further measurement.
 */
static int test_stable_weight_after_five_measurements(void)
{
    weight_monitor_t monitor;
    weight_monitor_output_t output;
    uint32_t i;

    weight_monitor_init(&monitor);
    for (i = 1U; i < WEIGHT_STABLE_SAMPLE_COUNT; ++i)
    {
        CHECK(feed_ok(&monitor, 20.0F, (uint64_t)i * PERIOD_MS, &output));
        CHECK(!output.stable_weight_available);
        CHECK(output.led_request == WEIGHT_MONITOR_LED_NONE);
        CHECK(!output.report_sensor_fault);
        CHECK(!output.sensor_recovered);
    }
    CHECK(feed_ok(&monitor, 20.0F, 5U * PERIOD_MS, &output));
    CHECK(output.stable_weight_available);
    CHECK((output.stable_weight_grams > 19.999F) && (output.stable_weight_grams < 20.001F));
    CHECK(feed_ok(&monitor, 20.0F, 6U * PERIOD_MS, &output));
    CHECK(output.stable_weight_available);
    return 0;
}

/* A short outlier (spread above 0.04 g) is not passed on as stable. */
static int test_outlier_is_not_stable(void)
{
    weight_monitor_t monitor;
    weight_monitor_output_t output;
    uint32_t i;

    weight_monitor_init(&monitor);
    for (i = 1U; i <= WEIGHT_STABLE_SAMPLE_COUNT; ++i)
    {
        CHECK(feed_ok(&monitor, 20.0F, (uint64_t)i * PERIOD_MS, &output));
    }
    CHECK(output.stable_weight_available);
    CHECK(feed_ok(&monitor, 20.5F, 6U * PERIOD_MS, &output));
    CHECK(!output.stable_weight_available);
    return 0;
}

/* Requirement 2.4: the last plausible measurement is the reference weight. */
static int test_previous_weight_is_last_plausible_measurement(void)
{
    weight_monitor_t monitor;
    weight_monitor_output_t output;
    float previous = 0.0F;

    weight_monitor_init(&monitor);
    CHECK(feed_ok(&monitor, 12.5F, PERIOD_MS, &output));
    CHECK(weight_monitor_previous_weight(&monitor, &previous));
    CHECK(previous == 12.5F);
    CHECK(feed_ok(&monitor, 13.0F, 2U * PERIOD_MS, &output));
    CHECK(weight_monitor_previous_weight(&monitor, &previous));
    CHECK(previous == 13.0F);
    return 0;
}

/*
 * Requirement 2.6: a sensor fault asks once for the flashing red LED and
 * the fault report, drops the reference weight and passes nothing on.
 * Requirement 2.7: failed rechecks every 10 s report nothing new.
 */
static int test_fault_flashes_and_reports_once(void)
{
    weight_monitor_t monitor;
    weight_monitor_output_t output;
    float previous = 0.0F;

    weight_monitor_init(&monitor);
    CHECK(feed_ok(&monitor, 20.0F, PERIOD_MS, &output));
    CHECK(feed_fault(&monitor, 2U * PERIOD_MS, &output));
    CHECK(output.led_request == WEIGHT_MONITOR_LED_FAULT_FLASH);
    CHECK(output.report_sensor_fault);
    CHECK(!output.sensor_recovered);
    CHECK(!output.stable_weight_available);
    CHECK(!weight_monitor_previous_weight(&monitor, &previous));

    CHECK(!weight_monitor_measurement_due(&monitor, 3U * PERIOD_MS));
    CHECK(!weight_monitor_measurement_due(&monitor, 2U * PERIOD_MS + 9999U));
    CHECK(weight_monitor_measurement_due(&monitor, 2U * PERIOD_MS + 10000U));
    CHECK(feed_fault(&monitor, 2U * PERIOD_MS + 10000U, &output));
    CHECK(output.led_request == WEIGHT_MONITOR_LED_NONE);
    CHECK(!output.report_sensor_fault);
    CHECK(!output.sensor_recovered);
    return 0;
}

/*
 * A fault empties the stability window: measurements before the fault do
 * not count, and after a valid recheck the recovery is signalled once and
 * the LED is switched off until five fresh measurements give a stable
 * weight again.
 */
static int test_resume_needs_fresh_stable_window(void)
{
    weight_monitor_t monitor;
    weight_monitor_output_t output;
    uint64_t now_ms = PERIOD_MS;
    uint32_t i;

    weight_monitor_init(&monitor);
    for (i = 1U; i < WEIGHT_STABLE_SAMPLE_COUNT; ++i)
    {
        CHECK(feed_ok(&monitor, 20.0F, now_ms, &output));
        now_ms += PERIOD_MS;
    }
    CHECK(feed_fault(&monitor, now_ms, &output));
    now_ms += SENSOR_FAULT_RECHECK_INTERVAL_MS;

    CHECK(weight_monitor_measurement_due(&monitor, now_ms));
    CHECK(feed_ok(&monitor, 20.0F, now_ms, &output));
    CHECK(output.led_request == WEIGHT_MONITOR_LED_OFF);
    CHECK(output.sensor_recovered);
    CHECK(!output.report_sensor_fault);
    CHECK(!output.stable_weight_available);
    CHECK(weight_monitor_measurement_due(&monitor, now_ms + PERIOD_MS));

    for (i = 2U; i < WEIGHT_STABLE_SAMPLE_COUNT; ++i)
    {
        now_ms += PERIOD_MS;
        CHECK(feed_ok(&monitor, 20.0F, now_ms, &output));
        CHECK(!output.stable_weight_available);
        CHECK(output.led_request == WEIGHT_MONITOR_LED_NONE);
        CHECK(!output.sensor_recovered);
    }
    now_ms += PERIOD_MS;
    CHECK(feed_ok(&monitor, 20.0F, now_ms, &output));
    CHECK(output.stable_weight_available);
    return 0;
}

/* A second fault after resuming is reported again. */
static int test_second_fault_reported_again(void)
{
    weight_monitor_t monitor;
    weight_monitor_output_t output;

    weight_monitor_init(&monitor);
    CHECK(feed_fault(&monitor, 0U, &output));
    CHECK(output.report_sensor_fault);
    CHECK(feed_ok(&monitor, 20.0F, SENSOR_FAULT_RECHECK_INTERVAL_MS, &output));
    CHECK(output.led_request == WEIGHT_MONITOR_LED_OFF);
    CHECK(feed_fault(&monitor, SENSOR_FAULT_RECHECK_INTERVAL_MS + PERIOD_MS, &output));
    CHECK(output.led_request == WEIGHT_MONITOR_LED_FAULT_FLASH);
    CHECK(output.report_sensor_fault);
    return 0;
}

/* Programming errors of weight_measure() count as a sensor fault (fail closed). */
static int test_programming_errors_count_as_fault(void)
{
    weight_monitor_t monitor;
    weight_monitor_output_t output;
    weight_measurement_t measurement = {0, 20.0F};

    weight_monitor_init(&monitor);
    CHECK(weight_monitor_process(&monitor, WEIGHT_MEASURE_NOT_INITIALIZED, &measurement, 0U,
                                 &output));
    CHECK(output.report_sensor_fault);
    CHECK(output.led_request == WEIGHT_MONITOR_LED_FAULT_FLASH);

    weight_monitor_init(&monitor);
    CHECK(weight_monitor_process(&monitor, WEIGHT_MEASURE_INVALID_ARGUMENT, &measurement, 0U,
                                 &output));
    CHECK(output.report_sensor_fault);
    CHECK(!output.stable_weight_available);
    return 0;
}

/* Missing arguments are rejected and change nothing. */
static int test_missing_arguments(void)
{
    weight_monitor_t monitor;
    weight_monitor_output_t output;
    weight_measurement_t measurement = {0, 20.0F};
    float previous = 0.0F;

    weight_monitor_init(NULL);
    weight_monitor_init(&monitor);
    CHECK(!weight_monitor_measurement_due(NULL, 0U));
    CHECK(!weight_monitor_previous_weight(NULL, &previous));
    CHECK(!weight_monitor_previous_weight(&monitor, NULL));
    CHECK(!weight_monitor_process(NULL, WEIGHT_MEASURE_OK, &measurement, 0U, &output));
    CHECK(!weight_monitor_process(&monitor, WEIGHT_MEASURE_OK, NULL, 0U, &output));
    CHECK(!weight_monitor_process(&monitor, WEIGHT_MEASURE_OK, &measurement, 0U, NULL));
    CHECK(!weight_monitor_previous_weight(&monitor, &previous));
    CHECK(monitor.stability.sample_count == 0U);
    return 0;
}

/* Run all weight-monitor tests; exit status 0 only if all pass. */
int main(void)
{
    int failures = 0;

    failures += test_initial_state();
    failures += test_stable_weight_after_five_measurements();
    failures += test_outlier_is_not_stable();
    failures += test_previous_weight_is_last_plausible_measurement();
    failures += test_fault_flashes_and_reports_once();
    failures += test_resume_needs_fresh_stable_window();
    failures += test_second_fault_reported_again();
    failures += test_programming_errors_count_as_fault();
    failures += test_missing_arguments();

    if (failures != 0)
    {
        (void)fprintf(stderr, "weight_monitor_test: %d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    (void)printf("weight_monitor_test: all tests passed\n");
    return EXIT_SUCCESS;
}
