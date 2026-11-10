#include "weight.h"

#include <math.h>
#include <stddef.h>

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

#define HX711_DATA_GPIO GPIO_NUM_19
#define HX711_CLOCK_GPIO GPIO_NUM_18
#define HX711_TIMEOUT_US \
    ((int64_t)WEIGHT_READ_TIMEOUT_MS * INT64_C(1000))
#define HX711_POLL_DELAY_US 10U
#define HX711_CLOCK_DELAY_US 1U
#define HX711_BITS 24U
#define HX711_SIGN_BIT UINT32_C(0x00800000)
#define HX711_NEGATIVE_RANGE UINT32_C(0x01000000)
#define HX711_MINIMUM_RAW UINT32_C(0x00800000)
#define HX711_MAXIMUM_RAW UINT32_C(0x007FFFFF)

static portMUX_TYPE weight_mux = portMUX_INITIALIZER_UNLOCKED;
static bool weight_initialized;

/*
 * Configure the HX711 data input and hold its clock low while idle.
 * Design Document, Chapter 2, Sections 2.1 and 2.4.
 */
weight_result_t weight_init(void)
{
    const gpio_config_t data_config = {
        .pin_bit_mask = (UINT64_C(1) << HX711_DATA_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    const gpio_config_t clock_config = {
        .pin_bit_mask = (UINT64_C(1) << HX711_CLOCK_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (gpio_config(&data_config) != ESP_OK)
    {
        return WEIGHT_RESULT_SENSOR_FAULT;
    }
    if (gpio_config(&clock_config) != ESP_OK)
    {
        return WEIGHT_RESULT_SENSOR_FAULT;
    }
    if (gpio_set_level(HX711_CLOCK_GPIO, 0) != ESP_OK)
    {
        return WEIGHT_RESULT_SENSOR_FAULT;
    }

    weight_initialized = true;
    return WEIGHT_RESULT_OK;
}

/* Wait for the HX711 to signal a finished conversion (data line low). */
static weight_result_t weight_wait_for_conversion(void)
{
    int64_t started_at_us = esp_timer_get_time();

    while (gpio_get_level(HX711_DATA_GPIO) != 0)
    {
        if ((esp_timer_get_time() - started_at_us) >= HX711_TIMEOUT_US)
        {
            return WEIGHT_RESULT_TIMEOUT;
        }
        esp_rom_delay_us(HX711_POLL_DELAY_US);
    }

    return WEIGHT_RESULT_OK;
}

/*
 * Drive one HX711 clock pulse and sample the data line while the clock is
 * high. The clock is always driven low again so the HX711 cannot power down.
 */
static weight_result_t weight_clock_pulse(uint32_t *data_bit)
{
    esp_err_t clock_high = gpio_set_level(HX711_CLOCK_GPIO, 1);
    esp_err_t clock_low;

    esp_rom_delay_us(HX711_CLOCK_DELAY_US);
    *data_bit = (uint32_t)gpio_get_level(HX711_DATA_GPIO);
    clock_low = gpio_set_level(HX711_CLOCK_GPIO, 0);
    esp_rom_delay_us(HX711_CLOCK_DELAY_US);

    return ((clock_high == ESP_OK) && (clock_low == ESP_OK))
               ? WEIGHT_RESULT_OK
               : WEIGHT_RESULT_SENSOR_FAULT;
}

/*
 * Read the signed 24-bit HX711 sample with interrupts masked only while
 * clocking the conversion data.
 * Design Document, Chapter 2, Sections 2.1 and 2.3, and Chapter 6,
 * Section 6.1, Requirements 2.1 and 2.2.
 */
static weight_result_t weight_read_raw(int32_t *raw_counts)
{
    uint32_t shifted_data = 0U;
    uint32_t data_bit = 0U;
    uint32_t pulse;
    weight_result_t result = weight_wait_for_conversion();

    if (result == WEIGHT_RESULT_OK)
    {
        portENTER_CRITICAL(&weight_mux);
        /* 24 data bits, then one more pulse to select channel A, gain 128. */
        for (pulse = 0U; (pulse <= HX711_BITS) && (result == WEIGHT_RESULT_OK);
             ++pulse)
        {
            result = weight_clock_pulse(&data_bit);
            if (pulse < HX711_BITS)
            {
                shifted_data = (shifted_data << 1U) | data_bit;
            }
        }
        portEXIT_CRITICAL(&weight_mux);
    }

    if (result == WEIGHT_RESULT_OK)
    {
        if ((shifted_data == HX711_MINIMUM_RAW) ||
            (shifted_data == HX711_MAXIMUM_RAW))
        {
            result = WEIGHT_RESULT_SENSOR_FAULT;
        }
        else if ((shifted_data & HX711_SIGN_BIT) != 0U)
        {
            *raw_counts = (int32_t)shifted_data - (int32_t)HX711_NEGATIVE_RANGE;
        }
        else
        {
            *raw_counts = (int32_t)shifted_data;
        }
    }

    return result;
}

/*
 * Convert one HX711 sample and reject readings outside the specified limits.
 * Design Document, Chapter 2, Section 2.1, and Chapter 6, Section 6.1,
 * Requirements 2.3 and 2.4.
 */
weight_result_t weight_read(const weight_calibration_t *calibration,
                            bool intake_window_active,
                            bool has_previous_measurement,
                            float previous_weight_grams,
                            weight_measurement_t *measurement)
{
    int32_t raw_counts;
    int64_t adjusted_counts;
    float weight_grams;
    weight_measurement_t completed_measurement;
    weight_result_t result;

    if ((calibration == NULL) || (measurement == NULL) ||
        !isfinite(calibration->counts_per_gram) ||
        (calibration->counts_per_gram == 0.0F) ||
        (has_previous_measurement && !isfinite(previous_weight_grams)))
    {
        return WEIGHT_RESULT_INVALID_ARGUMENT;
    }
    if (!weight_initialized)
    {
        return WEIGHT_RESULT_NOT_INITIALIZED;
    }

    result = weight_read_raw(&raw_counts);
    if (result != WEIGHT_RESULT_OK)
    {
        return result;
    }

    adjusted_counts = (int64_t)raw_counts -
                      (int64_t)calibration->tare_offset_counts;
    weight_grams = (float)adjusted_counts / calibration->counts_per_gram;
    if (!isfinite(weight_grams) ||
        (weight_grams < WEIGHT_MINIMUM_GRAMS) ||
        (weight_grams > WEIGHT_MAXIMUM_GRAMS))
    {
        return WEIGHT_RESULT_IMPLAUSIBLE;
    }
    if (!intake_window_active && has_previous_measurement &&
        (fabsf(weight_grams - previous_weight_grams) >
         WEIGHT_MAXIMUM_OUTSIDE_WINDOW_CHANGE_GRAMS))
    {
        return WEIGHT_RESULT_IMPLAUSIBLE;
    }

    completed_measurement.raw_counts = raw_counts;
    completed_measurement.grams = weight_grams;
    *measurement = completed_measurement;
    return WEIGHT_RESULT_OK;
}
