/*
 * Required for open() flags, fstat() and geteuid() with -std=c11.
 * MISRA C deviation (Rule 21.1): defining this reserved feature-test macro
 * is the POSIX-specified way to request these declarations.
 */
#define _POSIX_C_SOURCE 200809L

#include "schedule.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * JSON parsing uses the json-c library (https://github.com/json-c/json-c,
 * MIT licence). It allocates memory internally; parsing only happens once at
 * start-up, outside the safety-critical runtime path.
 */
#include <json-c/json.h>

#define SCHEDULE_TIME_TEXT_LENGTH 5U
#define SCHEDULE_REMINDER_PREFIX "Erinnerung: Bitte jetzt einnehmen: "

/* File buffer; schedule_load() is called once at start-up (not reentrant). */
static char schedule_file_buffer[SCHEDULE_MAX_FILE_SIZE + 1U];

/*
 * Copy a JSON string member into a fixed buffer. The string must be
 * non-empty, fit the buffer and contain no control characters, so it is safe
 * to use in the reminder text.
 */
static bool schedule_copy_string(json_object *value, char *output, size_t capacity)
{
    bool valid = false;

    if ((value != NULL) && json_object_is_type(value, json_type_string))
    {
        const char *text = json_object_get_string(value);
        size_t length = (size_t)json_object_get_string_len(value);
        size_t index;

        valid = (length > 0U) && (length < capacity);
        for (index = 0U; (index < length) && valid; index++)
        {
            unsigned char character = (unsigned char)text[index];

            if ((character < 0x20U) || (character == 0x7FU))
            {
                valid = false;
            }
        }

        if (valid)
        {
            (void)memcpy(output, text, length);
            output[length] = '\0';
        }
    }

    return valid;
}

/* Return true if the character is an ASCII decimal digit. */
static bool schedule_is_digit(char character)
{
    return (character >= '0') && (character <= '9');
}

/* Value of an ASCII decimal digit; the caller checked schedule_is_digit(). */
static uint32_t schedule_digit_value(char character)
{
    return (uint32_t)(unsigned char)character - (uint32_t)(unsigned char)'0';
}

/* Parse a strict "HH:MM" time into seconds since midnight. */
static bool schedule_parse_time(json_object *value, uint32_t *time_seconds)
{
    bool valid = false;

    if ((value != NULL) && json_object_is_type(value, json_type_string) &&
        ((size_t)json_object_get_string_len(value) == SCHEDULE_TIME_TEXT_LENGTH))
    {
        const char *text = json_object_get_string(value);

        if (schedule_is_digit(text[0]) && schedule_is_digit(text[1]) && (text[2] == ':') &&
            schedule_is_digit(text[3]) && schedule_is_digit(text[4]))
        {
            uint32_t hours = (schedule_digit_value(text[0]) * 10U) + schedule_digit_value(text[1]);
            uint32_t minutes =
                (schedule_digit_value(text[3]) * 10U) + schedule_digit_value(text[4]);

            if ((hours < 24U) && (minutes < 60U))
            {
                *time_seconds = (hours * 3600U) + (minutes * 60U);
                valid = true;
            }
        }
    }

    return valid;
}

/* Convert a tablet weight in grams to whole milligrams (rounded, >= 1 mg). */
static bool schedule_parse_tablet_weight(json_object *value, uint32_t *weight_milligrams)
{
    bool valid = false;

    if ((value != NULL) && (json_object_is_type(value, json_type_double) ||
                            json_object_is_type(value, json_type_int)))
    {
        double milligrams = json_object_get_double(value) * 1000.0;

        if ((isfinite(milligrams) != 0) && (milligrams >= 0.5) &&
            (milligrams <= (double)SCHEDULE_MAX_TOTAL_MILLIGRAMS))
        {
            *weight_milligrams = (uint32_t)(milligrams + 0.5);
            valid = true;
        }
    }

    return valid;
}

/* Return true and the index if a medication with this id exists. */
static bool schedule_find_medication(const schedule_t *schedule, const char *id, size_t *index)
{
    bool found = false;
    size_t candidate;

    for (candidate = 0U; (candidate < schedule->medication_count) && !found; candidate++)
    {
        if (strcmp(schedule->medications[candidate].id, id) == 0)
        {
            *index = candidate;
            found = true;
        }
    }

    return found;
}

