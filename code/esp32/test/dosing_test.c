#include "../components/dosing/dosing.h"

#include <stdio.h>

#define HOURS(h) ((uint32_t)(h) * 3600U)
#define MINUTES(m) ((uint32_t)(m) * 60U)

/* Schedule of Design Document, Chapter 3, Table 6: 07:00, 12:00, 18:00. */
static const startup_schedule_entry_t table6_entries[] = {
    {HOURS(7), 100U},
    {HOURS(12), 200U},
    {HOURS(18), 100U},
};
static const startup_daily_schedule_t table6_schedule = {table6_entries, 3U};

/* Build the Table 6 schedule; returns nonzero on failure. */
static int create_table6(dosing_table_t *table)
{
    return dosing_table_create(&table6_schedule, table);
}

/* Return nonzero if the evaluation fails or differs from the expectation. */
static int expect_result(const dosing_table_t *table,
                         uint32_t now_seconds,
                         int32_t weight_milligrams,
                         dosing_result_t expected)
{
    dosing_result_t result = DOSING_RESULT_TAKEN;

    if ((dosing_evaluate(table, now_seconds, weight_milligrams, &result) != 0) ||
        (result != expected))
    {
        printf("  evaluate(t=%u s, w=%d mg) gave %d, expected %d\n",
               (unsigned int)now_seconds, (int)weight_milligrams, (int)result,
               (int)expected);
        return 1;
    }
    return 0;
}

/* Requirement 2.11: rows, times and expected weights match Table 6. */
static int test_table_matches_table6(void)
{
    static const dosing_row_t expected[] = {
        {DOSING_PHASE_OUTSIDE_WINDOW, 0U, HOURS(7), 400, 400},
        {DOSING_PHASE_INTAKE_WINDOW, HOURS(7), HOURS(8), 400, 300},
        {DOSING_PHASE_OUTSIDE_WINDOW, HOURS(8), HOURS(12), 300, 300},
        {DOSING_PHASE_INTAKE_WINDOW, HOURS(12), HOURS(13), 300, 100},
        {DOSING_PHASE_OUTSIDE_WINDOW, HOURS(13), HOURS(18), 100, 100},
        {DOSING_PHASE_INTAKE_WINDOW, HOURS(18), HOURS(19), 100, 0},
    };
    dosing_table_t table;
    size_t index;

    if ((create_table6(&table) != 0) || (!table.valid) || (table.row_count != 6U) ||
        (table.total_milligrams != 400) || (table.day_end_seconds != HOURS(19)))
    {
        return 1;
    }
    for (index = 0U; index < 6U; index++)
    {
        const dosing_row_t *row = &table.rows[index];

        if ((row->phase != expected[index].phase) ||
            (row->from_seconds != expected[index].from_seconds) ||
            (row->to_seconds != expected[index].to_seconds) ||
            (row->weight_before_milligrams != expected[index].weight_before_milligrams) ||
            (row->weight_after_milligrams != expected[index].weight_after_milligrams))
        {
            return 1;
        }
    }
    return 0;
}

/* Requirement 2.9: filling must equal the daily total within the tolerance. */
static int test_filling_check(void)
{
    dosing_table_t table;

    if (create_table6(&table) != 0)
    {
        return 1;
    }
    if ((!dosing_filling_is_correct(&table, 400)) ||
        (!dosing_filling_is_correct(&table, 400 + DOSING_TOLERANCE_MILLIGRAMS)) ||
        (!dosing_filling_is_correct(&table, 400 - DOSING_TOLERANCE_MILLIGRAMS)) ||
        dosing_filling_is_correct(&table, 400 + DOSING_TOLERANCE_MILLIGRAMS + 1) ||
        dosing_filling_is_correct(&table, 300) || dosing_filling_is_correct(&table, 0))
    {
        return 1;
    }
    return 0;
}

