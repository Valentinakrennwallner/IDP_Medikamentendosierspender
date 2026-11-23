#include <stdio.h>
#include <stdlib.h>

#include "wall_clock.h"

/*
 * Host tests for the ESP32 clock (components/wall_clock). The monotonic
 * counter is injected in milliseconds. Expected local times were computed
 * independently with the IANA zone Europe/Vienna (Python zoneinfo).
 * Design Document, Chapter 3 (Table 6), and Chapter 6, Section 6.1,
 * Requirements 1.4, 1.6, 2.4, 2.11, 3.2 and 3.3.
 */

#define HOURS(h) ((uint32_t)(h) * 3600U)
#define SET_MS UINT64_C(5000)
#define SECOND_MS UINT64_C(1000)
#define HOUR_MS (UINT64_C(3600) * SECOND_MS)

/* 2026-11-23 (CET): local day 20780; 06:00 local = 05:00 UTC. */
#define TEST_DAY 20780U
#define TEST_DAY_0600_UNIX INT64_C(1795410000)

/* Report a failed check with its line number and return 1. */
static int fail(int line)
{
    (void)fprintf(stderr, "wall_clock_test: check failed at line %d\n", line);
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

/* Schedule of Design Document, Chapter 3, Table 6: 07:00, 12:00, 18:00. */
static const startup_schedule_entry_t table6_entries[] = {
    {HOURS(7), 100U},
    {HOURS(12), 200U},
    {HOURS(18), 100U},
};
static const startup_daily_schedule_t table6_schedule = {table6_entries, 3U};

/* Return true if the conversion gives the expected local day and time. */
static bool local_time_is(int64_t unix_seconds, uint32_t day, uint32_t seconds)
{
    wall_clock_time_t time;

    return wall_clock_local_time(unix_seconds, &time) && (time.unix_seconds == unix_seconds) &&
           (time.local_day == day) && (time.seconds_since_local_midnight == seconds);
}

/* Monotonic time at which the clock shows the given local time on TEST_DAY. */
static uint64_t at_local(uint32_t seconds_since_midnight)
{
    return SET_MS + (((uint64_t)seconds_since_midnight - (uint64_t)HOURS(6)) * SECOND_MS);
}

/* Requirement 3.3: before a time is set the clock is in the state "no time". */
static int test_no_time_initially(void)
{
    wall_clock_t state;
    wall_clock_time_t time;
    dosing_table_t table;
    const dosing_row_t *row = &table.rows[0];

    CHECK(dosing_table_create(&table6_schedule, &table) == 0);
    wall_clock_init(&state);
    CHECK(!wall_clock_now(&state, SET_MS, &time));
    CHECK(wall_clock_current_row(&state, &table, TEST_DAY, SET_MS, &row) ==
          WALL_CLOCK_ROW_NO_TIME);
    CHECK(row == NULL);
    CHECK(!wall_clock_intake_window_active(&state, &table, TEST_DAY, SET_MS));
    return 0;
}

/* The set time is advanced with the monotonic counter (whole seconds). */
static int test_time_advances(void)
{
    wall_clock_t state;
    wall_clock_time_t time;

    wall_clock_init(&state);
    CHECK(wall_clock_set_unix_time(&state, TEST_DAY_0600_UNIX, SET_MS));
    CHECK(wall_clock_now(&state, SET_MS, &time));
    CHECK(time.unix_seconds == TEST_DAY_0600_UNIX);
    CHECK(time.local_day == TEST_DAY);
    CHECK(time.seconds_since_local_midnight == HOURS(6));
    CHECK(wall_clock_now(&state, SET_MS + 999U, &time));
    CHECK(time.unix_seconds == TEST_DAY_0600_UNIX);
    CHECK(wall_clock_now(&state, SET_MS + HOUR_MS + 1500U, &time));
    CHECK(time.unix_seconds == (TEST_DAY_0600_UNIX + 3601));
    CHECK(time.seconds_since_local_midnight == (HOURS(7) + 1U));
    /* A monotonic value before the set point is rejected (fail closed). */
    CHECK(!wall_clock_now(&state, SET_MS - 1U, &time));
    return 0;
}

/* Requirement 3.2: a new time after a reconnect replaces the old one. */
static int test_reconnect_replaces_time(void)
{
    wall_clock_t state;
    wall_clock_time_t time;

    wall_clock_init(&state);
    CHECK(wall_clock_set_unix_time(&state, TEST_DAY_0600_UNIX, SET_MS));
    CHECK(wall_clock_set_unix_time(&state, TEST_DAY_0600_UNIX + 120, SET_MS + 60U * SECOND_MS));
    CHECK(wall_clock_now(&state, SET_MS + 60U * SECOND_MS, &time));
    CHECK(time.unix_seconds == (TEST_DAY_0600_UNIX + 120));
    return 0;
}

/* Accepted range 2026-01-01T00:00:00Z ... 2099-12-31T23:59:59Z; outside: "no time". */
static int test_range_check(void)
{
    wall_clock_t state;
    wall_clock_time_t time;

    wall_clock_init(&state);
    CHECK(wall_clock_set_unix_time(&state, WALL_CLOCK_MINIMUM_UNIX_SECONDS, SET_MS));
    CHECK(wall_clock_set_unix_time(&state, WALL_CLOCK_MAXIMUM_UNIX_SECONDS, SET_MS));
    CHECK(!wall_clock_set_unix_time(&state, WALL_CLOCK_MINIMUM_UNIX_SECONDS - 1, SET_MS));
    CHECK(!state.valid);
    CHECK(!wall_clock_now(&state, SET_MS, &time));

    /* An invalid time after a valid one returns to "no time" (fail closed). */
    CHECK(wall_clock_set_unix_time(&state, TEST_DAY_0600_UNIX, SET_MS));
    CHECK(!wall_clock_set_unix_time(&state, WALL_CLOCK_MAXIMUM_UNIX_SECONDS + 1, SET_MS));
    CHECK(!wall_clock_now(&state, SET_MS, &time));
    CHECK(!wall_clock_set_unix_time(&state, 0, SET_MS));
    CHECK(!wall_clock_set_unix_time(&state, -1, SET_MS));

    /* Advancing beyond the accepted range ends the valid time. */
    CHECK(wall_clock_set_unix_time(&state, WALL_CLOCK_MAXIMUM_UNIX_SECONDS, SET_MS));
    CHECK(wall_clock_now(&state, SET_MS + 999U, &time));
    CHECK(!wall_clock_now(&state, SET_MS + SECOND_MS, &time));
    return 0;
}

/* CET/CEST conversion, including both switches and the range limits. */
static int test_local_time_conversion(void)
{
    /* Range limits: 2026-01-01 01:00 CET and 2100-01-01 00:59:59 CET. */
    CHECK(local_time_is(INT64_C(1767225600), 20454U, 3600U));
    CHECK(local_time_is(INT64_C(4102444799), 47482U, 3599U));
    /* 2026-03-29: 00:59:59 UTC = 01:59:59 CET, 01:00:00 UTC = 03:00:00 CEST. */
    CHECK(local_time_is(INT64_C(1774745999), 20541U, 7199U));
    CHECK(local_time_is(INT64_C(1774746000), 20541U, 10800U));
    /* 2026-10-25: 00:59:59 UTC = 02:59:59 CEST, 01:00:00 UTC = 02:00:00 CET. */
    CHECK(local_time_is(INT64_C(1792889999), 20751U, 10799U));
    CHECK(local_time_is(INT64_C(1792890000), 20751U, 7200U));
    /* 2027: other switch dates (28 March, 31 October). */
    CHECK(local_time_is(INT64_C(1806195600), 20905U, 10800U));
    CHECK(local_time_is(INT64_C(1824944399), 21122U, 10799U));
    /* Leap day 2028-02-29 12:00 CET. */
    CHECK(local_time_is(INT64_C(1835434800), 21243U, 43200U));
    /* Summer: 22:30 UTC is already the next local day (00:30 CEST). */
    CHECK(local_time_is(INT64_C(1781562600), 20620U, 1800U));
    /* Local midnight in winter: 23:00 UTC. */
    CHECK(local_time_is(INT64_C(1795474799), TEST_DAY, 86399U));
    CHECK(local_time_is(INT64_C(1795474800), TEST_DAY + 1U, 0U));
    /* Outside the range or missing argument. */
    CHECK(!local_time_is(WALL_CLOCK_MINIMUM_UNIX_SECONDS - 1, 0U, 0U));
    CHECK(!wall_clock_local_time(TEST_DAY_0600_UNIX, NULL));
    return 0;
}

/* Requirement 2.11, Table 6: current row and intake window at the boundaries. */
static int test_current_row_table6(void)
{
    wall_clock_t state;
    dosing_table_t table;
    const dosing_row_t *row = NULL;

    CHECK(dosing_table_create(&table6_schedule, &table) == 0);
    wall_clock_init(&state);
    CHECK(wall_clock_set_unix_time(&state, TEST_DAY_0600_UNIX, SET_MS));

    CHECK(wall_clock_current_row(&state, &table, TEST_DAY, at_local(HOURS(7) - 1U), &row) ==
          WALL_CLOCK_ROW_FOUND);
    CHECK(row == &table.rows[0]);
    CHECK(!wall_clock_intake_window_active(&state, &table, TEST_DAY, at_local(HOURS(7) - 1U)));

    CHECK(wall_clock_current_row(&state, &table, TEST_DAY, at_local(HOURS(7)), &row) ==
          WALL_CLOCK_ROW_FOUND);
    CHECK(row == &table.rows[1]);
    CHECK(wall_clock_intake_window_active(&state, &table, TEST_DAY, at_local(HOURS(7))));
    CHECK(wall_clock_intake_window_active(&state, &table, TEST_DAY, at_local(HOURS(8) - 1U)));

    CHECK(wall_clock_current_row(&state, &table, TEST_DAY, at_local(HOURS(8)), &row) ==
          WALL_CLOCK_ROW_FOUND);
    CHECK(row == &table.rows[2]);
    CHECK(!wall_clock_intake_window_active(&state, &table, TEST_DAY, at_local(HOURS(8))));

    CHECK(wall_clock_intake_window_active(&state, &table, TEST_DAY, at_local(HOURS(12))));
    CHECK(wall_clock_intake_window_active(&state, &table, TEST_DAY, at_local(HOURS(19) - 1U)));
    return 0;
}

/*
 * Requirements 1.6 and 1.7: at the end of the last window and on a later
 * local day the day has ended; no window is active.
 */
static int test_day_end_and_midnight(void)
{
    wall_clock_t state;
    dosing_table_t table;
    const dosing_row_t *row = &table.rows[0];

    CHECK(dosing_table_create(&table6_schedule, &table) == 0);
    wall_clock_init(&state);
    CHECK(wall_clock_set_unix_time(&state, TEST_DAY_0600_UNIX, SET_MS));

    CHECK(wall_clock_current_row(&state, &table, TEST_DAY, at_local(HOURS(19)), &row) ==
          WALL_CLOCK_ROW_DAY_ENDED);
    CHECK(row == NULL);
    CHECK(!wall_clock_intake_window_active(&state, &table, TEST_DAY, at_local(HOURS(19))));
    CHECK(wall_clock_current_row(&state, &table, TEST_DAY, at_local(86399U), &row) ==
          WALL_CLOCK_ROW_DAY_ENDED);

    /* After local midnight the first row of the old table must not apply again. */
    CHECK(wall_clock_current_row(&state, &table, TEST_DAY, at_local(86400U + HOURS(7)), &row) ==
          WALL_CLOCK_ROW_DAY_ENDED);
    CHECK(!wall_clock_intake_window_active(&state, &table, TEST_DAY,
                                           at_local(86400U + HOURS(7))));
    return 0;
}

/* Time set back to a day before the table's day: treated as "no time". */
static int test_time_before_table_day(void)
{
    wall_clock_t state;
    dosing_table_t table;
    const dosing_row_t *row = NULL;

    CHECK(dosing_table_create(&table6_schedule, &table) == 0);
    wall_clock_init(&state);
    CHECK(wall_clock_set_unix_time(&state, TEST_DAY_0600_UNIX - INT64_C(86400), SET_MS));
    CHECK(wall_clock_current_row(&state, &table, TEST_DAY, at_local(HOURS(7)), &row) ==
          WALL_CLOCK_ROW_NO_TIME);
    CHECK(!wall_clock_intake_window_active(&state, &table, TEST_DAY, at_local(HOURS(7))));
    return 0;
}

/* Without a valid table there is no row and no window (fail closed). */
static int test_no_table(void)
{
    wall_clock_t state;
    dosing_table_t table;
    const dosing_row_t *row = NULL;

    CHECK(dosing_table_create(&table6_schedule, &table) == 0);
    wall_clock_init(&state);
    CHECK(wall_clock_set_unix_time(&state, TEST_DAY_0600_UNIX, SET_MS));
    CHECK(wall_clock_current_row(&state, NULL, TEST_DAY, at_local(HOURS(7)), &row) ==
          WALL_CLOCK_ROW_NO_TABLE);
    table.valid = false;
    CHECK(wall_clock_current_row(&state, &table, TEST_DAY, at_local(HOURS(7)), &row) ==
          WALL_CLOCK_ROW_NO_TABLE);
    CHECK(!wall_clock_intake_window_active(&state, &table, TEST_DAY, at_local(HOURS(7))));
    CHECK(!wall_clock_intake_window_active(&state, NULL, TEST_DAY, at_local(HOURS(7))));
    return 0;
}

/* Missing arguments are rejected without side effects. */
static int test_missing_arguments(void)
{
    wall_clock_t state;
    wall_clock_time_t time;
    dosing_table_t table;

    CHECK(dosing_table_create(&table6_schedule, &table) == 0);
    wall_clock_init(NULL);
    CHECK(!wall_clock_set_unix_time(NULL, TEST_DAY_0600_UNIX, SET_MS));
    wall_clock_init(&state);
    CHECK(wall_clock_set_unix_time(&state, TEST_DAY_0600_UNIX, SET_MS));
    CHECK(!wall_clock_now(NULL, SET_MS, &time));
    CHECK(!wall_clock_now(&state, SET_MS, NULL));
    CHECK(wall_clock_current_row(&state, &table, TEST_DAY, SET_MS, NULL) ==
          WALL_CLOCK_ROW_NO_TIME);
    CHECK(!wall_clock_intake_window_active(NULL, &table, TEST_DAY, at_local(HOURS(7))));
    return 0;
}

/* Run all clock tests; exit status 0 only if all pass. */
int main(void)
{
    int failures = 0;

    failures += test_no_time_initially();
    failures += test_time_advances();
    failures += test_reconnect_replaces_time();
    failures += test_range_check();
    failures += test_local_time_conversion();
    failures += test_current_row_table6();
    failures += test_day_end_and_midnight();
    failures += test_time_before_table_day();
    failures += test_no_table();
    failures += test_missing_arguments();

    if (failures != 0)
    {
        (void)fprintf(stderr, "wall_clock_test: %d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    (void)printf("wall_clock_test: all tests passed\n");
    return EXIT_SUCCESS;
}
