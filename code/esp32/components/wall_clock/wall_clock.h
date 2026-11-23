#ifndef WALL_CLOCK_H
#define WALL_CLOCK_H

/*
 * ESP32 clock (wall time) as pure logic without hardware dependencies.
 * The time is set once as Unix time (received from the Raspberry Pi with
 * the STARTUP message, task 18) and then advanced with a monotonic
 * millisecond counter that the caller passes in (esp_timer on the ESP32,
 * a plain value in the host tests). Until a valid time is set the clock is
 * in the state "no time" (Requirement 3.3).
 *
 * Local time is Austrian time (CET, UTC+1; CEST, UTC+2) with the EU summer
 * time rule: summer time from the last Sunday in March, 01:00 UTC, to the
 * last Sunday in October, 01:00 UTC. The intake times in the Raspberry Pi
 * schedule must be given in the same local time.
 *
 * Design Document, Chapter 2, Section 2.1 (dose evaluation and intake
 * monitoring), Chapter 3 (Table 6), and Chapter 6, Section 6.1,
 * Requirements 1.4, 1.6, 2.11, 3.2 and 3.3.
 */

#include <stdbool.h>
#include <stdint.h>

#include "dosing.h"

/* Accepted range of a received time: 2026-01-01T00:00:00Z ... 2099-12-31T23:59:59Z. */
#define WALL_CLOCK_MINIMUM_UNIX_SECONDS INT64_C(1767225600)
#define WALL_CLOCK_MAXIMUM_UNIX_SECONDS INT64_C(4102444799)

/* UTC offsets of Austrian standard time (CET) and summer time (CEST). */
#define WALL_CLOCK_STANDARD_OFFSET_SECONDS INT64_C(3600)
#define WALL_CLOCK_SUMMER_OFFSET_SECONDS INT64_C(7200)

typedef struct
{
    /* False in the state "no time" (Requirement 3.3). */
    bool valid;
    /* Unix time received from the Raspberry Pi. */
    int64_t unix_seconds_at_set;
    /* Monotonic time at which unix_seconds_at_set was received. */
    uint64_t monotonic_ms_at_set;
} wall_clock_t;

/* Current time, as Unix time and as local day and time of day. */
typedef struct
{
    int64_t unix_seconds;
    /* Local calendar day as days since 1970-01-01 (local time). */
    uint32_t local_day;
    /* Seconds since local midnight, 0 ... 86399 (unit of the dosing table). */
    uint32_t seconds_since_local_midnight;
} wall_clock_time_t;

/* Result of wall_clock_current_row(). */
typedef enum
{
    /* The time lies in a row of the table; *row points to it. */
    WALL_CLOCK_ROW_FOUND = 0,
    /*
     * No valid time (Requirement 3.3), or the time lies on a day before the
     * day of the table (time set back across midnight): no evaluation.
     */
    WALL_CLOCK_ROW_NO_TIME = 1,
    /* No valid table of expected weights registered yet. */
    WALL_CLOCK_ROW_NO_TABLE = 2,
    /*
     * The last intake window of the table's day has ended, or a later
     * local day has begun: day-end check (Requirements 1.6 and 1.7).
     */
    WALL_CLOCK_ROW_DAY_ENDED = 3
} wall_clock_row_status_t;

/*
 * Start in the state "no time". Design Document, Chapter 6, Section 6.1,
 * Requirement 3.3.
 */
void wall_clock_init(wall_clock_t *state);

/*
 * Set the clock to the received Unix time at the given monotonic time.
 * A later call (reconnect, Requirement 3.2) replaces the time. A value
 * outside WALL_CLOCK_MINIMUM/MAXIMUM_UNIX_SECONDS is rejected and puts the
 * clock into the state "no time" (fail closed); returns false then.
 * Design Document, Chapter 6, Section 6.1, Requirements 1.4, 3.2 and 3.3.
 */
bool wall_clock_set_unix_time(wall_clock_t *state, int64_t unix_seconds, uint64_t now_ms);

/*
 * Convert a Unix time into Austrian local day and time of day (CET/CEST).
 * Returns false for a time outside the accepted range or a missing
 * argument. Design Document, Chapter 3 (Table 6), and Chapter 6,
 * Section 6.1, Requirement 2.11.
 */
bool wall_clock_local_time(int64_t unix_seconds, wall_clock_time_t *current_time);

/*
 * Return the current time advanced from the set time with the monotonic
 * counter. Returns false in the state "no time", if the monotonic counter
 * is behind the set point, or if the time leaves the accepted range.
 * Design Document, Chapter 2, Section 2.2 (esp_timer), and Chapter 6,
 * Section 6.1, Requirement 3.3.
 */
bool wall_clock_now(const wall_clock_t *state, uint64_t now_ms, wall_clock_time_t *current_time);

/*
 * Find the row of the table of expected weights for the current time. The
 * table is only valid for table_local_day, the local day on which it was
 * created (see wall_clock_row_status_t for the other results). *row is set
 * to NULL unless WALL_CLOCK_ROW_FOUND is returned. Design Document,
 * Chapter 3 (ESP32 flowchart, Table 6), and Chapter 6, Section 6.1,
 * Requirements 1.6, 2.11 and 3.3.
 */
wall_clock_row_status_t wall_clock_current_row(const wall_clock_t *state,
                                               const dosing_table_t *table,
                                               uint32_t table_local_day,
                                               uint64_t now_ms,
                                               const dosing_row_t **row);

/*
 * True only if a valid time lies in an intake-window row of the table.
 * Without time, without table, after the day end or on a missing argument
 * the result is false ("outside an intake window", fail closed), so the
 * 5 g change check of Requirement 2.4 stays active.
 * Design Document, Chapter 6, Section 6.1, Requirements 2.4, 2.11 and 3.3.
 */
bool wall_clock_intake_window_active(const wall_clock_t *state,
                                     const dosing_table_t *table,
                                     uint32_t table_local_day,
                                     uint64_t now_ms);

#endif
