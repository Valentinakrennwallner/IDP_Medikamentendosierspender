#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calibration.h"

/*
 * Host tests for the serial commissioning calibration: input parsing, factor
 * computation and the dialog with a scripted terminal, fake sensor and fake
 * storage. weight_calibrate() (UART driver binding) is tested on hardware.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */

#define OUTPUT_CAPACITY 8192U
#define MAXIMUM_QUEUED 8U
#define EURO_TARE 267585
#define EURO_REFERENCE (267585 + 132450) /* 7.5 g at 17660 counts per gram */

typedef struct
{
    weight_result_t result;
    int32_t counts;
} fake_sample_t;

/* Scripted terminal. */
static const char *input_script;
static size_t input_position;
static char output_text[OUTPUT_CAPACITY];
static size_t output_length;
static int write_fails;

/* Fake sensor. */
static fake_sample_t tare_queue[MAXIMUM_QUEUED];
static unsigned int tare_count;
static unsigned int tare_calls;
static fake_sample_t raw_queue[MAXIMUM_QUEUED];
static unsigned int raw_count;
static unsigned int raw_calls;
static weight_measure_status_t measure_status;
static float measure_grams;

/* Fake storage. */
static persistence_result_t init_result;
static persistence_result_t erase_result;
static unsigned int erase_calls;
static persistence_result_t save_result;
static unsigned int save_calls;
static weight_calibration_t saved_calibration;
static persistence_result_t load_result;
static int load_changes_tare;

/* Report a failed check with its line number and return 1. */
static int fail(int line)
{
    (void)fprintf(stderr, "calibration_test: check failed at line %d\n", line);
    (void)fprintf(stderr, "dialog output:\n%s\n", output_text);
    return 1;
}

#define CHECK(condition)           \
    do                             \
    {                              \
        if (!(condition))          \
        {                          \
            return fail(__LINE__); \
        }                          \
    } while (0)

static bool fake_read_byte(uint8_t *byte)
{
    if (input_script[input_position] == '\0')
    {
        return false; /* script exhausted: behaves like a read error */
    }
    *byte = (uint8_t)input_script[input_position];
    input_position++;
    return true;
}

static bool fake_write_text(const char *text)
{
    size_t length = strlen(text);

    if (write_fails != 0)
    {
        return false;
    }
    if ((output_length + length) < OUTPUT_CAPACITY)
    {
        (void)memcpy(&output_text[output_length], text, length);
        output_length += length;
        output_text[output_length] = '\0';
    }
    return true;
}

static persistence_result_t fake_storage_init(void)
{
    return init_result;
}

static persistence_result_t fake_storage_erase_and_init(void)
{
    erase_calls++;
    return erase_result;
}

/* Take the next queued sample; an empty queue is a sensor fault. */
static weight_result_t take_sample(fake_sample_t *queue, unsigned int count,
                                   unsigned int *calls, int32_t *counts)
{
    weight_result_t result = WEIGHT_RESULT_SENSOR_FAULT;

    if (*calls < count)
    {
        result = queue[*calls].result;
        if (result == WEIGHT_RESULT_OK)
        {
            *counts = queue[*calls].counts;
        }
    }
    (*calls)++;
    return result;
}

static weight_result_t fake_measure_tare(int32_t *counts)
{
    return take_sample(tare_queue, tare_count, &tare_calls, counts);
}

static weight_result_t fake_measure_raw_mean(int32_t *counts)
{
    return take_sample(raw_queue, raw_count, &raw_calls, counts);
}

static persistence_result_t fake_save(const weight_calibration_t *calibration)
{
    save_calls++;
    if (save_result == PERSISTENCE_OK)
    {
        saved_calibration = *calibration;
    }
    return save_result;
}

/* Return the saved calibration with the 0.001 count/g storage rounding. */
static persistence_result_t fake_load(weight_calibration_t *calibration)
{
    if (load_result != PERSISTENCE_OK)
    {
        return load_result;
    }
    calibration->tare_offset_counts =
        saved_calibration.tare_offset_counts + ((load_changes_tare != 0) ? 1 : 0);
    calibration->counts_per_gram =
        (float)(round((double)saved_calibration.counts_per_gram * 1000.0) / 1000.0);
    return PERSISTENCE_OK;
}

static weight_measure_status_t fake_measure(const weight_calibration_t *calibration,
                                            weight_measurement_t *measurement)
{
    (void)calibration;
    if (measure_status == WEIGHT_MEASURE_OK)
    {
        measurement->grams = measure_grams;
    }
    return measure_status;
}

static const calibration_io_t fake_io = {
    fake_read_byte,
    fake_write_text,
    fake_storage_init,
    fake_storage_erase_and_init,
    fake_measure_tare,
    fake_measure_raw_mean,
    fake_save,
    fake_load,
    fake_measure,
};

