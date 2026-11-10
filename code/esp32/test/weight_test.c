#include "../components/weight/weight.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include <stdio.h>
#include <stdlib.h>

#define PULSES_PER_READ 25U

static int clock_level;
static int data_ready;
static uint32_t sample_word;
static unsigned int sample_bit;
static unsigned int rising_edges;
static int64_t now_us;

/* Accept the HX711 pin configuration. */
esp_err_t gpio_config(const gpio_config_t *config)
{
    (void)config;
    return ESP_OK;
}

/* Record the clock level and count rising edges. */
esp_err_t gpio_set_level(gpio_num_t gpio, uint32_t level)
{
    if (gpio == GPIO_NUM_18)
    {
        if ((level != 0U) && (clock_level == 0))
        {
            rising_edges++;
        }
        clock_level = (int)level;
    }
    return ESP_OK;
}

/* Model the HX711: low when ready, then shift the sample out MSB first. */
int gpio_get_level(gpio_num_t gpio)
{
    int level = 1;

    if (gpio == GPIO_NUM_19)
    {
        if ((clock_level == 0) && (sample_bit == 0U))
        {
            level = (data_ready != 0) ? 0 : 1;
        }
        else if (sample_bit < 24U)
        {
            level = (int)((sample_word >> (23U - sample_bit)) & 1U);
            sample_bit++;
        }
        else
        {
            level = 0;
        }
    }
    return level;
}

/* Advance the simulated clock. */
void esp_rom_delay_us(unsigned int microseconds)
{
    now_us += (int64_t)microseconds;
}

/* Return the simulated monotonic time. */
int64_t esp_timer_get_time(void)
{
    return now_us;
}

/* Load the next 24-bit HX711 conversion result. */
static void set_sample(uint32_t word)
{
    sample_word = word;
    sample_bit = 0U;
    rising_edges = 0U;
    clock_level = 0;
    data_ready = 1;
}

/* Verify a calibrated positive reading and the 25-pulse read protocol. */
static int test_valid_reading_and_pulse_count(void)
{
    weight_calibration_t calibration = {1000, 1000.0F};
    weight_measurement_t measurement = {0, 0.0F};

    set_sample(UINT32_C(51000));
    if ((weight_init() != WEIGHT_RESULT_OK) ||
        (weight_read(&calibration, true, false, 0.0F, &measurement) !=
         WEIGHT_RESULT_OK) ||
        (measurement.raw_counts != 51000) || (measurement.grams != 50.0F) ||
        (rising_edges != PULSES_PER_READ) || (clock_level != 0))
    {
        return 1;
    }
    return 0;
}

/* Verify two's-complement decoding of negative raw values. */
static int test_negative_raw_value(void)
{
    weight_calibration_t calibration = {0, 1000.0F};
    weight_measurement_t measurement = {0, 0.0F};

    set_sample(UINT32_C(0x00FFFC18));
    if ((weight_read(&calibration, true, false, 0.0F, &measurement) !=
         WEIGHT_RESULT_OK) ||
        (measurement.raw_counts != -1000) || (measurement.grams != -1.0F))
    {
        return 1;
    }
    return 0;
}

/* Requirement 2.2: both 24-bit limit values are faulty. */
static int test_limit_values_are_faulty(void)
{
    weight_calibration_t calibration = {0, 1000.0F};
    weight_measurement_t measurement = {0, 0.0F};

    set_sample(UINT32_C(0x007FFFFF));
    if (weight_read(&calibration, true, false, 0.0F, &measurement) !=
        WEIGHT_RESULT_SENSOR_FAULT)
    {
        return 1;
    }
    set_sample(UINT32_C(0x00800000));
    if (weight_read(&calibration, true, false, 0.0F, &measurement) !=
        WEIGHT_RESULT_SENSOR_FAULT)
    {
        return 1;
    }
    return 0;
}

