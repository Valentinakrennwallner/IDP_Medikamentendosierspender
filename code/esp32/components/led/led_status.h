#ifndef LED_STATUS_H
#define LED_STATUS_H

#include <stdbool.h>

#include "esp_err.h"

typedef enum
{
    LED_STATUS_OFF = 0,
    LED_STATUS_GREEN,
    LED_STATUS_BLUE,
    LED_STATUS_RED,
    LED_STATUS_FAULT_FLASH
} led_status_t;

/* Configure RGB LED GPIO outputs and the fault-flash timer. */
esp_err_t led_status_init(void);

/* Display one of the states defined by the Design Document LED table. */
esp_err_t led_status_set(led_status_t status);

/* Run the required red, green, blue startup self-test, then switch off. */
esp_err_t led_status_self_test(void);

/* Set and hold red for a missed dose, or switch off, before deep sleep. */
esp_err_t led_status_prepare_for_deep_sleep(bool missed_dose);

#endif