/* Default scenario: 1-euro coin, everything works. */
static void reset_fakes(const char *script)
{
    input_script = script;
    input_position = 0U;
    output_text[0] = '\0';
    output_length = 0U;
    write_fails = 0;

    tare_queue[0].result = WEIGHT_RESULT_OK;
    tare_queue[0].counts = EURO_TARE;
    tare_count = 1U;
    tare_calls = 0U;
    raw_queue[0].result = WEIGHT_RESULT_OK;
    raw_queue[0].counts = EURO_REFERENCE;
    raw_count = 1U;
    raw_calls = 0U;
    measure_status = WEIGHT_MEASURE_OK;
    measure_grams = 7.49F;

    init_result = PERSISTENCE_OK;
    erase_result = PERSISTENCE_OK;
    erase_calls = 0U;
    save_result = PERSISTENCE_OK;
    save_calls = 0U;
    saved_calibration.tare_offset_counts = 0;
    saved_calibration.counts_per_gram = 0.0F;
    load_result = PERSISTENCE_OK;
    load_changes_tare = 0;
}

/* Count the occurrences of text in the dialog output. */
static unsigned int count_output(const char *text)
{
    unsigned int count = 0U;
    const char *position = strstr(output_text, text);

    while (position != NULL)
    {
        count++;
        position = strstr(position + 1, text);
    }
    return count;
}

/* Grams typed by the user: decimal point or comma, up to three decimals. */
static int test_parse_valid(void)
{
    uint32_t milligrams = 0U;

    CHECK(calibration_parse_grams("7.5", &milligrams) && (milligrams == 7500U));
    CHECK(calibration_parse_grams("7,5", &milligrams) && (milligrams == 7500U));
    CHECK(calibration_parse_grams("20", &milligrams) && (milligrams == 20000U));
    CHECK(calibration_parse_grams("0.001", &milligrams) && (milligrams == 1U));
    CHECK(calibration_parse_grams("100.000", &milligrams) && (milligrams == 100000U));
    CHECK(calibration_parse_grams("007.49", &milligrams) && (milligrams == 7490U));
    CHECK(calibration_parse_grams("999999.999", &milligrams) &&
          (milligrams == 999999999U));
    return 0;
}

/* Malformed input is rejected and the output stays unchanged. */
static int test_parse_invalid(void)
{
    static const char *const invalid[] = {"", ".5", "7.", "7.5.1", "7,1234", "abc", "-5",
                                          " 7", "7 ", "1234567", "7.5g", "q"};
    uint32_t milligrams = 42U;
    size_t index;

    for (index = 0U; index < (sizeof(invalid) / sizeof(invalid[0])); index++)
    {
        CHECK(!calibration_parse_grams(invalid[index], &milligrams));
    }
    CHECK(milligrams == 42U);
    CHECK(!calibration_parse_grams(NULL, &milligrams));
    CHECK(!calibration_parse_grams("7.5", NULL));
    return 0;
}

/* Requirement 2.8: 1-euro coin with the values of the assembled load cell. */
static int test_compute_euro_coin(void)
{
    float factor = 0.0F;

    CHECK(calibration_compute_counts_per_gram(EURO_TARE, EURO_REFERENCE, 7500U, &factor));
    CHECK(factor == 17660.0F);
    CHECK(calibration_compute_counts_per_gram(EURO_REFERENCE, EURO_TARE, 7500U, &factor));
    CHECK(factor == -17660.0F);
    CHECK(calibration_compute_counts_per_gram(0, 35320, 2000U, &factor));
    CHECK(factor == 17660.0F);
    return 0;
}

/* Reference weight from 1.000 g to 100.000 g. */
static int test_compute_reference_limits(void)
{
    float factor = 0.0F;

    CHECK(calibration_compute_counts_per_gram(0, 17660, 1000U, &factor));
    CHECK(calibration_compute_counts_per_gram(0, 1766000, 100000U, &factor));
    CHECK(fabsf(factor - 17660.0F) < 0.001F);
    factor = 42.0F;
    CHECK(!calibration_compute_counts_per_gram(0, 17660, 999U, &factor));
    CHECK(!calibration_compute_counts_per_gram(0, 17660, 100001U, &factor));
    CHECK(!calibration_compute_counts_per_gram(0, 17660, 0U, &factor));
    CHECK(factor == 42.0F);
    return 0;
}

