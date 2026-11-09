#include "schedule.h"

/*
 * Check the shared daily schedule constraints before the gateway transfers
 * intake times and target weights. Design Document, Chapter 2, Section 2.1
 * (intake schedule and time control) and Chapter 6, Section 6.1, Requirement
 * 1.5.
 */
bool schedule_is_valid_daily(const daily_schedule_t *schedule)
{
    return startup_schedule_is_valid(schedule);
}