/* Parse the "medications" array: unique ids, names and tablet weights. */
static bool schedule_parse_medications(json_object *root, schedule_t *schedule)
{
    json_object *array = NULL;
    bool valid = false;

    if (json_object_object_get_ex(root, "medications", &array) &&
        json_object_is_type(array, json_type_array))
    {
        size_t count = json_object_array_length(array);
        size_t index;

        valid = (count > 0U) && (count <= SCHEDULE_MAX_MEDICATIONS);
        for (index = 0U; (index < count) && valid; index++)
        {
            json_object *item = json_object_array_get_idx(array, index);
            json_object *member = NULL;
            schedule_medication_t *medication = &schedule->medications[index];
            size_t duplicate = 0U;

            valid = json_object_is_type(item, json_type_object) &&
                    json_object_object_get_ex(item, "id", &member) &&
                    schedule_copy_string(member, medication->id, sizeof(medication->id)) &&
                    !schedule_find_medication(schedule, medication->id, &duplicate) &&
                    json_object_object_get_ex(item, "name", &member) &&
                    schedule_copy_string(member, medication->name, sizeof(medication->name)) &&
                    json_object_object_get_ex(item, "tablet_weight_g", &member) &&
                    schedule_parse_tablet_weight(member, &medication->tablet_weight_milligrams);
            if (valid)
            {
                /* Counted only after validation, so the duplicate check is correct. */
                schedule->medication_count = index + 1U;
            }
        }
    }

    return valid;
}

/* Parse the medication list of one intake and calculate its target weight. */
static bool schedule_parse_intake_items(json_object *array, const schedule_t *schedule,
                                        schedule_intake_t *intake)
{
    bool valid = false;

    if ((array != NULL) && json_object_is_type(array, json_type_array))
    {
        size_t count = json_object_array_length(array);
        size_t index;

        valid = (count > 0U) && (count <= SCHEDULE_MAX_ITEMS_PER_INTAKE);
        intake->target_weight_milligrams = 0U;
        intake->min_tablet_weight_milligrams = 0U;

        for (index = 0U; (index < count) && valid; index++)
        {
            json_object *item = json_object_array_get_idx(array, index);
            json_object *member = NULL;
            char id[SCHEDULE_ID_CAPACITY];
            size_t medication_index = 0U;
            int64_t quantity = 0;
            size_t previous;

            valid = json_object_is_type(item, json_type_object) &&
                    json_object_object_get_ex(item, "id", &member) &&
                    schedule_copy_string(member, id, sizeof(id)) &&
                    schedule_find_medication(schedule, id, &medication_index) &&
                    json_object_object_get_ex(item, "quantity", &member) &&
                    json_object_is_type(member, json_type_int);
            if (valid)
            {
                quantity = json_object_get_int64(member);
                valid = (quantity >= 1) && (quantity <= (int64_t)SCHEDULE_MAX_QUANTITY);
            }
            /* The same medication must not appear twice in one intake. */
            for (previous = 0U; (previous < index) && valid; previous++)
            {
                if (intake->items[previous].medication_index == medication_index)
                {
                    valid = false;
                }
            }

            if (valid)
            {
                uint32_t tablet = schedule->medications[medication_index].tablet_weight_milligrams;
                /* quantity <= 100 and tablet <= 100000 mg: product fits uint32_t. */
                uint32_t weight = (uint32_t)quantity * tablet;

                if (weight > (SCHEDULE_MAX_TOTAL_MILLIGRAMS - intake->target_weight_milligrams))
                {
                    valid = false;
                }
                else
                {
                    intake->items[index].medication_index = medication_index;
                    intake->items[index].quantity = (uint32_t)quantity;
                    intake->item_count = index + 1U;
                    intake->target_weight_milligrams += weight;
                    if ((intake->min_tablet_weight_milligrams == 0U) ||
                        (tablet < intake->min_tablet_weight_milligrams))
                    {
                        intake->min_tablet_weight_milligrams = tablet;
                    }
                }
            }
        }
    }

    return valid;
}