/* No difference, invalid raw values, overflow and a missing output. */
static int test_compute_invalid(void)
{
    float factor = 42.0F;

    CHECK(!calibration_compute_counts_per_gram(EURO_TARE, EURO_TARE, 7500U, &factor));
    CHECK(!calibration_compute_counts_per_gram(8388607, 0, 7500U, &factor));
    CHECK(!calibration_compute_counts_per_gram(-8388608, 0, 7500U, &factor));
    CHECK(!calibration_compute_counts_per_gram(0, 8388607, 7500U, &factor));
    CHECK(!calibration_compute_counts_per_gram(0, -8388608, 7500U, &factor));
    /* 16 777 213 counts per gram do not fit as counts per kilogram. */
    CHECK(!calibration_compute_counts_per_gram(-8388607, 8388606, 1000U, &factor));
    CHECK(factor == 42.0F);
    CHECK(calibration_compute_counts_per_gram(-8388607, 8388606, 10000U, &factor));
    CHECK(!calibration_compute_counts_per_gram(0, 17660, 7500U, NULL));
    return 0;
}

/* Requirement 2.8: full dialog, saved, reloaded, control measurement shown. */
static int test_dialog_success(void)
{
    reset_fakes("\r7.5\ry\r");
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_OK);
    CHECK((save_calls == 1U) && (tare_calls == 1U) && (raw_calls == 1U));
    CHECK(saved_calibration.tare_offset_counts == EURO_TARE);
    CHECK(saved_calibration.counts_per_gram == 17660.0F);
    CHECK(count_output("Calibration factor: 17660.000 counts per gram.") == 1U);
    CHECK(count_output("Control measurement: 7.490 g") == 1U);
    /* No tare or raw values in the output. */
    CHECK(count_output("267585") == 0U);
    CHECK(count_output("400035") == 0U);
    return 0;
}

/* q (or Q) at every prompt cancels without saving. */
static int test_dialog_cancel_everywhere(void)
{
    static const char *const scripts[] = {"q\r", "Q\r", "\rq\r", "\r7.5\rq\r",
                                          "\r0.2\rq\r", "\r7.5\rn\rq\r"};
    size_t index;

    for (index = 0U; index < (sizeof(scripts) / sizeof(scripts[0])); index++)
    {
        reset_fakes(scripts[index]);
        CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_CANCELLED);
        CHECK(save_calls == 0U);
        CHECK(count_output("Calibration cancelled. Nothing saved.") == 1U);
    }
    reset_fakes("q\r");
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_CANCELLED);
    CHECK(tare_calls == 0U);
    return 0;
}

/* Out-of-range, malformed and too long weights are asked again. */
static int test_dialog_invalid_weight_reprompts(void)
{
    reset_fakes("\r0.5\r200\rabc\r12345678901234567\r7.5\ry\r");
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_OK);
    CHECK(count_output("Invalid weight.") == 4U);
    CHECK(saved_calibration.counts_per_gram == 17660.0F);
    return 0;
}

/* Echo, backspace, and CR, LF or CR LF as line end. */
static int test_dialog_line_editing(void)
{
    reset_fakes("\r7.6\b5\ry\r");
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_OK);
    CHECK(count_output("\b \b") == 1U);
    CHECK(saved_calibration.counts_per_gram == 17660.0F);

    reset_fakes("\x7F\r7.5\x7F\x7F,5\ry\r");
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_OK);
    CHECK(saved_calibration.counts_per_gram == 17660.0F);

    reset_fakes("\r\n7.5\r\ny\r\n");
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_OK);

    reset_fakes("\n7.5\ny\n");
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_OK);

    reset_fakes("\r7.5\x01\ry\r");
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_OK);
    return 0;
}

/* Requirement 2.2: a sensor fault can be retried with Enter or cancelled. */
static int test_dialog_sensor_fault_retry(void)
{
    reset_fakes("\r\r7.5\ry\r");
    tare_queue[0].result = WEIGHT_RESULT_TIMEOUT;
    tare_queue[1].result = WEIGHT_RESULT_OK;
    tare_queue[1].counts = EURO_TARE;
    tare_count = 2U;
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_OK);
    CHECK((tare_calls == 2U) && (count_output("Sensor fault while measuring.") == 1U));

    reset_fakes("\r7.5\r\ry\r");
    raw_queue[0].result = WEIGHT_RESULT_SENSOR_FAULT;
    raw_queue[1].result = WEIGHT_RESULT_OK;
    raw_queue[1].counts = EURO_REFERENCE;
    raw_count = 2U;
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_OK);
    CHECK(raw_calls == 2U);

    reset_fakes("\rq\r");
    tare_queue[0].result = WEIGHT_RESULT_SENSOR_FAULT;
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_CANCELLED);
    CHECK(save_calls == 0U);
    return 0;
}

/* No reference weight detected: no valid factor, nothing saved. */
static int test_dialog_invalid_factor(void)
{
    reset_fakes("\r7.5\ry\r");
    raw_queue[0].counts = EURO_TARE;
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_INVALID_FACTOR);
    CHECK(save_calls == 0U);
    CHECK(count_output("Nothing saved.") == 1U);
    return 0;
}