/* Requirements 1.8 and 2.13: results inside an intake window. */
static int test_inside_intake_window(void)
{
    dosing_table_t table;
    int failures = 0;

    if (create_table6(&table) != 0)
    {
        return 1;
    }
    /* Not yet taken: W ~ W_before, LED off. */
    failures += expect_result(&table, HOURS(7) + MINUTES(30), 400, DOSING_RESULT_NOT_YET_TAKEN);
    failures += expect_result(&table, HOURS(7) + MINUTES(30), 415, DOSING_RESULT_NOT_YET_TAKEN);
    /* Taken: W ~ W_after, LED green. */
    failures += expect_result(&table, HOURS(7) + MINUTES(30), 300, DOSING_RESULT_TAKEN);
    failures += expect_result(&table, HOURS(7) + MINUTES(30), 285, DOSING_RESULT_TAKEN);
    /* Deviation smaller than one tablet: blue. */
    failures += expect_result(&table, HOURS(7) + MINUTES(30), 350,
                              DOSING_RESULT_WRONG_AMOUNT_SMALL);
    failures += expect_result(&table, HOURS(7) + MINUTES(30), 250,
                              DOSING_RESULT_WRONG_AMOUNT_SMALL);
    /* Deviation of one tablet or more: red (overdose). */
    failures += expect_result(&table, HOURS(7) + MINUTES(30), 200, DOSING_RESULT_WRONG_AMOUNT);
    failures += expect_result(&table, HOURS(12) + MINUTES(10), 0, DOSING_RESULT_WRONG_AMOUNT);
    return (failures != 0) ? 1 : 0;
}

/* Requirements 1.8, 2.12 and 2.13: results outside an intake window. */
static int test_outside_intake_window(void)
{
    dosing_table_t table;
    int failures = 0;

    if (create_table6(&table) != 0)
    {
        return 1;
    }
    /* Correctly filled before the first window: W ~ W_after, green. */
    failures += expect_result(&table, HOURS(6), 400, DOSING_RESULT_TAKEN);
    /* Unscheduled removal before the first window: red. */
    failures += expect_result(&table, HOURS(6), 300, DOSING_RESULT_UNSCHEDULED_REMOVAL);
    /* Weight increase before the first window (no window has ended): red. */
    failures += expect_result(&table, HOURS(6), 500, DOSING_RESULT_UNEXPECTED_INCREASE);
    failures += expect_result(&table, 0U, 400 + DOSING_TOLERANCE_MILLIGRAMS + 1,
                              DOSING_RESULT_UNEXPECTED_INCREASE);
    /* Dose taken in time, gap afterwards: green. */
    failures += expect_result(&table, HOURS(9), 300, DOSING_RESULT_TAKEN);
    /* Missed dose after the window ended: red. */
    failures += expect_result(&table, HOURS(9), 400, DOSING_RESULT_MISSED);
    /* Dose only partly taken in the window, window ended: still missed. */
    failures += expect_result(&table, HOURS(9), 350, DOSING_RESULT_MISSED);
    failures += expect_result(&table, HOURS(15), 100 + DOSING_TOLERANCE_MILLIGRAMS + 1,
                              DOSING_RESULT_MISSED);
    /* Unscheduled removal between windows: red. */
    failures += expect_result(&table, HOURS(15), 50, DOSING_RESULT_UNSCHEDULED_REMOVAL);
    failures += expect_result(&table, HOURS(9), 300 - DOSING_TOLERANCE_MILLIGRAMS - 1,
                              DOSING_RESULT_UNSCHEDULED_REMOVAL);
    /*
     * Known limitation: an overdose inside a window is reported as an
     * unscheduled removal once the window has ended (stateless evaluation).
     */
    failures += expect_result(&table, HOURS(7) + MINUTES(30), 200, DOSING_RESULT_WRONG_AMOUNT);
    failures += expect_result(&table, HOURS(9), 200, DOSING_RESULT_UNSCHEDULED_REMOVAL);
    return (failures != 0) ? 1 : 0;
}

/* Without an initial row, a higher weight in the first gap is a missed dose. */
static int test_missed_without_initial_row(void)
{
    static const startup_schedule_entry_t entries[] = {{0U, 100U}, {HOURS(5), 100U}};
    const startup_daily_schedule_t schedule = {entries, 2U};
    dosing_table_t table;
    int failures = 0;

    if ((dosing_table_create(&schedule, &table) != 0) ||
        (table.rows[0].phase != DOSING_PHASE_INTAKE_WINDOW))
    {
        return 1;
    }
    failures += expect_result(&table, HOURS(2), 200, DOSING_RESULT_MISSED);
    failures += expect_result(&table, HOURS(2), 50, DOSING_RESULT_UNSCHEDULED_REMOVAL);
    return (failures != 0) ? 1 : 0;
}

