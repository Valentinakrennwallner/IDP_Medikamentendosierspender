#include "startup_message.h"

/* Write one 32-bit protocol field in network byte order. */
static void write_u32_be(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)((value >> 16U) & UINT32_C(0x000000FF));
    output[2] = (uint8_t)((value >> 8U) & UINT32_C(0x000000FF));
    output[3] = (uint8_t)(value & UINT32_C(0x000000FF));
}

/* Write a 64-bit protocol field as two network-byte-order words. */
static void write_u64_be(uint8_t *output, uint64_t value)
{
    uint32_t upper = (uint32_t)(value >> 32U);
    uint32_t lower = (uint32_t)(value & UINT64_C(0x00000000FFFFFFFF));

    write_u32_be(output, upper);
    write_u32_be(&output[4], lower);
}

/* Read one 32-bit network-byte-order protocol field. */
static uint32_t read_u32_be(const uint8_t *input)
{
    return ((uint32_t)input[0] << 24U) |
           ((uint32_t)input[1] << 16U) |
           ((uint32_t)input[2] << 8U) |
           (uint32_t)input[3];
}

/* Read a 64-bit network-byte-order protocol field as two words. */
static uint64_t read_u64_be(const uint8_t *input)
{
    uint64_t upper = (uint64_t)read_u32_be(input);
    uint64_t lower = (uint64_t)read_u32_be(&input[4]);

    return (upper << 32U) | lower;
}

/*
 * Validate the ordered, non-empty daily intake list before encoding or use.
 * Design Document, Chapter 6, Section 6.1, Requirements 1.5 and 1.10.
 */
bool startup_schedule_is_valid(const startup_daily_schedule_t *schedule)
{
    size_t index;
    uint32_t previous_time = 0U;

    if ((schedule == NULL) || (schedule->entries == NULL) ||
        (schedule->entry_count == 0U) ||
        (schedule->entry_count > (size_t)STARTUP_MESSAGE_MAX_ENTRIES))
    {
        return false;
    }

    for (index = 0U; index < schedule->entry_count; index++)
    {
        const startup_schedule_entry_t *entry = &schedule->entries[index];

        if ((entry->intake_time_seconds >= STARTUP_MESSAGE_SECONDS_PER_DAY) ||
            (entry->target_weight_milligrams == 0U) ||
            ((index > 0U) && (entry->intake_time_seconds <= previous_time)))
        {
            return false;
        }

        previous_time = entry->intake_time_seconds;
    }

    return true;
}

/*
 * Calculate the exact startup payload size after validating its schedule.
 * Design Document, Chapter 2, Section 2.1 (software modules).
 */
static bool startup_message_size(const startup_daily_schedule_t *schedule,
                                 size_t *message_size)
{
    size_t entries_size;

    if ((message_size == NULL) || !startup_schedule_is_valid(schedule))
    {
        return false;
    }

    if (schedule->entry_count >
        ((SIZE_MAX - (size_t)STARTUP_MESSAGE_HEADER_SIZE) /
         (size_t)STARTUP_MESSAGE_ENTRY_SIZE))
    {
        return false;
    }

    entries_size = schedule->entry_count * (size_t)STARTUP_MESSAGE_ENTRY_SIZE;
    *message_size = (size_t)STARTUP_MESSAGE_HEADER_SIZE + entries_size;
    return true;
}

/*
 * Encode the current Unix time and daily intake targets in the shared
 * big-endian startup-message format. Design Document, Chapter 2, Section 2.1,
 * Chapter 3 (Raspberry Pi flow), and Chapter 6, Section 6.1, Requirements 1.4
 * and 1.5.
 */
