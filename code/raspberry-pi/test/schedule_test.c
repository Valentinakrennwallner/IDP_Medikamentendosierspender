/* Required for mkstemp(), fchmod() and symlink() with -std=c11. */
#define _POSIX_C_SOURCE 200809L

#include "../src/schedule/schedule.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HOURS(h) ((uint32_t)(h) * 3600U)
#define MINUTES(m) ((uint32_t)(m) * 60U)

/* Valid configuration; intakes intentionally not in time order. */
static const char valid_config[] =
    "{"
    "\"ble_address\": \"00:00:00:00:00:00\","
    "\"medications\": ["
    "  {\"id\": \"med-a\", \"name\": \"Medikament A\", \"tablet_weight_g\": 0.1},"
    "  {\"id\": \"med-b\", \"name\": \"Medikament B\", \"tablet_weight_g\": 0.25}"
    "],"
    "\"intakes\": ["
    "  {\"time\": \"18:00\", \"medications\": [{\"id\": \"med-a\", \"quantity\": 1}]},"
    "  {\"time\": \"07:00\", \"medications\": [{\"id\": \"med-a\", \"quantity\": 1}]},"
    "  {\"time\": \"12:00\", \"medications\": [{\"id\": \"med-a\", \"quantity\": 2},"
    "                                       {\"id\": \"med-b\", \"quantity\": 1}]}"
    "]"
    "}";

/* Parse a NUL-terminated configuration text. */
static schedule_result_t parse_text(const char *text, schedule_t *schedule)
{
    return schedule_parse(text, strlen(text), schedule);
}

/* Requirements 1.9 and 1.10: names, tablet weights and target weights. */
static int test_valid_configuration(void)
{
    schedule_t schedule;

    if ((parse_text(valid_config, &schedule) != SCHEDULE_RESULT_OK) ||
        (schedule.medication_count != 2U) || (schedule.intake_count != 3U) ||
        (strcmp(schedule.medications[1].name, "Medikament B") != 0) ||
        (schedule.medications[0].tablet_weight_milligrams != 100U) ||
        (schedule.medications[1].tablet_weight_milligrams != 250U))
    {
        return 1;
    }
    /* Sorted by time; 12:00 is 2 x 100 mg + 1 x 250 mg. */
    if ((schedule.intakes[0].time_seconds != HOURS(7)) ||
        (schedule.intakes[1].time_seconds != HOURS(12)) ||
        (schedule.intakes[2].time_seconds != HOURS(18)) ||
        (schedule.intakes[0].target_weight_milligrams != 100U) ||
        (schedule.intakes[1].target_weight_milligrams != 450U) ||
        (schedule.intakes[1].min_tablet_weight_milligrams != 100U) ||
        (schedule.intakes[2].target_weight_milligrams != 100U) ||
        (schedule.total_weight_milligrams != 650U))
    {
        return 1;
    }
    return 0;
}