/* Requirement 2.12: a missed dose taken later turns the LED green again. */
static int test_late_intake(void)
{
    dosing_table_t table;
    int failures = 0;

    if (create_table6(&table) != 0)
    {
        return 1;
    }
    failures += expect_result(&table, HOURS(10), 400, DOSING_RESULT_MISSED);
    failures += expect_result(&table, HOURS(10) + MINUTES(5), 300, DOSING_RESULT_TAKEN);
    return (failures != 0) ? 1 : 0;
}

/* Window boundaries: start inclusive, end exclusive (1 h windows). */
static int test_window_boundaries(void)
{
    dosing_table_t table;
    int failures = 0;

    if (create_table6(&table) != 0)
    {
        return 1;
    }
    failures += expect_result(&table, HOURS(7) - 1U, 400, DOSING_RESULT_TAKEN);
    failures += expect_result(&table, HOURS(7), 400, DOSING_RESULT_NOT_YET_TAKEN);
    failures += expect_result(&table, HOURS(8) - 1U, 400, DOSING_RESULT_NOT_YET_TAKEN);
    failures += expect_result(&table, HOURS(8), 400, DOSING_RESULT_MISSED);
    failures += expect_result(&table, HOURS(8), 200, DOSING_RESULT_UNSCHEDULED_REMOVAL);
    return (failures != 0) ? 1 : 0;
}

/* Requirement 1.6: day-end check against W_after of the last window. */
static int test_day_end(void)
{
    dosing_table_t table;
    int failures = 0;

    if (create_table6(&table) != 0)
    {
        return 1;
    }
    failures += expect_result(&table, HOURS(19), 0, DOSING_RESULT_DAY_ENDED_OK);
    failures += expect_result(&table, HOURS(19), 15, DOSING_RESULT_DAY_ENDED_OK);
    failures += expect_result(&table, HOURS(19), 100, DOSING_RESULT_DAY_ENDED_MISSED);
    failures += expect_result(&table, DOSING_SECONDS_PER_DAY - 1U, 100,
                              DOSING_RESULT_DAY_ENDED_MISSED);
    return (failures != 0) ? 1 : 0;
}

/* Adjacent windows have no gap row; an intake at midnight has no initial row. */
static int test_row_layout_edge_cases(void)
{
    static const startup_schedule_entry_t adjacent[] = {{HOURS(7), 100U}, {HOURS(8), 100U}};
    static const startup_schedule_entry_t midnight[] = {{0U, 100U}};
    const startup_daily_schedule_t adjacent_schedule = {adjacent, 2U};
    const startup_daily_schedule_t midnight_schedule = {midnight, 1U};
    dosing_table_t table;

    if ((dosing_table_create(&adjacent_schedule, &table) != 0) || (table.row_count != 3U) ||
        (table.rows[2].from_seconds != HOURS(8)) ||
        (table.rows[2].weight_before_milligrams != 100))
    {
        return 1;
    }
    if ((dosing_table_create(&midnight_schedule, &table) != 0) || (table.row_count != 1U) ||
        (table.rows[0].phase != DOSING_PHASE_INTAKE_WINDOW) ||
        (table.day_end_seconds != HOURS(1)))
    {
        return 1;
    }
    return 0;
}

