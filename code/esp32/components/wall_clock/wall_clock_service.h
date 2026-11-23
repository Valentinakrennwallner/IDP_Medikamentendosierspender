#ifndef WALL_CLOCK_SERVICE_H
#define WALL_CLOCK_SERVICE_H

/*
 * The single ESP32 clock instance on the hardware: wall_clock logic
 * advanced with esp_timer_get_time() and protected by a spinlock, because
 * the time is written by the BLE thread and read by the weight-measurement
 * and dose-evaluation threads (64-bit values are not read atomically).
 * Design Document, Chapter 2, Sections 2.2 and 2.3, and Chapter 6,
 * Section 6.1, Requirements 1.4, 2.11, 3.2 and 3.3.
 */

#include <stdbool.h>
#include <stdint.h>

#include "dosing.h"
#include "wall_clock.h"

/*
 * Set the clock to the Unix time received from the Raspberry Pi. To be
 * called by the STARTUP handling (task 18) on every connection. An invalid
 * time puts the clock into the state "no time" and returns false. Only
 * "time set" or "time rejected" is logged, never the time itself.
 * Design Document, Chapter 6, Section 6.1, Requirements 1.4, 3.2 and 3.3.
 */
bool wall_clock_service_set_unix_time(int64_t unix_seconds);

/*
 * Read the current time. Returns false in the state "no time".
 * Design Document, Chapter 6, Section 6.1, Requirement 3.3.
 */
bool wall_clock_service_now(wall_clock_time_t *current_time);

/*
 * Register the table of expected weights for the current local day (to be
 * called by the dose evaluation after the filling check, task 21), or
 * remove it with NULL. The table must stay unchanged while registered.
 * Returns false and registers nothing in the state "no time" or for an
 * invalid table. Design Document, Chapter 6, Section 6.1, Requirement 2.11.
 */
bool wall_clock_service_set_table(const dosing_table_t *table);

/*
 * Copy the row of the registered table for the current time to *row and
 * return WALL_CLOCK_ROW_FOUND, or return the reason why there is no row
 * (no time, no table, day ended). Design Document, Chapter 3 (Table 6),
 * and Chapter 6, Section 6.1, Requirements 1.6, 2.11 and 3.3.
 */
wall_clock_row_status_t wall_clock_service_current_row(dosing_row_t *row);

/*
 * True only if a valid time lies in an intake window of the registered
 * table; false without time, without table or after the day end (fail
 * closed). Design Document, Chapter 6, Section 6.1, Requirements 2.4,
 * 2.11 and 3.3.
 */
bool wall_clock_service_intake_window_active(void);

#endif