/* Requirement 1.10: invalid configurations are rejected and cleared. */
static int test_invalid_configurations(void)
{
    static const char *const invalid[] = {
        /* Not JSON, not an object, trailing characters. */
        "not json",
        "[1, 2]",
        "{\"medications\": [], \"intakes\": []} x",
        /* Missing or empty arrays. */
        "{\"intakes\": [{\"time\": \"07:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]}]}",
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0.1}]}",
        "{\"medications\": [], \"intakes\": []}",
        /* Medication errors: duplicate id, empty name, control character, bad weight. */
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0.1},"
        "{\"id\": \"a\", \"name\": \"B\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"07:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]}]}",
        "{\"medications\": [{\"id\": \"a\", \"name\": \"\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"07:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]}]}",
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\\nB\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"07:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]}]}",
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0}],"
        "\"intakes\": [{\"time\": \"07:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]}]}",
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": -0.1}],"
        "\"intakes\": [{\"time\": \"07:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]}]}",
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": \"0.1\"}],"
        "\"intakes\": [{\"time\": \"07:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]}]}",
        /* Intake item errors: unknown id, quantity 0 / 101 / 1.5, duplicate medication. */
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"07:00\", \"medications\": [{\"id\": \"b\", \"quantity\": 1}]}]}",
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"07:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 0}]}]}",
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"07:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 101}]}]}",
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"07:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 1.5}]}]}",
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"07:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 1},"
        "{\"id\": \"a\", \"quantity\": 1}]}]}",
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"07:00\", \"medications\": []}]}",
        /* Time format errors. */
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"7:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]}]}",
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"24:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]}]}",
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"12:60\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]}]}",
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"12-00\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]}]}",
        /* Window errors: overlap, same time twice, window past midnight. */
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"07:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]},"
        "{\"time\": \"07:30\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]}]}",
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"07:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]},"
        "{\"time\": \"07:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]}]}",
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"23:30\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]}]}",
        /* Daily total above the 100 g load cell capacity (60 g + 60 g). */
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 1.0}],"
        "\"intakes\": [{\"time\": \"07:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 60}]},"
        "{\"time\": \"12:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 60}]}]}",
    };
    schedule_t schedule;
    size_t index;

    for (index = 0U; index < (sizeof(invalid) / sizeof(invalid[0])); index++)
    {
        if ((parse_text(invalid[index], &schedule) == SCHEDULE_RESULT_OK) ||
            (schedule.intake_count != 0U) || (schedule.medication_count != 0U))
        {
            printf("  invalid configuration %u was accepted\n", (unsigned int)index);
            return 1;
        }
    }
    if ((schedule_parse(NULL, 1U, &schedule) == SCHEDULE_RESULT_OK) ||
        (schedule_parse(valid_config, 0U, &schedule) == SCHEDULE_RESULT_OK) ||
        (parse_text(valid_config, NULL) == SCHEDULE_RESULT_OK))
    {
        return 1;
    }
    return 0;
}

/* Requirement 1.5: times and target weights for the ESP32 start-up message. */
static int test_build_startup(void)
{
    schedule_t schedule;
    startup_schedule_entry_t entries[SCHEDULE_MAX_INTAKES];
    startup_daily_schedule_t startup;

    if ((parse_text(valid_config, &schedule) != SCHEDULE_RESULT_OK) ||
        (schedule_build_startup(&schedule, entries, SCHEDULE_MAX_INTAKES, &startup) !=
         SCHEDULE_RESULT_OK) ||
        (startup.entry_count != 3U) || (startup.entries != entries) ||
        (entries[1].intake_time_seconds != HOURS(12)) ||
        (entries[1].target_weight_milligrams != 450U) || !startup_schedule_is_valid(&startup))
    {
        return 1;
    }
    if (schedule_build_startup(&schedule, entries, 2U, &startup) !=
        SCHEDULE_RESULT_BUFFER_TOO_SMALL)
    {
        return 1;
    }
    return 0;
}

/* Requirement 1.11: each reminder is due exactly once, also across midnight. */
static int test_check_due(void)
{
    static const char midnight_config[] =
        "{\"medications\": [{\"id\": \"a\", \"name\": \"A\", \"tablet_weight_g\": 0.1}],"
        "\"intakes\": [{\"time\": \"00:00\", \"medications\": [{\"id\": \"a\", \"quantity\": 1}]}]}";
    schedule_t schedule;
    size_t intake = 99U;

    if (parse_text(valid_config, &schedule) != SCHEDULE_RESULT_OK)
    {
        return 1;
    }
    if (!schedule_check_due(&schedule, HOURS(7) - 1U, HOURS(7), &intake) || (intake != 0U) ||
        schedule_check_due(&schedule, HOURS(7), HOURS(7) + 1U, &intake) ||
        !schedule_check_due(&schedule, HOURS(11), HOURS(12) + MINUTES(5), &intake) ||
        (intake != 1U) || schedule_check_due(&schedule, HOURS(19), HOURS(19), &intake) ||
        schedule_check_due(&schedule, HOURS(19), HOURS(23), &intake))
    {
        return 1;
    }
    if ((parse_text(midnight_config, &schedule) != SCHEDULE_RESULT_OK) ||
        !schedule_check_due(&schedule, HOURS(23) + MINUTES(59), MINUTES(1), &intake) ||
        (intake != 0U))
    {
        return 1;
    }
    return 0;
}