/* Sort intakes by time (insertion sort; at most SCHEDULE_MAX_INTAKES). */
static void schedule_sort_intakes(schedule_t *schedule)
{
    size_t index;

    for (index = 1U; index < schedule->intake_count; index++)
    {
        schedule_intake_t current = schedule->intakes[index];
        size_t position = index;

        while ((position > 0U) &&
               (schedule->intakes[position - 1U].time_seconds > current.time_seconds))
        {
            schedule->intakes[position] = schedule->intakes[position - 1U];
            position--;
        }
        schedule->intakes[position] = current;
    }
}

/*
 * Check the ordered intakes: 1 h windows must not overlap or pass midnight,
 * and the daily total must fit the load cell. Also rejects equal times.
 */
static bool schedule_check_intakes(schedule_t *schedule)
{
    bool valid = true;
    uint32_t total = 0U;
    size_t index;

    for (index = 0U; (index < schedule->intake_count) && valid; index++)
    {
        const schedule_intake_t *intake = &schedule->intakes[index];
        uint32_t window_end = intake->time_seconds + SCHEDULE_INTAKE_WINDOW_SECONDS;

        if ((window_end > SCHEDULE_SECONDS_PER_DAY) ||
            (((index + 1U) < schedule->intake_count) &&
             (window_end > schedule->intakes[index + 1U].time_seconds)) ||
            (intake->target_weight_milligrams > (SCHEDULE_MAX_TOTAL_MILLIGRAMS - total)))
        {
            valid = false;
        }
        else
        {
            total += intake->target_weight_milligrams;
        }
    }

    schedule->total_weight_milligrams = total;
    return valid;
}

/* Parse the "intakes" array, then sort and check it. */
static bool schedule_parse_intakes(json_object *root, schedule_t *schedule)
{
    json_object *array = NULL;
    bool valid = false;

    if (json_object_object_get_ex(root, "intakes", &array) &&
        json_object_is_type(array, json_type_array))
    {
        size_t count = json_object_array_length(array);
        size_t index;

        valid = (count > 0U) && (count <= SCHEDULE_MAX_INTAKES);
        for (index = 0U; (index < count) && valid; index++)
        {
            json_object *item = json_object_array_get_idx(array, index);
            json_object *member = NULL;
            schedule_intake_t *intake = &schedule->intakes[index];

            valid = json_object_is_type(item, json_type_object) &&
                    json_object_object_get_ex(item, "time", &member) &&
                    schedule_parse_time(member, &intake->time_seconds) &&
                    json_object_object_get_ex(item, "medications", &member) &&
                    schedule_parse_intake_items(member, schedule, intake);
            if (valid)
            {
                schedule->intake_count = index + 1U;
            }
        }
    }

    if (valid)
    {
        schedule_sort_intakes(schedule);
        valid = schedule_check_intakes(schedule);
    }

    return valid;
}

/*
 * Parse and validate the configuration text. The whole text must be one
 * JSON object (strict mode, no trailing characters).
 * Design Document, Chapter 6, Section 6.1, Requirements 1.9 and 1.10.
 */
schedule_result_t schedule_parse(const char *json_text, size_t json_length,
                                 schedule_t *schedule)
{
    schedule_result_t result = SCHEDULE_RESULT_OK;
    json_tokener *tokener = NULL;
    json_object *root = NULL;

    if ((json_text == NULL) || (schedule == NULL) || (json_length == 0U) ||
        (json_length > SCHEDULE_MAX_FILE_SIZE))
    {
        result = SCHEDULE_RESULT_INVALID_ARGUMENT;
    }
    else
    {
        (void)memset(schedule, 0, sizeof(*schedule));
        tokener = json_tokener_new();
        if (tokener == NULL)
        {
            result = SCHEDULE_RESULT_PARSE_ERROR;
        }
    }

    if (result == SCHEDULE_RESULT_OK)
    {
        json_tokener_set_flags(tokener, JSON_TOKENER_STRICT);
        root = json_tokener_parse_ex(tokener, json_text, (int)json_length);
        if ((root == NULL) || (json_tokener_get_error(tokener) != json_tokener_success) ||
            (json_tokener_get_parse_end(tokener) != json_length) ||
            !json_object_is_type(root, json_type_object))
        {
            result = SCHEDULE_RESULT_PARSE_ERROR;
        }
        else if (!schedule_parse_medications(root, schedule) ||
                 !schedule_parse_intakes(root, schedule))
        {
            result = SCHEDULE_RESULT_INVALID_CONTENT;
        }
        else
        {
            /* Content is valid. */
        }
    }

    if (root != NULL)
    {
        (void)json_object_put(root);
    }
    if (tokener != NULL)
    {
        json_tokener_free(tokener);
    }
    if ((result != SCHEDULE_RESULT_OK) && (schedule != NULL))
    {
        /* Never leave a partially filled schedule behind (Requirement 1.10). */
        (void)memset(schedule, 0, sizeof(*schedule));
    }

    return result;
}