/* Anything but y or q at the confirmation is asked again. */
static int test_dialog_confirmation(void)
{
    reset_fakes("\r7.5\rn\ryes\rY\r");
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_OK);
    CHECK(count_output("Enter y or q: ") == 2U);
    CHECK(save_calls == 1U);
    return 0;
}

/* Full or outdated NVS: erase only after typing ERASE, never automatically. */
static int test_dialog_storage_erase(void)
{
    reset_fakes("ERASE\r\r7.5\ry\r");
    init_result = PERSISTENCE_STORAGE_ERROR;
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_OK);
    CHECK(erase_calls == 1U);
    CHECK(count_output("pre-shared key") == 1U);

    reset_fakes("erase\r");
    init_result = PERSISTENCE_STORAGE_ERROR;
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_CANCELLED);
    CHECK((erase_calls == 0U) && (save_calls == 0U));

    reset_fakes("q\r");
    init_result = PERSISTENCE_STORAGE_ERROR;
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_CANCELLED);
    CHECK(erase_calls == 0U);

    reset_fakes("ERASE\r");
    init_result = PERSISTENCE_STORAGE_ERROR;
    erase_result = PERSISTENCE_STORAGE_ERROR;
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_STORAGE_ERROR);
    CHECK(tare_calls == 0U);

    reset_fakes("\r7.5\ry\r");
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_OK);
    CHECK(erase_calls == 0U);
    return 0;
}

/* Save, reload and control measurement failures. */
static int test_dialog_save_and_verify_errors(void)
{
    reset_fakes("\r7.5\ry\r");
    save_result = PERSISTENCE_STORAGE_ERROR;
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_STORAGE_ERROR);

    reset_fakes("\r7.5\ry\r");
    save_result = PERSISTENCE_INVALID_VALUE;
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_INVALID_FACTOR);

    reset_fakes("\r7.5\ry\r");
    load_result = PERSISTENCE_INVALID_CHECKSUM;
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_VERIFY_FAILED);

    reset_fakes("\r7.5\ry\r");
    load_changes_tare = 1;
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_VERIFY_FAILED);

    reset_fakes("\r7.5\ry\r");
    measure_status = WEIGHT_MEASURE_SENSOR_FAULT;
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_SENSOR_FAULT);
    CHECK(save_calls == 1U);
    CHECK(count_output("control measurement failed") == 1U);
    return 0;
}

/* Serial read or write errors end the dialog without saving. */
static int test_dialog_io_errors(void)
{
    reset_fakes("\r7.5\r");
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_IO_ERROR);
    CHECK(save_calls == 0U);

    reset_fakes("\r7.5\ry\r");
    write_fails = 1;
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_IO_ERROR);
    CHECK((save_calls == 0U) && (tare_calls == 0U));
    return 0;
}

/* A missing or incomplete I/O description is rejected. */
static int test_dialog_invalid_io(void)
{
    calibration_io_t incomplete = fake_io;

    reset_fakes("");
    CHECK(calibration_dialog_run(NULL) == CALIBRATION_INVALID_ARGUMENT);
    incomplete.save = NULL;
    CHECK(calibration_dialog_run(&incomplete) == CALIBRATION_INVALID_ARGUMENT);
    CHECK(output_length == 0U);
    return 0;
}

/* Small factors and negative weights are formatted with three decimals. */
static int test_dialog_number_format(void)
{
    reset_fakes("\r100\ry\r");
    raw_queue[0].counts = EURO_TARE + 7;
    measure_grams = -0.004F;
    CHECK(calibration_dialog_run(&fake_io) == CALIBRATION_OK);
    CHECK(count_output("Calibration factor: 0.070 counts per gram.") == 1U);
    CHECK(count_output("Control measurement: -0.004 g") == 1U);
    return 0;
}

/* Run the calibration tests without ESP-IDF or hardware. */
int main(void)
{
    if ((test_parse_valid() != 0) || (test_parse_invalid() != 0) ||
        (test_compute_euro_coin() != 0) || (test_compute_reference_limits() != 0) ||
        (test_compute_invalid() != 0) || (test_dialog_success() != 0) ||
        (test_dialog_cancel_everywhere() != 0) ||
        (test_dialog_invalid_weight_reprompts() != 0) ||
        (test_dialog_line_editing() != 0) || (test_dialog_sensor_fault_retry() != 0) ||
        (test_dialog_invalid_factor() != 0) || (test_dialog_confirmation() != 0) ||
        (test_dialog_storage_erase() != 0) ||
        (test_dialog_save_and_verify_errors() != 0) || (test_dialog_io_errors() != 0) ||
        (test_dialog_invalid_io() != 0) || (test_dialog_number_format() != 0))
    {
        (void)fputs("calibration tests failed\n", stderr);
        return EXIT_FAILURE;
    }

    (void)puts("calibration tests passed");
    return EXIT_SUCCESS;
}
