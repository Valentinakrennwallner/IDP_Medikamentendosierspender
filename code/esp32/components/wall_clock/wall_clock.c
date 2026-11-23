#include "wall_clock.h"

#include <stddef.h>

#define WALL_CLOCK_SECONDS_PER_DAY INT64_C(86400)
#define WALL_CLOCK_SECONDS_PER_HOUR INT64_C(3600)
#define WALL_CLOCK_EPOCH_YEAR INT64_C(1970)
/* 1970-01-01 was a Thursday; weekday numbering 0 = Sunday. */
#define WALL_CLOCK_EPOCH_WEEKDAY INT64_C(4)
#define WALL_CLOCK_MARCH INT64_C(3)
#define WALL_CLOCK_OCTOBER INT64_C(10)

/* Days before the first day of each month in a non-leap year. */
static const int64_t wall_clock_days_before_month[12] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334,
};

/*
 * Number of leap years from year 1 up to and including the given year
 * (Gregorian rule). Helper for the calendar calculation of the local time.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.11.
 */
static int64_t wall_clock_leap_years_through(int64_t year)
{
    return (year / INT64_C(4)) - (year / INT64_C(100)) + (year / INT64_C(400));
}

/*
 * True if the year is a Gregorian leap year. Helper for the calendar
 * calculation of the local time. Design Document, Chapter 6, Section 6.1,
 * Requirement 2.11.
 */
static bool wall_clock_is_leap_year(int64_t year)
{
    return (((year % INT64_C(4)) == 0) && ((year % INT64_C(100)) != 0)) ||
           ((year % INT64_C(400)) == 0);
}

/*
 * Days from 1970-01-01 to the first day of the given year (year >= 1970).
 * Design Document, Chapter 6, Section 6.1, Requirement 2.11.
 */
static int64_t wall_clock_days_before_year(int64_t year)
{
    return (INT64_C(365) * (year - WALL_CLOCK_EPOCH_YEAR)) +
           (wall_clock_leap_years_through(year - INT64_C(1)) -
            wall_clock_leap_years_through(WALL_CLOCK_EPOCH_YEAR - INT64_C(1)));
}

/*
 * Days from 1970-01-01 to the given date (month 1 ... 12, day 1 ... 31,
 * only called with valid dates). Design Document, Chapter 6, Section 6.1,
 * Requirement 2.11.
 */
static int64_t wall_clock_days_from_date(int64_t year, int64_t month, int64_t day)
{
    int64_t days = wall_clock_days_before_year(year) +
                   wall_clock_days_before_month[month - INT64_C(1)] + (day - INT64_C(1));

    if ((month > INT64_C(2)) && wall_clock_is_leap_year(year))
    {
        days += INT64_C(1);
    }
    return days;
}

/*
 * Calendar year of a day number (days since 1970-01-01, not negative).
 * Starts below the result and counts up a few years at most.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.11.
 */
static int64_t wall_clock_year_of_day(int64_t days)
{
    int64_t year = WALL_CLOCK_EPOCH_YEAR + (days / INT64_C(366));

    while (wall_clock_days_before_year(year + INT64_C(1)) <= days)
    {
        year += INT64_C(1);
    }
    return year;
}

/*
 * Day number of the last Sunday of a month that has 31 days (March or
 * October). Design Document, Chapter 6, Section 6.1, Requirement 2.11.
 */
static int64_t wall_clock_last_sunday(int64_t year, int64_t month)
{
    int64_t last_day = wall_clock_days_from_date(year, month, INT64_C(31));
    int64_t weekday = (last_day + WALL_CLOCK_EPOCH_WEEKDAY) % INT64_C(7);

    return last_day - weekday;
}

/*
 * True if Austrian summer time (CEST) applies at the Unix time: from the
 * last Sunday in March, 01:00 UTC, to the last Sunday in October,
 * 01:00 UTC (EU rule). Design Document, Chapter 6, Section 6.1,
 * Requirement 2.11.
 */
static bool wall_clock_is_summer_time(int64_t unix_seconds)
{
    int64_t year = wall_clock_year_of_day(unix_seconds / WALL_CLOCK_SECONDS_PER_DAY);
    int64_t start = (wall_clock_last_sunday(year, WALL_CLOCK_MARCH) * WALL_CLOCK_SECONDS_PER_DAY) +
                    WALL_CLOCK_SECONDS_PER_HOUR;
    int64_t end = (wall_clock_last_sunday(year, WALL_CLOCK_OCTOBER) * WALL_CLOCK_SECONDS_PER_DAY) +
                  WALL_CLOCK_SECONDS_PER_HOUR;

    return (unix_seconds >= start) && (unix_seconds < end);
}

/*
 * Start in the state "no time". Design Document, Chapter 6, Section 6.1,
 * Requirement 3.3.
 */
void wall_clock_init(wall_clock_t *state)
{
    if (state != NULL)
    {
        state->valid = false;
        state->unix_seconds_at_set = 0;
        state->monotonic_ms_at_set = 0U;
    }
}

