#ifndef STARTUP_MESSAGE_H
#define STARTUP_MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define STARTUP_MESSAGE_VERSION 1U
#define STARTUP_MESSAGE_TYPE_SCHEDULE_AND_TIME 1U
#define STARTUP_MESSAGE_HEADER_SIZE 16U
#define STARTUP_MESSAGE_ENTRY_SIZE 8U
#define STARTUP_MESSAGE_SECONDS_PER_DAY 86400U
#define STARTUP_MESSAGE_MAX_ENTRIES STARTUP_MESSAGE_SECONDS_PER_DAY

typedef struct
{
    uint32_t intake_time_seconds;
    uint32_t target_weight_milligrams;
} startup_schedule_entry_t;

typedef struct
{
    const startup_schedule_entry_t *entries;
    size_t entry_count;
} startup_daily_schedule_t;

bool startup_schedule_is_valid(const startup_daily_schedule_t *schedule);
bool startup_message_size(const startup_daily_schedule_t *schedule,
                          size_t *message_size);
int startup_message_encode(const startup_daily_schedule_t *schedule,
                           int64_t current_unix_time,
                           uint8_t *output,
                           size_t output_capacity,
                           size_t *output_size);
int startup_message_decode(const uint8_t *message,
                           size_t message_size,
                           startup_schedule_entry_t *entries,
                           size_t entry_capacity,
                           size_t *entry_count,
                           int64_t *current_unix_time);

#endif
