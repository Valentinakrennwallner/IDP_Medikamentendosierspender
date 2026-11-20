#include "dosing.h"

/*
 * Return true if the measured weight corresponds to the expected weight
 * within DOSING_TOLERANCE_MILLIGRAMS. The difference is computed in 64 bit so
 * that no int32_t overflow can occur.
 */
static bool dosing_weight_matches(int32_t measured_milligrams,
                                  int32_t expected_milligrams)
{
    int64_t difference = (int64_t)measured_milligrams - (int64_t)expected_milligrams;

    if (difference < 0)
    {
        difference = -difference;
    }

    return (difference <= (int64_t)DOSING_TOLERANCE_MILLIGRAMS);
}

/*
 * Return true if the deviation from the target is smaller than the weight of
 * one tablet (blue instead of red). Requirement 1.8.
 */
static bool dosing_deviation_is_small(int32_t measured_milligrams,
                                      int32_t target_milligrams)
{
    int64_t difference = (int64_t)measured_milligrams - (int64_t)target_milligrams;

    if (difference < 0)
    {
        difference = -difference;
    }

    return (difference < (int64_t)DOSING_MIN_TABLET_WEIGHT_MILLIGRAMS);
}

/*
 * Check the schedule constraints that the table relies on: number of
 * intakes, total weight within the load cell capacity, and intake windows
 * that neither overlap each other nor extend past midnight.
 */
static bool dosing_schedule_fits(const startup_daily_schedule_t *schedule,
                                 uint32_t *total_milligrams)
{
    bool fits = true;
    uint32_t total = 0U;
    size_t index;

    if ((schedule->entry_count == 0U) || (schedule->entry_count > DOSING_MAX_INTAKES))
    {
        fits = false;
    }

    for (index = 0U; (index < schedule->entry_count) && fits; index++)
    {
        const startup_schedule_entry_t *entry = &schedule->entries[index];
        uint32_t window_end = entry->intake_time_seconds + DOSING_INTAKE_WINDOW_SECONDS;

        /* Each step keeps total <= DOSING_MAX_TOTAL_MILLIGRAMS, so no overflow. */
        if (entry->target_weight_milligrams > (DOSING_MAX_TOTAL_MILLIGRAMS - total))
        {
            fits = false;
        }
        else if (window_end > DOSING_SECONDS_PER_DAY)
        {
            fits = false;
        }
        else if (((index + 1U) < schedule->entry_count) &&
                 (window_end > schedule->entries[index + 1U].intake_time_seconds))
        {
            fits = false;
        }
        else
        {
            total += entry->target_weight_milligrams;
        }
    }

    *total_milligrams = total;
    return fits;
}

/* Append one row to the table; the caller guarantees free capacity. */
static void dosing_add_row(dosing_table_t *table,
                           dosing_phase_t phase,
                           uint32_t from_seconds,
                           uint32_t to_seconds,
                           int32_t weight_before_milligrams,
                           int32_t weight_after_milligrams)
{
    dosing_row_t *row = &table->rows[table->row_count];

    row->phase = phase;
    row->from_seconds = from_seconds;
    row->to_seconds = to_seconds;
    row->weight_before_milligrams = weight_before_milligrams;
    row->weight_after_milligrams = weight_after_milligrams;
    table->row_count++;
}

/*
 * Build the table of expected weights for the day. Rows are contiguous from
 * midnight to the end of the last intake window: an optional initial row
 * before the first intake, then for each intake its window row and, if there
 * is a gap, a row until the next intake. Between two windows the expected
 * weight before and after is identical.
 * Design Document, Chapter 3, Table 6, and Chapter 6, Section 6.1,
 * Requirement 2.11.
 */
int dosing_table_create(const startup_daily_schedule_t *schedule,
                        dosing_table_t *table)
{
    int status = 0;
    uint32_t total = 0U;

    if ((schedule == NULL) || (table == NULL))
    {
        status = -1;
    }
    else
    {
        table->valid = false;
        table->row_count = 0U;

        if ((!startup_schedule_is_valid(schedule)) || (!dosing_schedule_fits(schedule, &total)))
        {
            status = -1;
        }
    }

    if (status == 0)
    {
        int32_t remaining;
        size_t index;

        /* total <= DOSING_MAX_TOTAL_MILLIGRAMS, so it fits into int32_t. */
        table->total_milligrams = (int32_t)total;
        remaining = table->total_milligrams;

        if (schedule->entries[0].intake_time_seconds > 0U)
        {
            dosing_add_row(table, DOSING_PHASE_OUTSIDE_WINDOW, 0U,
                           schedule->entries[0].intake_time_seconds, remaining, remaining);
        }

        for (index = 0U; index < schedule->entry_count; index++)
        {
            const startup_schedule_entry_t *entry = &schedule->entries[index];
            uint32_t window_end = entry->intake_time_seconds + DOSING_INTAKE_WINDOW_SECONDS;
            int32_t after = remaining - (int32_t)entry->target_weight_milligrams;

            dosing_add_row(table, DOSING_PHASE_INTAKE_WINDOW, entry->intake_time_seconds,
                           window_end, remaining, after);
            remaining = after;

            if (((index + 1U) < schedule->entry_count) &&
                (window_end < schedule->entries[index + 1U].intake_time_seconds))
            {
                dosing_add_row(table, DOSING_PHASE_OUTSIDE_WINDOW, window_end,
                               schedule->entries[index + 1U].intake_time_seconds, remaining,
                               remaining);
            }
        }

        table->day_end_seconds = table->rows[table->row_count - 1U].to_seconds;
        table->valid = true;
    }

    return status;
}