/* Read the whole file into schedule_file_buffer; returns false on error. */
static bool schedule_read_all(int descriptor, size_t size)
{
    size_t total = 0U;
    bool valid = true;

    while ((total < size) && valid)
    {
        ssize_t count = read(descriptor, &schedule_file_buffer[total], size - total);

        if (count > 0)
        {
            total += (size_t)count;
        }
        else if ((count < 0) && (errno == EINTR))
        {
            /* Interrupted by a signal: retry. */
        }
        else
        {
            valid = false;
        }
    }

    return valid;
}

/*
 * Open the configuration file without following symbolic links, check that
 * it is a regular file owned by the current user with mode 600 (no group or
 * other permissions), then parse it.
 * Design Document, Chapter 6, Requirements 1.9, 1.10 and 4.6.
 */
schedule_result_t schedule_load(const char *path, schedule_t *schedule)
{
    schedule_result_t result = SCHEDULE_RESULT_OK;
    struct stat status;
    int descriptor = -1;

    if ((path == NULL) || (schedule == NULL))
    {
        result = SCHEDULE_RESULT_INVALID_ARGUMENT;
    }
    else
    {
        descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if ((descriptor < 0) || (fstat(descriptor, &status) != 0) || !S_ISREG(status.st_mode))
        {
            result = SCHEDULE_RESULT_FILE_ERROR;
        }
    }

    if (result == SCHEDULE_RESULT_OK)
    {
        if ((status.st_uid != geteuid()) ||
            ((status.st_mode & (mode_t)(S_IRWXG | S_IRWXO)) != 0U))
        {
            result = SCHEDULE_RESULT_PERMISSIONS;
        }
        else if ((status.st_size <= 0) || (status.st_size > (off_t)SCHEDULE_MAX_FILE_SIZE) ||
                 !schedule_read_all(descriptor, (size_t)status.st_size))
        {
            result = SCHEDULE_RESULT_FILE_ERROR;
        }
        else
        {
            schedule_file_buffer[(size_t)status.st_size] = '\0';
            result = schedule_parse(schedule_file_buffer, (size_t)status.st_size, schedule);
        }
    }

    if (descriptor >= 0)
    {
        (void)close(descriptor);
    }
    /* The buffer may contain health data: clear it. */
    (void)memset(schedule_file_buffer, 0, sizeof(schedule_file_buffer));

    return result;
}

/*
 * Copy intake times and target weights into the format sent to the ESP32.
 * Design Document, Chapter 6, Section 6.1, Requirement 1.5.
 */
schedule_result_t schedule_build_startup(const schedule_t *schedule,
                                         startup_schedule_entry_t *entries,
                                         size_t entry_capacity,
                                         startup_daily_schedule_t *startup_schedule)
{
    schedule_result_t result = SCHEDULE_RESULT_OK;

    if ((schedule == NULL) || (entries == NULL) || (startup_schedule == NULL) ||
        (schedule->intake_count == 0U))
    {
        result = SCHEDULE_RESULT_INVALID_ARGUMENT;
    }
    else if (entry_capacity < schedule->intake_count)
    {
        result = SCHEDULE_RESULT_BUFFER_TOO_SMALL;
    }
    else
    {
        size_t index;

        for (index = 0U; index < schedule->intake_count; index++)
        {
            entries[index].intake_time_seconds = schedule->intakes[index].time_seconds;
            entries[index].target_weight_milligrams =
                schedule->intakes[index].target_weight_milligrams;
        }
        startup_schedule->entries = entries;
        startup_schedule->entry_count = schedule->intake_count;
    }

    return result;
}