int startup_message_encode(const startup_daily_schedule_t *schedule,
                           int64_t current_unix_time,
                           uint8_t *output,
                           size_t output_capacity,
                           size_t *output_size)
{
    size_t required_size;
    size_t index;
    uint64_t encoded_time;

    if ((output == NULL) || (output_size == NULL) || (current_unix_time <= 0) ||
        !startup_message_size(schedule, &required_size) ||
        (output_capacity < required_size))
    {
        return -1;
    }

    encoded_time = (uint64_t)current_unix_time;
    output[0] = (uint8_t)STARTUP_MESSAGE_VERSION;
    output[1] = (uint8_t)STARTUP_MESSAGE_TYPE_SCHEDULE_AND_TIME;
    write_u64_be(&output[2], encoded_time);
    write_u32_be(&output[10], (uint32_t)schedule->entry_count);
    output[14] = 0U;
    output[15] = 0U;

    for (index = 0U; index < schedule->entry_count; index++)
    {
        size_t entry_offset = (size_t)STARTUP_MESSAGE_HEADER_SIZE +
                              (index * (size_t)STARTUP_MESSAGE_ENTRY_SIZE);
        write_u32_be(&output[entry_offset],
                     schedule->entries[index].intake_time_seconds);
        write_u32_be(&output[entry_offset + 4U],
                     schedule->entries[index].target_weight_milligrams);
    }

    *output_size = required_size;
    return 0;
}

/*
 * Decode and validate the authenticated startup payload for ESP32 use.
 * Design Document, Chapter 2, Section 2.1, Chapter 3 (ESP32 flow), and
 * Chapter 6, Section 6.1, Requirements 1.4 and 1.5.
 */
int startup_message_decode(const uint8_t *message,
                           size_t message_size,
                           startup_schedule_entry_t *entries,
                           size_t entry_capacity,
                           size_t *entry_count,
                           int64_t *current_unix_time)
{
    uint32_t decoded_entry_count;
    uint64_t decoded_time;
    size_t expected_size;
    size_t index;
    startup_daily_schedule_t decoded_schedule;

    if ((message == NULL) || (entries == NULL) || (entry_count == NULL) ||
        (current_unix_time == NULL) ||
        (message_size < (size_t)STARTUP_MESSAGE_HEADER_SIZE) ||
        (message[0] != (uint8_t)STARTUP_MESSAGE_VERSION) ||
        (message[1] != (uint8_t)STARTUP_MESSAGE_TYPE_SCHEDULE_AND_TIME) ||
        (message[14] != 0U) || (message[15] != 0U))
    {
        return -1;
    }

    decoded_time = read_u64_be(&message[2]);
    decoded_entry_count = read_u32_be(&message[10]);
    if ((decoded_time == 0U) || (decoded_time > (uint64_t)INT64_MAX) ||
        (decoded_entry_count == 0U) ||
        (decoded_entry_count > (uint32_t)STARTUP_MESSAGE_MAX_ENTRIES) ||
        ((size_t)decoded_entry_count > entry_capacity))
    {
        return -1;
    }

    expected_size = (size_t)STARTUP_MESSAGE_HEADER_SIZE +
                    ((size_t)decoded_entry_count *
                     (size_t)STARTUP_MESSAGE_ENTRY_SIZE);
    if (message_size != expected_size)
    {
        return -1;
    }

    for (index = 0U; index < (size_t)decoded_entry_count; index++)
    {
        size_t entry_offset = (size_t)STARTUP_MESSAGE_HEADER_SIZE +
                              (index * (size_t)STARTUP_MESSAGE_ENTRY_SIZE);
        entries[index].intake_time_seconds = read_u32_be(&message[entry_offset]);
        entries[index].target_weight_milligrams =
            read_u32_be(&message[entry_offset + 4U]);
    }

    decoded_schedule.entries = entries;
    decoded_schedule.entry_count = (size_t)decoded_entry_count;
    if (!startup_schedule_is_valid(&decoded_schedule))
    {
        return -1;
    }

    *entry_count = (size_t)decoded_entry_count;
    *current_unix_time = (int64_t)decoded_time;
    return 0;
}