/*
 * Set the clock to the received Unix time at the given monotonic time.
 * A later call (reconnect, Requirement 3.2) replaces the time. A value
 * outside WALL_CLOCK_MINIMUM/MAXIMUM_UNIX_SECONDS is rejected and puts the
 * clock into the state "no time" (fail closed); returns false then.
 * Design Document, Chapter 6, Section 6.1, Requirements 1.4, 3.2 and 3.3.
 */
bool wall_clock_set_unix_time(wall_clock_t *state, int64_t unix_seconds, uint64_t now_ms)
{
    bool accepted = false;

    if (state != NULL)
    {
        accepted = (unix_seconds >= WALL_CLOCK_MINIMUM_UNIX_SECONDS) &&
                   (unix_seconds <= WALL_CLOCK_MAXIMUM_UNIX_SECONDS);
        if (accepted)
        {
            state->valid = true;
            state->unix_seconds_at_set = unix_seconds;
            state->monotonic_ms_at_set = now_ms;
        }
        else
        {
            wall_clock_init(state);
        }
    }
    return accepted;
}

/*
 * Convert a Unix time into Austrian local day and time of day (CET/CEST).
 * Returns false for a time outside the accepted range or a missing
 * argument. Design Document, Chapter 3 (Table 6), and Chapter 6,
 * Section 6.1, Requirement 2.11.
 */
bool wall_clock_local_time(int64_t unix_seconds, wall_clock_time_t *current_time)
{
    int64_t local_seconds;
    int64_t local_day;
    int64_t seconds_since_midnight;

    if ((current_time == NULL) || (unix_seconds < WALL_CLOCK_MINIMUM_UNIX_SECONDS) ||
        (unix_seconds > WALL_CLOCK_MAXIMUM_UNIX_SECONDS))
    {
        return false;
    }

    local_seconds = unix_seconds + (wall_clock_is_summer_time(unix_seconds)
                                        ? WALL_CLOCK_SUMMER_OFFSET_SECONDS
                                        : WALL_CLOCK_STANDARD_OFFSET_SECONDS);
    /* local_seconds is positive in the accepted range, so both results are too. */
    local_day = local_seconds / WALL_CLOCK_SECONDS_PER_DAY;
    seconds_since_midnight = local_seconds % WALL_CLOCK_SECONDS_PER_DAY;
    current_time->unix_seconds = unix_seconds;
    current_time->local_day = (uint32_t)local_day;
    current_time->seconds_since_local_midnight = (uint32_t)seconds_since_midnight;
    return true;
}

/*
 * Return the current time advanced from the set time with the monotonic
 * counter. Returns false in the state "no time", if the monotonic counter
 * is behind the set point, or if the time leaves the accepted range.
 * Design Document, Chapter 2, Section 2.2 (esp_timer), and Chapter 6,
 * Section 6.1, Requirement 3.3.
 */
bool wall_clock_now(const wall_clock_t *state, uint64_t now_ms, wall_clock_time_t *current_time)
{
    uint64_t elapsed_seconds;
    int64_t remaining_seconds;

    if ((state == NULL) || (current_time == NULL) || (!state->valid) ||
        (now_ms < state->monotonic_ms_at_set))
    {
        return false;
    }

    elapsed_seconds = (now_ms - state->monotonic_ms_at_set) / UINT64_C(1000);
    /* Not negative: a set time is never above the maximum. */
    remaining_seconds = WALL_CLOCK_MAXIMUM_UNIX_SECONDS - state->unix_seconds_at_set;
    if (elapsed_seconds > (uint64_t)remaining_seconds)
    {
        return false;
    }
    return wall_clock_local_time(state->unix_seconds_at_set + (int64_t)elapsed_seconds,
                                 current_time);
}

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
                                               const dosing_row_t **row)
{
    wall_clock_row_status_t status;
    wall_clock_time_t now;

    if (row == NULL)
    {
        return WALL_CLOCK_ROW_NO_TIME;
    }
    *row = NULL;

    if (!wall_clock_now(state, now_ms, &now))
    {
        status = WALL_CLOCK_ROW_NO_TIME;
    }
    else if ((table == NULL) || (!table->valid))
    {
        status = WALL_CLOCK_ROW_NO_TABLE;
    }
    else if (now.local_day < table_local_day)
    {
        /* Time set back across midnight: the table is not valid yet (fail closed). */
        status = WALL_CLOCK_ROW_NO_TIME;
    }
    else if ((now.local_day > table_local_day) ||
             (now.seconds_since_local_midnight >= table->day_end_seconds))
    {
        status = WALL_CLOCK_ROW_DAY_ENDED;
    }
    else
    {
        *row = dosing_table_find_row(table, now.seconds_since_local_midnight);
        /* A valid table covers the whole day up to day_end_seconds. */
        status = (*row != NULL) ? WALL_CLOCK_ROW_FOUND : WALL_CLOCK_ROW_NO_TABLE;
    }
    return status;
}

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
                                     uint64_t now_ms)
{
    const dosing_row_t *row = NULL;

    return (wall_clock_current_row(state, table, table_local_day, now_ms, &row) ==
            WALL_CLOCK_ROW_FOUND) &&
           (row != NULL) && (row->phase == DOSING_PHASE_INTAKE_WINDOW);
}
