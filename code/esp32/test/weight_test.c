#include "../components/weight/weight.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"

#include <stdio.h>
#include <stdlib.h>

#define PULSES_PER_READ 25U
#define CONVERSION_TIMEOUT_US INT64_C(200000)
#define MAX_QUEUED_SAMPLES 4U
#define SAMPLE_NOT_READY UINT32_C(0xFFFFFFFF)

static int clock_level;
static int data_ready;
static uint32_t sample_word;
static unsigned int sample_bit;
static unsigned int rising_edges;
static unsigned int pulses_in_sample;
static int64_t now_us;
static int64_t sample_loaded_at_us;
/* Samples the HX711 delivers after the current one (weight_measure tests). */
static uint32_t queued_samples[MAX_QUEUED_SAMPLES];
static unsigned int queued_count;
static unsigned int queued_next;
static unsigned int sensor_reads;

/* Make word the current conversion; SAMPLE_NOT_READY never becomes ready. */
static void load_sample(uint32_t word)
{
    sample_word = word;
    sample_bit = 0U;
    pulses_in_sample = 0U;
    data_ready = (word != SAMPLE_NOT_READY) ? 1 : 0;
    sample_loaded_at_us = now_us;
}

/* Count one finished read and present the next queued sample, if any. */
static void finish_sample(void)
{
    sensor_reads++;
    if (queued_next < queued_count)
    {
        load_sample(queued_samples[queued_next]);
        queued_next++;
    }
}

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
            pulses_in_sample++;
        }
        else if ((level == 0U) && (clock_level != 0) &&
                 (pulses_in_sample == PULSES_PER_READ))
        {
            clock_level = 0;
            finish_sample();
        }
        else
        {
            /* No edge that changes the simulated conversion. */
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

/*
 * Return the simulated monotonic time. Once a sample that never becomes
 * ready has made weight_read() time out, the next queued sample is presented.
 */
int64_t esp_timer_get_time(void)
{
    if ((data_ready == 0) &&
        ((now_us - sample_loaded_at_us) >= CONVERSION_TIMEOUT_US))
    {
        finish_sample();
    }
    return now_us;
}

/* Load the next 24-bit HX711 conversion result. */
static void set_sample(uint32_t word)
{
    queued_count = 0U;
    queued_next = 0U;
    rising_edges = 0U;
    sensor_reads = 0U;
    clock_level = 0;
    load_sample(word);
}

/* Load a sequence of conversions, one per read (SAMPLE_NOT_READY: timeout). */
static void set_samples(const uint32_t *words, unsigned int count)
{
    unsigned int index;

    set_sample(words[0]);
    for (index = 1U; (index < count) && (index <= MAX_QUEUED_SAMPLES); ++index)
    {
        queued_samples[index - 1U] = words[index];
    }
    queued_count = index - 1U;
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

/* weight_measure() before weight_init() reports the missing initialisation. */
static int test_measure_not_initialized(void)
{
    weight_calibration_t calibration = {0, 1000.0F};
    weight_measurement_t measurement = {0, 0.0F};

    set_sample(UINT32_C(1000));
    if ((weight_measure(&calibration, true, false, 0.0F, &measurement) !=
         WEIGHT_MEASURE_NOT_INITIALIZED) ||
        (rising_edges != 0U))
    {
        return 1;
    }
    return 0;
}

/* Requirement 2.5: a valid first read needs no repetition. */
static int test_measure_first_read_valid(void)
{
    static const uint32_t samples[] = {UINT32_C(20000), UINT32_C(30000)};
    weight_calibration_t calibration = {0, 1000.0F};
    weight_measurement_t measurement = {0, 0.0F};

    set_samples(samples, 2U);
    if ((weight_measure(&calibration, true, false, 0.0F, &measurement) !=
         WEIGHT_MEASURE_OK) ||
        (measurement.grams != 20.0F) || (sensor_reads != 1U))
    {
        return 1;
    }
    return 0;
}

/* Requirement 2.5: one faulty read, the first repetition is valid. */
static int test_measure_first_retry_valid(void)
{
    static const uint32_t samples[] = {UINT32_C(0x007FFFFF), UINT32_C(21000),
                                       UINT32_C(30000)};
    weight_calibration_t calibration = {0, 1000.0F};
    weight_measurement_t measurement = {0, 0.0F};

    set_samples(samples, 3U);
    if ((weight_measure(&calibration, true, false, 0.0F, &measurement) !=
         WEIGHT_MEASURE_OK) ||
        (measurement.grams != 21.0F) || (sensor_reads != 2U))
    {
        return 1;
    }
    return 0;
}

/* Requirement 2.5: two faulty reads, the second repetition is valid. */
static int test_measure_second_retry_valid(void)
{
    static const uint32_t samples[] = {SAMPLE_NOT_READY, UINT32_C(150000),
                                       UINT32_C(22000), UINT32_C(30000)};
    weight_calibration_t calibration = {0, 1000.0F};
    weight_measurement_t measurement = {0, 0.0F};

    set_samples(samples, 4U);
    if ((weight_measure(&calibration, true, false, 0.0F, &measurement) !=
         WEIGHT_MEASURE_OK) ||
        (measurement.grams != 22.0F) || (sensor_reads != 3U))
    {
        return 1;
    }
    return 0;
}

/*
 * Requirement 2.5: three faulty reads (limit value, timeout, out of range)
 * are a sensor fault; no fourth read is made and the result is not written.
 */
static int test_measure_three_faulty_reads(void)
{
    static const uint32_t samples[] = {UINT32_C(0x00800000), SAMPLE_NOT_READY,
                                       UINT32_C(150000), UINT32_C(23000)};
    weight_calibration_t calibration = {0, 1000.0F};
    weight_measurement_t measurement = {7, 7.0F};

    set_samples(samples, 4U);
    if ((weight_measure(&calibration, true, false, 0.0F, &measurement) !=
         WEIGHT_MEASURE_SENSOR_FAULT) ||
        (sensor_reads != 3U) || (measurement.raw_counts != 7) ||
        (measurement.grams != 7.0F))
    {
        return 1;
    }
    return 0;
}

/* Requirement 2.5: three timeouts are a sensor fault. */
static int test_measure_three_timeouts(void)
{
    static const uint32_t samples[] = {SAMPLE_NOT_READY, SAMPLE_NOT_READY,
                                       SAMPLE_NOT_READY, UINT32_C(23000)};
    weight_calibration_t calibration = {0, 1000.0F};
    weight_measurement_t measurement = {0, 0.0F};
    int64_t started_at_us = now_us;

    set_samples(samples, 4U);
    if ((weight_measure(&calibration, true, false, 0.0F, &measurement) !=
         WEIGHT_MEASURE_SENSOR_FAULT) ||
        (sensor_reads != 3U) || (rising_edges != 0U) ||
        ((now_us - started_at_us) < (3 * CONVERSION_TIMEOUT_US)))
    {
        return 1;
    }
    return 0;
}

/*
 * Requirements 2.4 and 2.5: a lasting change above 5 g outside an intake
 * window is a sensor fault; the same weights inside a window are valid.
 */
static int test_measure_change_outside_intake_window(void)
{
    static const uint32_t samples[] = {UINT32_C(30000), UINT32_C(30000),
                                       UINT32_C(30000)};
    weight_calibration_t calibration = {0, 1000.0F};
    weight_measurement_t measurement = {0, 0.0F};

    set_samples(samples, 3U);
    if ((weight_measure(&calibration, false, true, 24.0F, &measurement) !=
         WEIGHT_MEASURE_SENSOR_FAULT) ||
        (sensor_reads != 3U))
    {
        return 1;
    }
    set_samples(samples, 3U);
    if ((weight_measure(&calibration, true, true, 24.0F, &measurement) !=
         WEIGHT_MEASURE_OK) ||
        (measurement.grams != 30.0F) || (sensor_reads != 1U))
    {
        return 1;
    }
    return 0;
}

/* Argument errors are returned at once, without reading the sensor. */
static int test_measure_invalid_arguments(void)
{
    weight_calibration_t calibration = {0, 1000.0F};
    weight_calibration_t zero_factor = {0, 0.0F};
    weight_measurement_t measurement = {0, 0.0F};

    set_sample(UINT32_C(1000));
    if ((weight_measure(NULL, true, false, 0.0F, &measurement) !=
         WEIGHT_MEASURE_INVALID_ARGUMENT) ||
        (weight_measure(&calibration, true, false, 0.0F, NULL) !=
         WEIGHT_MEASURE_INVALID_ARGUMENT) ||
        (weight_measure(&zero_factor, true, false, 0.0F, &measurement) !=
         WEIGHT_MEASURE_INVALID_ARGUMENT) ||
        (rising_edges != 0U) || (sensor_reads != 0U))
    {
        return 1;
    }
    return 0;
}

/* Run the HX711 weighing tests without ESP-IDF or hardware. */
int main(void)
{
    /* Must run first: every later test relies on weight_init(). */
    if (test_measure_not_initialized() != 0)
    {
        (void)fputs("weight tests failed\n", stderr);
        return EXIT_FAILURE;
    }

    if ((test_valid_reading_and_pulse_count() != 0) ||
        (test_negative_raw_value() != 0) ||
        (test_limit_values_are_faulty() != 0) ||
        (test_weight_range() != 0) ||
        (test_change_outside_intake_window() != 0) ||
        (test_conversion_timeout() != 0) ||
        (test_invalid_arguments() != 0) ||
        (test_measure_first_read_valid() != 0) ||
        (test_measure_first_retry_valid() != 0) ||
        (test_measure_second_retry_valid() != 0) ||
        (test_measure_three_faulty_reads() != 0) ||
        (test_measure_three_timeouts() != 0) ||
        (test_measure_change_outside_intake_window() != 0) ||
        (test_measure_invalid_arguments() != 0))
    {
        (void)fputs("weight tests failed\n", stderr);
        return EXIT_FAILURE;
    }

    (void)puts("weight tests passed");
    return EXIT_SUCCESS;
}