/*
 * Start-up filling check: the measured weight must correspond to the sum of
 * the target weights of the day within the tolerance.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.9.
 */
bool dosing_filling_is_correct(const dosing_table_t *table, int32_t weight_milligrams)
{
    bool correct = false;

    if ((table != NULL) && table->valid)
    {
        correct = dosing_weight_matches(weight_milligrams, table->total_milligrams);
    }

    return correct;
}

/*
 * Evaluate the measured weight against the row for the current time.
 * First compare with W_after: a match means the dose was taken, also outside
 * an intake window (late intake). Otherwise, inside an intake window W_before
 * means "not yet taken" and any other weight is a wrong amount (blue if the
 * deviation is smaller than one tablet). Outside a window the sign of the
 * deviation from W_after decides: a lower weight is an unscheduled removal;
 * a higher weight is a missed dose if an intake window has already ended
 * (gap row), or an unexpected increase before the first window (initial
 * row, the only outside-window row that can be rows[0]). After the last
 * window the day-end check of Requirement 1.6 applies.
 * Design Document, Chapter 3, Figure 4, and Chapter 6, Section 6.1,
 * Requirements 1.6, 1.8, 2.12 and 2.13.
 */
int dosing_evaluate(const dosing_table_t *table,
                    uint32_t now_seconds,
                    int32_t weight_milligrams,
                    dosing_result_t *result)
{
    int status = 0;
    const dosing_row_t *row = NULL;
    size_t index;

    if ((table == NULL) || (result == NULL) || (!table->valid) ||
        (now_seconds >= DOSING_SECONDS_PER_DAY))
    {
        status = -1;
    }
    else if (now_seconds >= table->day_end_seconds)
    {
        row = &table->rows[table->row_count - 1U];
        *result = dosing_weight_matches(weight_milligrams, row->weight_after_milligrams)
                      ? DOSING_RESULT_DAY_ENDED_OK
                      : DOSING_RESULT_DAY_ENDED_MISSED;
    }
    else
    {
        for (index = 0U; (index < table->row_count) && (row == NULL); index++)
        {
            if ((now_seconds >= table->rows[index].from_seconds) &&
                (now_seconds < table->rows[index].to_seconds))
            {
                row = &table->rows[index];
            }
        }

        if (row == NULL)
        {
            /* Unreachable for a valid table, since rows cover the whole day. */
            status = -1;
        }
        else if (dosing_weight_matches(weight_milligrams, row->weight_after_milligrams))
        {
            *result = DOSING_RESULT_TAKEN;
        }
        else if (row->phase == DOSING_PHASE_INTAKE_WINDOW)
        {
            if (dosing_weight_matches(weight_milligrams, row->weight_before_milligrams))
            {
                *result = DOSING_RESULT_NOT_YET_TAKEN;
            }
            else if (dosing_deviation_is_small(weight_milligrams, row->weight_after_milligrams))
            {
                *result = DOSING_RESULT_WRONG_AMOUNT_SMALL;
            }
            else
            {
                *result = DOSING_RESULT_WRONG_AMOUNT;
            }
        }
        else if (weight_milligrams < row->weight_after_milligrams)
        {
            *result = DOSING_RESULT_UNSCHEDULED_REMOVAL;
        }
        else if (row == &table->rows[0])
        {
            *result = DOSING_RESULT_UNEXPECTED_INCREASE;
        }
        else
        {
            *result = DOSING_RESULT_MISSED;
        }
    }

    return status;
}

/*
 * Results that the Raspberry Pi must forward as a Telegram warning.
 * Design Document, Chapter 6, Section 6.1, Requirements 1.6, 1.8 and 2.13.
 */
bool dosing_result_is_deviation(dosing_result_t result)
{
    return ((result == DOSING_RESULT_WRONG_AMOUNT_SMALL) ||
            (result == DOSING_RESULT_WRONG_AMOUNT) ||
            (result == DOSING_RESULT_MISSED) ||
            (result == DOSING_RESULT_UNSCHEDULED_REMOVAL) ||
            (result == DOSING_RESULT_UNEXPECTED_INCREASE) ||
            (result == DOSING_RESULT_DAY_ENDED_MISSED));
}
