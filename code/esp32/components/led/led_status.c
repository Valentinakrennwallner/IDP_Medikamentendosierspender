#include "led_status.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>

#define LED_RED_GPIO GPIO_NUM_25
#define LED_GREEN_GPIO GPIO_NUM_26
#define LED_BLUE_GPIO GPIO_NUM_27
#define LED_FAULT_FLASH_PERIOD_US INT64_C(500000)
#define LED_SELF_TEST_DURATION_MS 300U

static const char *TAG = "led_status";
static esp_timer_handle_t fault_flash_timer;
static volatile led_status_t current_status = LED_STATUS_OFF;
static bool fault_flash_on;
static bool initialized;

/*
 * Toggle only the red channel while the fault status is active.
 * Design Document, Chapter 1, Section 1.2, LED state table.
 */
static void led_fault_flash_callback(void *argument)
{
    esp_err_t result;
    (void)argument;

    if (current_status != LED_STATUS_FAULT_FLASH)
    {
        return;
    }

    fault_flash_on = !fault_flash_on;
    result = gpio_set_level(LED_RED_GPIO, fault_flash_on ? 1 : 0);
    if (result != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to update fault LED");
    }
}

/*
 * Stop the periodic fault indication before applying a non-flashing state.
 * Design Document, Chapter 1, Section 1.2, LED state table.
 */
static esp_err_t led_stop_fault_flash(void)
{
    if ((fault_flash_timer != NULL) && esp_timer_is_active(fault_flash_timer))
    {
        return esp_timer_stop(fault_flash_timer);
    }

    return ESP_OK;
}

/*
 * Apply the requested RGB channel levels and report the first GPIO failure.
 * Design Document, Chapter 2, Section 2.1, LED status display.
 */
static esp_err_t led_write_rgb(int red, int green, int blue)
{
    esp_err_t first_error = ESP_OK;
    esp_err_t result;

    result = gpio_set_level(LED_RED_GPIO, red);
    if ((result != ESP_OK) && (first_error == ESP_OK))
    {
        first_error = result;
    }

    result = gpio_set_level(LED_GREEN_GPIO, green);
    if ((result != ESP_OK) && (first_error == ESP_OK))
    {
        first_error = result;
    }

    result = gpio_set_level(LED_BLUE_GPIO, blue);
    if ((result != ESP_OK) && (first_error == ESP_OK))
    {
        first_error = result;
    }

    return first_error;
}

/*
 * Initialize the common-cathode RGB LED on GPIOs 25, 26, and 27.
 * Design Document, Chapter 2, Sections 2.1 and 2.4, LED pinout.
 */
esp_err_t led_status_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << LED_RED_GPIO) |
                        (1ULL << LED_GREEN_GPIO) |
                        (1ULL << LED_BLUE_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    esp_timer_create_args_t timer_config = {
        .callback = led_fault_flash_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "led_fault_flash",
        .skip_unhandled_events = true};
    esp_err_t result;

    if (initialized)
    {
        return ESP_OK;
    }

    result = gpio_config(&config);
    if (result != ESP_OK)
    {
        return result;
    }

    result = led_write_rgb(0, 0, 0);
    if (result != ESP_OK)
    {
        return result;
    }

    result = esp_timer_create(&timer_config, &fault_flash_timer);
    if (result != ESP_OK)
    {
        return result;
    }

    current_status = LED_STATUS_OFF;
    fault_flash_on = false;
    initialized = true;
    return ESP_OK;
}

/*
 * Select the LED state corresponding to dose evaluation or a system fault.
 * Design Document, Chapter 1, Section 1.2, and Chapter 6, Section 6.1,
 * Requirements 1.8, 2.6, 2.10, 2.12, and 2.13.
 */
esp_err_t led_status_set(led_status_t status)
{
    esp_err_t result;

    if (!initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if ((status < LED_STATUS_OFF) || (status > LED_STATUS_FAULT_FLASH))
    {
        return ESP_ERR_INVALID_ARG;
    }

    current_status = status;
    result = led_stop_fault_flash();
    if (result != ESP_OK)
    {
        return result;
    }

    fault_flash_on = false;
    switch (status)
    {
        case LED_STATUS_OFF:
            return led_write_rgb(0, 0, 0);
        case LED_STATUS_GREEN:
            return led_write_rgb(0, 1, 0);
        case LED_STATUS_BLUE:
            return led_write_rgb(0, 0, 1);
        case LED_STATUS_RED:
            return led_write_rgb(1, 0, 0);
        case LED_STATUS_FAULT_FLASH:
            result = led_write_rgb(1, 0, 0);
            if (result != ESP_OK)
            {
                return result;
            }

            fault_flash_on = true;
            return esp_timer_start_periodic(fault_flash_timer,
                                            LED_FAULT_FLASH_PERIOD_US);
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

/*
 * Visually test red, green, and blue channels in sequence, then turn the LED
 * off. Design Document, Chapter 1, Section 1.2, LED state table.
 */
esp_err_t led_status_self_test(void)
{
    static const led_status_t test_states[] = {
        LED_STATUS_RED,
        LED_STATUS_GREEN,
        LED_STATUS_BLUE};
    size_t index;
    esp_err_t result;

    if (!initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    for (index = 0U; index < (sizeof(test_states) / sizeof(test_states[0])); index++)
    {
        result = led_status_set(test_states[index]);
        if (result != ESP_OK)
        {
            return result;
        }

        vTaskDelay(pdMS_TO_TICKS(LED_SELF_TEST_DURATION_MS));
    }

    return led_status_set(LED_STATUS_OFF);
}

/*
 * Preserve the red missed-dose indicator during deep sleep, otherwise leave
 * the LED off. Design Document, Chapter 1, Section 1.2, and Chapter 6,
 * Section 6.1, Requirements 1.6 and 1.7.
 */
esp_err_t led_status_prepare_for_deep_sleep(bool missed_dose)
{
    esp_err_t result;

    result = led_status_set(missed_dose ? LED_STATUS_RED : LED_STATUS_OFF);
    if (result != ESP_OK)
    {
        return result;
    }

    if (missed_dose)
    {
        result = gpio_hold_en(LED_RED_GPIO);
        if (result != ESP_OK)
        {
            return result;
        }

        gpio_deep_sleep_hold_en();
        return ESP_OK;
    }

    gpio_deep_sleep_hold_dis();
    result = gpio_hold_dis(LED_RED_GPIO);
    if (result != ESP_OK)
    {
        return result;
    }

    return ESP_OK;
}
