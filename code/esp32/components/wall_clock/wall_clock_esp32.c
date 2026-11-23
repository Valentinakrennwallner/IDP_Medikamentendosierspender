/*
 * ESP32 binding of the clock: one wall_clock instance advanced with
 * esp_timer_get_time() (monotonic microseconds since boot) and protected
 * by a spinlock. The state is copied inside the critical section and
 * evaluated outside it; nothing is logged inside the critical section.
 * The time is health-related context (intake times), so it is never
 * logged (Design Document, Chapter 6, Section 6.2, Requirement 4.4).
 */
#include "wall_clock_service.h"

#include <stddef.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *WALL_CLOCK_LOG_TAG = "wall_clock";

static portMUX_TYPE wall_clock_lock = portMUX_INITIALIZER_UNLOCKED;
/* Zero-initialised: valid == false, i.e. the state "no time". */
static wall_clock_t service_clock;
static const dosing_table_t *service_table = NULL;
static uint32_t service_table_day = 0U;

/*
 * Monotonic time in milliseconds since boot from esp_timer.
 * Design Document, Chapter 2, Section 2.2 (esp_timer).
 */
static uint64_t wall_clock_monotonic_ms(void)
{
    int64_t now_us = esp_timer_get_time();

    return (uint64_t)now_us / UINT64_C(1000);
}

/*
 * Copy the clock, the registered table and its day under the spinlock.
 * Design Document, Chapter 2, Section 2.3.
 */
static void wall_clock_snapshot(wall_clock_t *state, const dosing_table_t **table,
                                uint32_t *table_day)
{
    portENTER_CRITICAL(&wall_clock_lock);
    *state = service_clock;
    *table = service_table;
    *table_day = service_table_day;
    portEXIT_CRITICAL(&wall_clock_lock);
}

/*
 * Set the clock to the Unix time received from the Raspberry Pi; an
 * invalid time puts the clock into the state "no time".
 * Design Document, Chapter 6, Section 6.1, Requirements 1.4, 3.2 and 3.3.
 */
bool wall_clock_service_set_unix_time(int64_t unix_seconds)
{
    uint64_t now_ms = wall_clock_monotonic_ms();
    bool accepted;

    portENTER_CRITICAL(&wall_clock_lock);
    accepted = wall_clock_set_unix_time(&service_clock, unix_seconds, now_ms);
    portEXIT_CRITICAL(&wall_clock_lock);

    if (accepted)
    {
        ESP_LOGI(WALL_CLOCK_LOG_TAG, "Time set");
    }
    else
    {
        ESP_LOGW(WALL_CLOCK_LOG_TAG, "Received time rejected: no valid time");
    }
    return accepted;
}

/*
 * Read the current time; false in the state "no time".
 * Design Document, Chapter 6, Section 6.1, Requirement 3.3.
 */
bool wall_clock_service_now(wall_clock_time_t *current_time)
{
    wall_clock_t state;
    const dosing_table_t *table;
    uint32_t table_day;

    wall_clock_snapshot(&state, &table, &table_day);
    return wall_clock_now(&state, wall_clock_monotonic_ms(), current_time);
}

/*
 * Register the table of expected weights for the current local day, or
 * remove it with NULL. Design Document, Chapter 6, Section 6.1,
 * Requirement 2.11.
 */
bool wall_clock_service_set_table(const dosing_table_t *table)
{
    wall_clock_time_t now;
    bool registered = false;

    if (table == NULL)
    {
        portENTER_CRITICAL(&wall_clock_lock);
        service_table = NULL;
        service_table_day = 0U;
        portEXIT_CRITICAL(&wall_clock_lock);
        registered = true;
    }
    else if (table->valid && wall_clock_service_now(&now))
    {
        portENTER_CRITICAL(&wall_clock_lock);
        service_table = table;
        service_table_day = now.local_day;
        portEXIT_CRITICAL(&wall_clock_lock);
        registered = true;
    }
    else
    {
        /* No valid time or invalid table: nothing is registered. */
    }
    return registered;
}

/*
 * Copy the row of the registered table for the current time, or return
 * why there is none. Design Document, Chapter 3 (Table 6), and Chapter 6,
 * Section 6.1, Requirements 1.6, 2.11 and 3.3.
 */
wall_clock_row_status_t wall_clock_service_current_row(dosing_row_t *row)
{
    wall_clock_t state;
    const dosing_table_t *table;
    const dosing_row_t *found = NULL;
    uint32_t table_day;
    wall_clock_row_status_t status = WALL_CLOCK_ROW_NO_TIME;

    if (row != NULL)
    {
        wall_clock_snapshot(&state, &table, &table_day);
        status = wall_clock_current_row(&state, table, table_day, wall_clock_monotonic_ms(),
                                        &found);
        if ((status == WALL_CLOCK_ROW_FOUND) && (found != NULL))
        {
            *row = *found;
        }
    }
    return status;
}

/*
 * True only if a valid time lies in an intake window of the registered
 * table (fail closed). Design Document, Chapter 6, Section 6.1,
 * Requirements 2.4, 2.11 and 3.3.
 */
bool wall_clock_service_intake_window_active(void)
{
    wall_clock_t state;
    const dosing_table_t *table;
    uint32_t table_day;

    wall_clock_snapshot(&state, &table, &table_day);
    return wall_clock_intake_window_active(&state, table, table_day, wall_clock_monotonic_ms());
}
