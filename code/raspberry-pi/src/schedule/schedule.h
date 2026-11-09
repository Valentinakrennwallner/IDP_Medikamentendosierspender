#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../../protocol/include/startup_message.h"

typedef startup_schedule_entry_t schedule_entry_t;
typedef startup_daily_schedule_t daily_schedule_t;

bool schedule_is_valid_daily(const daily_schedule_t *schedule);

#endif
