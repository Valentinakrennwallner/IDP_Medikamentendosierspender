#ifndef DOSING_H
#define DOSING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "startup_message.h"

/*
 * Dose evaluation and intake monitoring (ESP32).
 * Design Document, Chapter 2, Section 2.1 (safety module 2), Table 6, and
 * Chapter 6, Section 6.1, Requirements 1.6, 1.8, 2.9 and 2.11-2.13.
 *
 * The module is pure logic: it has no hardware, timer or logging
 * dependencies, so it can be unit-tested on the host. Times are seconds since
 * local midnight, weights are milligrams.
 */

/* Allowed deviation for "weight corresponds to" comparisons. */
#define DOSING_TOLERANCE_MILLIGRAMS 20
/*
 * Weight of the lightest tablet. A wrong amount whose deviation from the
 * target is smaller than this is shown blue instead of red (Requirement 1.8).
 * The ESP32 does not receive per-tablet weights, so a fixed value is used.
 */
#define DOSING_MIN_TABLET_WEIGHT_MILLIGRAMS 100
/* Every intake window starts at the intake time and lasts 1 h. */
#define DOSING_INTAKE_WINDOW_SECONDS 3600U
#define DOSING_SECONDS_PER_DAY 86400U
/* Upper limit for intakes per day; keeps the table statically sized. */
#define DOSING_MAX_INTAKES 16U
/* One initial row, plus one window row and one gap row per intake. */
#define DOSING_MAX_ROWS ((2U * DOSING_MAX_INTAKES) + 1U)
/* Capacity of the 100 g load cell. */
#define DOSING_MAX_TOTAL_MILLIGRAMS 100000U

typedef enum
{
    DOSING_PHASE_OUTSIDE_WINDOW = 0,
    DOSING_PHASE_INTAKE_WINDOW
} dosing_phase_t;

/* One row of the table of expected weights (Requirement 2.11, Table 6). */
typedef struct
{
    dosing_phase_t phase;
    uint32_t from_seconds;
    uint32_t to_seconds;
    int32_t weight_before_milligrams;
    int32_t weight_after_milligrams;
} dosing_row_t;

typedef struct
{
    dosing_row_t rows[DOSING_MAX_ROWS];
    size_t row_count;
    int32_t total_milligrams;
    uint32_t day_end_seconds;
    bool valid;
} dosing_table_t;

/* Result of one evaluation and the LED state it maps to (Table 1). */
typedef enum
{
    DOSING_RESULT_TAKEN = 0,          /* LED green */
    DOSING_RESULT_NOT_YET_TAKEN,      /* LED off, inside an intake window */
    DOSING_RESULT_WRONG_AMOUNT_SMALL, /* LED blue, deviation < one tablet */
    DOSING_RESULT_WRONG_AMOUNT,       /* LED red, inside an intake window */
    DOSING_RESULT_MISSED_OR_REMOVED,  /* LED red, outside an intake window */
    DOSING_RESULT_DAY_ENDED_OK,       /* LED off, last dose taken */
    DOSING_RESULT_DAY_ENDED_MISSED    /* LED red, last dose missed */
} dosing_result_t;

/*
 * Build the table of expected weights from the received schedule.
 * Returns 0 on success, -1 if the schedule is invalid: empty, unordered,
 * more than DOSING_MAX_INTAKES intakes, overlapping intake windows, a
 * window ending after midnight, or a total above the load cell capacity.
 * Requirement 2.11.
 */
int dosing_table_create(const startup_daily_schedule_t *schedule,
                        dosing_table_t *table);

/*
 * Check the start-up filling: the measured weight must correspond to the sum
 * of all target weights of the day. Requirement 2.9.
 */
bool dosing_filling_is_correct(const dosing_table_t *table,
                               int32_t weight_milligrams);

/*
 * Compare the measured weight with the row for the current time.
 * Returns 0 on success, -1 on invalid arguments.
 * Requirements 1.6, 1.8, 2.12 and 2.13.
 */
int dosing_evaluate(const dosing_table_t *table,
                    uint32_t now_seconds,
                    int32_t weight_milligrams,
                    dosing_result_t *result);

/* True for results that must be reported as a warning (Requirement 2.13). */
bool dosing_result_is_deviation(dosing_result_t result);

#endif