/* Invalid schedules are rejected and leave the table marked invalid. */
static int test_invalid_schedules(void)
{
    static const startup_schedule_entry_t overlapping[] = {{HOURS(7), 100U},
                                                           {HOURS(7) + MINUTES(30), 100U}};
    static const startup_schedule_entry_t past_midnight[] = {{HOURS(23) + MINUTES(30), 100U}};
    static const startup_schedule_entry_t too_heavy[] = {{HOURS(7), 60000U},
                                                         {HOURS(12), 50000U}};
    static const startup_schedule_entry_t unordered[] = {{HOURS(12), 100U}, {HOURS(7), 100U}};
    startup_schedule_entry_t too_many[DOSING_MAX_INTAKES + 1U];
    const startup_daily_schedule_t schedules[] = {
        {table6_entries, 0U},
        {overlapping, 2U},
        {past_midnight, 1U},
        {too_heavy, 2U},
        {unordered, 2U},
        {too_many, DOSING_MAX_INTAKES + 1U},
    };
    dosing_table_t table;
    dosing_result_t result = DOSING_RESULT_TAKEN;
    size_t index;

    for (index = 0U; index < (DOSING_MAX_INTAKES + 1U); index++)
    {
        too_many[index].intake_time_seconds = (uint32_t)index * HOURS(1);
        too_many[index].target_weight_milligrams = 10U;
    }
    for (index = 0U; index < (sizeof(schedules) / sizeof(schedules[0])); index++)
    {
        if ((dosing_table_create(&schedules[index], &table) == 0) || table.valid ||
            (dosing_evaluate(&table, HOURS(7), 0, &result) == 0) ||
            dosing_filling_is_correct(&table, 0))
        {
            printf("  invalid schedule %u was accepted\n", (unsigned int)index);
            return 1;
        }
    }
    if ((dosing_table_create(NULL, &table) == 0) ||
        (dosing_table_create(&table6_schedule, NULL) == 0))
    {
        return 1;
    }
    return 0;
}

/* Invalid evaluation arguments are rejected. */
static int test_invalid_evaluation_arguments(void)
{
    dosing_table_t table;
    dosing_result_t result = DOSING_RESULT_TAKEN;

    if (create_table6(&table) != 0)
    {
        return 1;
    }
    if ((dosing_evaluate(NULL, HOURS(7), 0, &result) == 0) ||
        (dosing_evaluate(&table, HOURS(7), 0, NULL) == 0) ||
        (dosing_evaluate(&table, DOSING_SECONDS_PER_DAY, 0, &result) == 0))
    {
        return 1;
    }
    return 0;
}

/* Requirement 2.13: only deviations are reported as warnings. */
static int test_deviation_classification(void)
{
    if (dosing_result_is_deviation(DOSING_RESULT_TAKEN) ||
        dosing_result_is_deviation(DOSING_RESULT_NOT_YET_TAKEN) ||
        dosing_result_is_deviation(DOSING_RESULT_DAY_ENDED_OK) ||
        (!dosing_result_is_deviation(DOSING_RESULT_WRONG_AMOUNT_SMALL)) ||
        (!dosing_result_is_deviation(DOSING_RESULT_WRONG_AMOUNT)) ||
        (!dosing_result_is_deviation(DOSING_RESULT_MISSED)) ||
        (!dosing_result_is_deviation(DOSING_RESULT_UNSCHEDULED_REMOVAL)) ||
        (!dosing_result_is_deviation(DOSING_RESULT_UNEXPECTED_INCREASE)) ||
        (!dosing_result_is_deviation(DOSING_RESULT_DAY_ENDED_MISSED)))
    {
        return 1;
    }
    return 0;
}

/* Run all dose-evaluation tests and report failures. */
int main(void)
{
    struct
    {
        const char *name;
        int (*run)(void);
    } tests[] = {
        {"table_matches_table6", test_table_matches_table6},
        {"filling_check", test_filling_check},
        {"inside_intake_window", test_inside_intake_window},
        {"outside_intake_window", test_outside_intake_window},
        {"missed_without_initial_row", test_missed_without_initial_row},
        {"late_intake", test_late_intake},
        {"window_boundaries", test_window_boundaries},
        {"day_end", test_day_end},
        {"row_layout_edge_cases", test_row_layout_edge_cases},
        {"invalid_schedules", test_invalid_schedules},
        {"invalid_evaluation_arguments", test_invalid_evaluation_arguments},
        {"deviation_classification", test_deviation_classification},
    };
    int failures = 0;
    size_t index;

    for (index = 0U; index < (sizeof(tests) / sizeof(tests[0])); index++)
    {
        if (tests[index].run() != 0)
        {
            printf("FAIL %s\n", tests[index].name);
            failures++;
        }
    }

    if (failures == 0)
    {
        printf("All dosing tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