/* Return true if previous < time <= now, taking a wrap past midnight into account. */
static bool schedule_time_in_range(uint32_t time, uint32_t previous, uint32_t now)
{
    bool in_range;

    if (now >= previous)
    {
        in_range = (time > previous) && (time <= now);
    }
    else
    {
        in_range = (time > previous) || (time <= now);
    }

    return in_range;
}

/*
 * Report the first intake whose time lies in (previous_seconds, now_seconds].
 * Design Document, Chapter 3, Figure 5, and Chapter 6, Requirement 1.11.
 */
bool schedule_check_due(const schedule_t *schedule,
                        uint32_t previous_seconds,
                        uint32_t now_seconds,
                        size_t *intake_index)
{
    bool due = false;
    size_t index;

    if ((schedule != NULL) && (intake_index != NULL) &&
        (previous_seconds < SCHEDULE_SECONDS_PER_DAY) && (now_seconds < SCHEDULE_SECONDS_PER_DAY) &&
        (previous_seconds != now_seconds))
    {
        for (index = 0U; (index < schedule->intake_count) && !due; index++)
        {
            if (schedule_time_in_range(schedule->intakes[index].time_seconds, previous_seconds,
                                       now_seconds))
            {
                *intake_index = index;
                due = true;
            }
        }
    }

    return due;
}

/* Append a NUL-terminated string; returns false if it does not fit. */
static bool schedule_append(char *text, size_t capacity, size_t *length, const char *part)
{
    size_t part_length = strlen(part);
    bool fits = (part_length < (capacity - *length));

    if (fits)
    {
        (void)memcpy(&text[*length], part, part_length);
        *length += part_length;
        text[*length] = '\0';
    }

    return fits;
}

/* Append an unsigned decimal number; returns false if it does not fit. */
static bool schedule_append_number(char *text, size_t capacity, size_t *length, uint32_t value)
{
    static const char digit_characters[] = "0123456789";
    char digits[11];
    size_t count = 0U;
    char reversed[11];
    uint32_t remaining = value;
    size_t index;

    do
    {
        reversed[count] = digit_characters[remaining % 10U];
        count++;
        remaining /= 10U;
    } while ((remaining > 0U) && (count < (sizeof(reversed) - 1U)));

    for (index = 0U; index < count; index++)
    {
        digits[index] = reversed[count - 1U - index];
    }
    digits[count] = '\0';

    return schedule_append(text, capacity, length, digits);
}

/*
 * Build the reminder, e.g. "Erinnerung: Bitte jetzt einnehmen: 1x Aspirin,
 * 2x Vitamin D". Only names and quantities are included.
 * Design Document, Chapter 6, Requirements 1.11 and 4.5.
 */
schedule_result_t schedule_format_reminder(const schedule_t *schedule,
                                           size_t intake_index,
                                           char *text,
                                           size_t text_capacity)
{
    schedule_result_t result = SCHEDULE_RESULT_OK;
    size_t length = 0U;

    if ((schedule == NULL) || (text == NULL) || (text_capacity == 0U) ||
        (intake_index >= schedule->intake_count))
    {
        result = SCHEDULE_RESULT_INVALID_ARGUMENT;
    }
    else
    {
        const schedule_intake_t *intake = &schedule->intakes[intake_index];
        bool fits;
        size_t index;

        text[0] = '\0';
        fits = schedule_append(text, text_capacity, &length, SCHEDULE_REMINDER_PREFIX);
        for (index = 0U; (index < intake->item_count) && fits; index++)
        {
            const schedule_item_t *item = &intake->items[index];

            if (index > 0U)
            {
                fits = schedule_append(text, text_capacity, &length, ", ");
            }
            fits = fits && schedule_append_number(text, text_capacity, &length, item->quantity) &&
                   schedule_append(text, text_capacity, &length, "x ") &&
                   schedule_append(text, text_capacity, &length,
                                   schedule->medications[item->medication_index].name);
        }

        if (!fits)
        {
            text[0] = '\0';
            result = SCHEDULE_RESULT_BUFFER_TOO_SMALL;
        }
    }

    return result;
}