/* Requirements 1.11 and 4.5: reminder lists only names and quantities. */
static int test_format_reminder(void)
{
    schedule_t schedule;
    char text[128];
    char small[20];

    if ((parse_text(valid_config, &schedule) != SCHEDULE_RESULT_OK) ||
        (schedule_format_reminder(&schedule, 1U, text, sizeof(text)) != SCHEDULE_RESULT_OK) ||
        (strcmp(text, "Erinnerung: Bitte jetzt einnehmen: 2x Medikament A, 1x Medikament B") !=
         0))
    {
        printf("  reminder text: \"%s\"\n", text);
        return 1;
    }
    if ((schedule_format_reminder(&schedule, 1U, small, sizeof(small)) !=
         SCHEDULE_RESULT_BUFFER_TOO_SMALL) ||
        (small[0] != '\0') ||
        (schedule_format_reminder(&schedule, 3U, text, sizeof(text)) == SCHEDULE_RESULT_OK))
    {
        return 1;
    }
    return 0;
}

/* Write the valid configuration to a new temporary file with the given mode. */
static int write_config_file(char *path, mode_t mode)
{
    int descriptor = mkstemp(path);
    size_t length = strlen(valid_config);
    int failed = 0;

    if (descriptor < 0)
    {
        return 1;
    }
    if ((write(descriptor, valid_config, length) != (ssize_t)length) ||
        (fchmod(descriptor, mode) != 0))
    {
        failed = 1;
    }
    (void)close(descriptor);
    return failed;
}

/* Requirement 4.6: only a regular file with mode 600 is accepted. */
static int test_load_file_permissions(void)
{
    char path[] = "/tmp/schedule_test_XXXXXX";
    char link_path[sizeof(path) + 5U];
    schedule_t schedule;
    int failed = 0;

    if (write_config_file(path, (mode_t)0600) != 0)
    {
        return 1;
    }
    (void)snprintf(link_path, sizeof(link_path), "%s.lnk", path);

    if (schedule_load(path, &schedule) != SCHEDULE_RESULT_OK)
    {
        failed = 1;
    }
    if ((chmod(path, (mode_t)0640) != 0) ||
        (schedule_load(path, &schedule) != SCHEDULE_RESULT_PERMISSIONS))
    {
        failed = 1;
    }
    if ((chmod(path, (mode_t)0604) != 0) ||
        (schedule_load(path, &schedule) != SCHEDULE_RESULT_PERMISSIONS))
    {
        failed = 1;
    }
    /* Symbolic links are not followed. */
    if ((chmod(path, (mode_t)0600) != 0) || (symlink(path, link_path) != 0) ||
        (schedule_load(link_path, &schedule) != SCHEDULE_RESULT_FILE_ERROR))
    {
        failed = 1;
    }
    if (schedule_load("/nonexistent/device.json", &schedule) != SCHEDULE_RESULT_FILE_ERROR)
    {
        failed = 1;
    }

    (void)unlink(link_path);
    (void)unlink(path);
    return failed;
}

/* The example configuration in the repository must be valid. */
static int test_example_configuration(void)
{
    static char text[SCHEDULE_MAX_FILE_SIZE];
    schedule_t schedule;
    FILE *file = fopen(EXAMPLE_CONFIG_PATH, "rb");
    size_t length;

    if (file == NULL)
    {
        return 1;
    }
    length = fread(text, 1U, sizeof(text), file);
    (void)fclose(file);

    if ((length == 0U) || (length >= sizeof(text)) ||
        (schedule_parse(text, length, &schedule) != SCHEDULE_RESULT_OK) ||
        (schedule.intake_count == 0U))
    {
        return 1;
    }
    return 0;
}

/* Run all schedule tests and report failures. */
int main(void)
{
    struct
    {
        const char *name;
        int (*run)(void);
    } tests[] = {
        {"valid_configuration", test_valid_configuration},
        {"invalid_configurations", test_invalid_configurations},
        {"build_startup", test_build_startup},
        {"check_due", test_check_due},
        {"format_reminder", test_format_reminder},
        {"load_file_permissions", test_load_file_permissions},
        {"example_configuration", test_example_configuration},
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
        printf("All schedule tests passed\n");
    }
    return (failures == 0) ? 0 : 1;
}
