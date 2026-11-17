#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../../protocol/include/startup_message.h"

/*
 * Intake schedule (Raspberry Pi).
 * Design Document, Chapter 2, Section 2.1 (safety module 4, "Intake schedule
 * and time control"), and Chapter 6, Requirements 1.5, 1.9-1.11 and 4.6.
 *
 * Loads the medications and intake times from the configuration file
 * maintained by the nursing staff, calculates the target weight of every
 * intake, builds the schedule that is sent to the ESP32 and the text of the
 * Telegram reminder. Times are seconds since local midnight, weights are
 * milligrams. The module does not log anything (Requirement 4.4).
 */

#define SCHEDULE_MAX_MEDICATIONS 16U
/* Same limit as DOSING_MAX_INTAKES on the ESP32. */
#define SCHEDULE_MAX_INTAKES 16U
#define SCHEDULE_MAX_ITEMS_PER_INTAKE 8U
#define SCHEDULE_ID_CAPACITY 32U
#define SCHEDULE_NAME_CAPACITY 64U
#define SCHEDULE_MAX_QUANTITY 100U
/* Capacity of the 100 g load cell. */
#define SCHEDULE_MAX_TOTAL_MILLIGRAMS 100000U
/* Every intake window lasts 1 h; windows must not overlap or pass midnight. */
#define SCHEDULE_INTAKE_WINDOW_SECONDS 3600U
#define SCHEDULE_SECONDS_PER_DAY 86400U
/* Largest accepted configuration file. */
#define SCHEDULE_MAX_FILE_SIZE 65536U

typedef enum
{
    SCHEDULE_RESULT_OK = 0,
    SCHEDULE_RESULT_INVALID_ARGUMENT = -1,
    SCHEDULE_RESULT_FILE_ERROR = -2,
    SCHEDULE_RESULT_PERMISSIONS = -3,
    SCHEDULE_RESULT_PARSE_ERROR = -4,
    SCHEDULE_RESULT_INVALID_CONTENT = -5,
    SCHEDULE_RESULT_BUFFER_TOO_SMALL = -6
} schedule_result_t;

typedef struct
{
    char id[SCHEDULE_ID_CAPACITY];
    char name[SCHEDULE_NAME_CAPACITY];
    uint32_t tablet_weight_milligrams;
} schedule_medication_t;

typedef struct
{
    size_t medication_index;
    uint32_t quantity;
} schedule_item_t;

typedef struct
{
    uint32_t time_seconds;
    schedule_item_t items[SCHEDULE_MAX_ITEMS_PER_INTAKE];
    size_t item_count;
    /* Sum of quantity x tablet weight (Requirement 1.10). */
    uint32_t target_weight_milligrams;
    /* Lightest tablet of this intake; kept for a later protocol extension. */
    uint32_t min_tablet_weight_milligrams;
} schedule_intake_t;

typedef struct
{
    schedule_medication_t medications[SCHEDULE_MAX_MEDICATIONS];
    size_t medication_count;
    /* Ordered by strictly increasing time. */
    schedule_intake_t intakes[SCHEDULE_MAX_INTAKES];
    size_t intake_count;
    uint32_t total_weight_milligrams;
} schedule_t;

/*
 * Read and validate the configuration file. The file must be a regular file
 * owned by the current user with no group or other permissions (mode 600,
 * Requirement 4.6). On any error the schedule must not be used and no
 * schedule may be sent to the ESP32 (Requirement 1.10).
 * Requirements 1.9, 1.10 and 4.6.
 */
schedule_result_t schedule_load(const char *path, schedule_t *schedule);

/*
 * Parse and validate configuration text (the "medications" and "intakes"
 * members; other members are ignored). Used by schedule_load() and by tests.
 * Requirements 1.9 and 1.10.
 */
schedule_result_t schedule_parse(const char *json_text, size_t json_length,
                                 schedule_t *schedule);

/*
 * Fill caller-owned entries with the intake times and target weights that
 * are sent to the ESP32 (Requirement 1.5).
 */
schedule_result_t schedule_build_startup(const schedule_t *schedule,
                                         startup_schedule_entry_t *entries,
                                         size_t entry_capacity,
                                         startup_daily_schedule_t *startup_schedule);

/*
 * Report whether an intake time lies in (previous_seconds, now_seconds].
 * The caller passes the time of its previous check, so each reminder is
 * triggered exactly once; a wrap past midnight (now < previous) is handled.
 * Returns true and the index of the first due intake if one is due.
 * Requirement 1.11.
 */
bool schedule_check_due(const schedule_t *schedule,
                        uint32_t previous_seconds,
                        uint32_t now_seconds,
                        size_t *intake_index);

/*
 * Write the Telegram reminder for one intake, listing only the names and
 * quantities of the medications to take (Requirements 1.11 and 4.5).
 * The text is NUL-terminated.
 */
schedule_result_t schedule_format_reminder(const schedule_t *schedule,
                                           size_t intake_index,
                                           char *text,
                                           size_t text_capacity);

#endif