/* Requirement 2.3: weights outside -5 g to 100 g are implausible. */
static int test_weight_range(void)
{
    weight_calibration_t calibration = {0, 1000.0F};
    weight_measurement_t measurement = {0, 0.0F};

    set_sample(UINT32_C(100000));
    if (weight_read(&calibration, true, false, 0.0F, &measurement) !=
        WEIGHT_RESULT_OK)
    {
        return 1;
    }
    set_sample(UINT32_C(100001));
    if (weight_read(&calibration, true, false, 0.0F, &measurement) !=
        WEIGHT_RESULT_IMPLAUSIBLE)
    {
        return 1;
    }
    set_sample(UINT32_C(0x00FFEC78));
    if (weight_read(&calibration, true, false, 0.0F, &measurement) !=
        WEIGHT_RESULT_OK)
    {
        return 1;
    }
    set_sample(UINT32_C(0x00FFEC77));
    if (weight_read(&calibration, true, false, 0.0F, &measurement) !=
        WEIGHT_RESULT_IMPLAUSIBLE)
    {
        return 1;
    }
    return 0;
}

/* Requirement 2.4: a change above 5 g is implausible only outside a window. */
static int test_change_outside_intake_window(void)
{
    weight_calibration_t calibration = {0, 1000.0F};
    weight_measurement_t measurement = {0, 0.0F};

    set_sample(UINT32_C(30000));
    if (weight_read(&calibration, false, true, 24.0F, &measurement) !=
        WEIGHT_RESULT_IMPLAUSIBLE)
    {
        return 1;
    }
    set_sample(UINT32_C(30000));
    if (weight_read(&calibration, true, true, 24.0F, &measurement) !=
        WEIGHT_RESULT_OK)
    {
        return 1;
    }
    set_sample(UINT32_C(30000));
    if (weight_read(&calibration, false, true, 25.0F, &measurement) !=
        WEIGHT_RESULT_OK)
    {
        return 1;
    }
    return 0;
}

/* Requirement 2.1: no conversion within 200 ms is a timeout. */
static int test_conversion_timeout(void)
{
    weight_calibration_t calibration = {0, 1000.0F};
    weight_measurement_t measurement = {0, 0.0F};
    int64_t started_at_us;

    set_sample(UINT32_C(1000));
    data_ready = 0;
    started_at_us = now_us;
    if ((weight_read(&calibration, true, false, 0.0F, &measurement) !=
         WEIGHT_RESULT_TIMEOUT) ||
        ((now_us - started_at_us) < INT64_C(200000)) ||
        ((now_us - started_at_us) > INT64_C(210000)))
    {
        return 1;
    }
    return 0;
}

/* Verify invalid calibration and missing arguments are rejected. */
static int test_invalid_arguments(void)
{
    weight_calibration_t calibration = {0, 0.0F};
    weight_measurement_t measurement = {0, 0.0F};

    set_sample(UINT32_C(1000));
    if ((weight_read(&calibration, true, false, 0.0F, &measurement) !=
         WEIGHT_RESULT_INVALID_ARGUMENT) ||
        (weight_read(NULL, true, false, 0.0F, &measurement) !=
         WEIGHT_RESULT_INVALID_ARGUMENT) ||
        (weight_read(&calibration, true, false, 0.0F, NULL) !=
         WEIGHT_RESULT_INVALID_ARGUMENT))
    {
        return 1;
    }
    return 0;
}

/* Run the HX711 weighing tests without ESP-IDF or hardware. */
int main(void)
{
    if ((test_valid_reading_and_pulse_count() != 0) ||
        (test_negative_raw_value() != 0) ||
        (test_limit_values_are_faulty() != 0) ||
        (test_weight_range() != 0) ||
        (test_change_outside_intake_window() != 0) ||
        (test_conversion_timeout() != 0) ||
        (test_invalid_arguments() != 0))
    {
        (void)fputs("weight tests failed\n", stderr);
        return EXIT_FAILURE;
    }

    (void)puts("weight tests passed");
    return EXIT_SUCCESS;
}
