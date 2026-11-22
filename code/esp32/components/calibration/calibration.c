/*
 * Serial commissioning calibration of the weight measurement: input parsing,
 * factor computation and the dialog. All hardware access goes through
 * calibration_io_t, so this file is host-testable; the UART, HX711 and NVS
 * binding is in calibration_uart.c.
 * Logically part of the module "Weight measurement" (Design Document,
 * Chapter 2, Section 2.1, weight_calibrate()).
 * Design Document, Chapter 2, Sections 2.1 and 2.2, and Chapter 6,
 * Section 6.1, Requirement 2.8.
 *
 * The dialog shows the computed factor and the control weight (the reference
 * weight in grams). No tare or raw values are shown. During commissioning
 * only the reference weight lies on the tray, so no health data is output
 * (Chapter 5, Section 5.1, Threat 3, and Chapter 6, Section 6.2, Requirement 4.4).
 */
#include "calibration.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define MILLIGRAMS_PER_GRAM 1000U
#define MILLI_SCALE 1000.0
#define MAXIMUM_INTEGER_DIGITS 6U
#define MAXIMUM_DECIMAL_DIGITS 3U
#define INT32_MAXIMUM_AS_DOUBLE 2147483647.0
#define FLOAT_RELATIVE_TOLERANCE 2.4e-7
#define STORAGE_RESOLUTION_HALF 0.0005
#define NUMBER_TEXT_SIZE 24U
#define NUMBER_DIGITS_SIZE 20U
#define MINIMUM_FORMATTED_DIGITS 4U

#define ASCII_BACKSPACE 0x08U
#define ASCII_DELETE 0x7FU
#define ASCII_LINE_FEED 0x0AU
#define ASCII_CARRIAGE_RETURN 0x0DU
#define ASCII_FIRST_PRINTABLE 0x20U
#define ASCII_LAST_PRINTABLE 0x7EU

static const char digit_characters[] = "0123456789";

typedef enum
{
    INPUT_LINE = 0,
    INPUT_CANCEL = 1,
    INPUT_IO_ERROR = 2
} input_t;

typedef struct
{
    const calibration_io_t *io;
    char line[CALIBRATION_MAXIMUM_LINE_LENGTH + 1U];
    uint32_t line_length;
    bool line_too_long;
    bool skip_line_feed;
    bool io_failed;
} dialog_t;

/*
 * Return the value of a decimal digit character, or -1 for any other
 * character (table lookup, no character arithmetic).
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static int32_t digit_value(char character)
{
    int32_t value = -1;
    uint32_t index;

    for (index = 0U; index < 10U; ++index)
    {
        if (digit_characters[index] == character)
        {
            value = (int32_t)index;
        }
    }
    return value;
}

/*
 * Parse "<digits>[(.|,)<1 to 3 digits>]" into milligrams.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
bool calibration_parse_grams(const char *text, uint32_t *milligrams)
{
    uint32_t integer_part = 0U;
    uint32_t decimal_part = 0U;
    uint32_t integer_digits = 0U;
    uint32_t decimal_digits = 0U;
    uint32_t index = 0U;
    bool after_separator = false;
    bool valid = true;

    if ((text == NULL) || (milligrams == NULL))
    {
        return false;
    }

    while (valid && (text[index] != '\0'))
    {
        int32_t digit = digit_value(text[index]);

        if (digit >= 0)
        {
            if (!after_separator && (integer_digits < MAXIMUM_INTEGER_DIGITS))
            {
                integer_part = (integer_part * 10U) + (uint32_t)digit;
                integer_digits++;
            }
            else if (after_separator && (decimal_digits < MAXIMUM_DECIMAL_DIGITS))
            {
                decimal_part = (decimal_part * 10U) + (uint32_t)digit;
                decimal_digits++;
            }
            else
            {
                valid = false;
            }
        }
        else if (((text[index] == '.') || (text[index] == ',')) && !after_separator)
        {
            after_separator = true;
        }
        else
        {
            valid = false;
        }
        index++;
    }

    if ((integer_digits == 0U) || (after_separator && (decimal_digits == 0U)))
    {
        valid = false;
    }

    if (valid)
    {
        while (decimal_digits < MAXIMUM_DECIMAL_DIGITS)
        {
            decimal_part *= 10U;
            decimal_digits++;
        }
        *milligrams = (integer_part * MILLIGRAMS_PER_GRAM) + decimal_part;
    }
    return valid;
}

/*
 * Accept only raw values inside the signed 24-bit HX711 range without the
 * two limit values that mark a faulty conversion.
 * Design Document, Chapter 6, Section 6.1, Requirements 2.2 and 2.8.
 */
static bool raw_is_valid(int32_t raw_counts)
{
    return (raw_counts >= PERSISTENCE_TARE_MINIMUM_COUNTS) &&
           (raw_counts <= PERSISTENCE_TARE_MAXIMUM_COUNTS);
}

/*
 * counts_per_gram = (reference - tare) / reference weight. The difference
 * is formed in 64 bit, the division in double. The factor must be storable
 * by persistence_calibration_save() (counts per kilogram fit into int32_t).
 * Design Document, Chapter 2, Section 2.1, and Chapter 6, Section 6.1,
 * Requirement 2.8.
 */
bool calibration_compute_counts_per_gram(int32_t tare_counts,
                                         int32_t reference_counts,
                                         uint32_t reference_milligrams,
                                         float *counts_per_gram)
{
    bool valid = false;

    if ((counts_per_gram != NULL) &&
        (reference_milligrams >= CALIBRATION_MINIMUM_REFERENCE_MILLIGRAMS) &&
        (reference_milligrams <= CALIBRATION_MAXIMUM_REFERENCE_MILLIGRAMS) &&
        raw_is_valid(tare_counts) && raw_is_valid(reference_counts))
    {
        int64_t difference_counts = (int64_t)reference_counts - (int64_t)tare_counts;

        if (difference_counts != 0)
        {
            double factor = ((double)difference_counts * MILLI_SCALE) /
                            (double)reference_milligrams;

            if (fabs(factor * MILLI_SCALE) <= INT32_MAXIMUM_AS_DOUBLE)
            {
                *counts_per_gram = (float)factor;
                valid = true;
            }
        }
    }
    return valid;
}

/*
 * Send a text; after the first write error nothing more is sent and the
 * next input request reports the I/O error.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static void say(dialog_t *dialog, const char *text)
{
    if (!dialog->io_failed && !dialog->io->write_text(text))
    {
        dialog->io_failed = true;
    }
}

/*
 * Send a value given in thousandths with exactly three decimals, for
 * example 7490 as "7.490".
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static void say_milli(dialog_t *dialog, int64_t milli)
{
    char text[NUMBER_TEXT_SIZE];
    char digits[NUMBER_DIGITS_SIZE];
    uint64_t magnitude;
    uint32_t digit_count = 0U;
    uint32_t position = 0U;

    if (milli < 0)
    {
        text[position] = '-';
        position++;
        magnitude = (uint64_t)(-(milli + 1)) + 1U;
    }
    else
    {
        magnitude = (uint64_t)milli;
    }

    while (((magnitude > 0U) || (digit_count < MINIMUM_FORMATTED_DIGITS)) &&
           (digit_count < NUMBER_DIGITS_SIZE))
    {
        digits[digit_count] = digit_characters[magnitude % 10U];
        magnitude /= 10U;
        digit_count++;
    }

    while (digit_count > 0U)
    {
        digit_count--;
        text[position] = digits[digit_count];
        position++;
        if (digit_count == MAXIMUM_DECIMAL_DIGITS)
        {
            text[position] = '.';
            position++;
        }
    }
    text[position] = '\0';
    say(dialog, text);
}

/*
 * Round a float to thousandths for the output.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static int64_t to_milli(float value)
{
    double scaled = (double)value * MILLI_SCALE;

    if (scaled >= 0.0)
    {
        scaled = floor(scaled + 0.5);
    }
    else
    {
        scaled = ceil(scaled - 0.5);
    }
    return (int64_t)scaled;
}

/*
 * Echo one printable character back to the terminal.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static void echo_character(dialog_t *dialog, char character)
{
    char text[2];

    text[0] = character;
    text[1] = '\0';
    say(dialog, text);
}

/*
 * Read one line with echo and backspace. CR, LF and CR LF all end a line.
 * Lines longer than CALIBRATION_MAXIMUM_LINE_LENGTH are marked as too long
 * and later rejected. Returns false on an I/O error.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static bool read_line(dialog_t *dialog)
{
    bool line_complete = false;
    uint8_t byte = 0U;

    dialog->line_length = 0U;
    dialog->line_too_long = false;

    while (!line_complete && !dialog->io_failed)
    {
        if (!dialog->io->read_byte(&byte))
        {
            dialog->io_failed = true;
        }
        else if ((byte == ASCII_LINE_FEED) && dialog->skip_line_feed)
        {
            dialog->skip_line_feed = false;
        }
        else
        {
            dialog->skip_line_feed = false;
            if ((byte == ASCII_CARRIAGE_RETURN) || (byte == ASCII_LINE_FEED))
            {
                dialog->skip_line_feed = (byte == ASCII_CARRIAGE_RETURN);
                say(dialog, "\r\n");
                line_complete = true;
            }
            else if ((byte == ASCII_BACKSPACE) || (byte == ASCII_DELETE))
            {
                if (dialog->line_length > 0U)
                {
                    dialog->line_length--;
                    say(dialog, "\b \b");
                }
            }
            else if ((byte >= ASCII_FIRST_PRINTABLE) && (byte <= ASCII_LAST_PRINTABLE))
            {
                if (dialog->line_length < CALIBRATION_MAXIMUM_LINE_LENGTH)
                {
                    dialog->line[dialog->line_length] = (char)byte;
                    dialog->line_length++;
                    echo_character(dialog, (char)byte);
                }
                else
                {
                    dialog->line_too_long = true;
                }
            }
            else
            {
                /* Other control characters are ignored. */
            }
        }
    }

    dialog->line[dialog->line_length] = '\0';
    return line_complete;
}

/*
 * True if the complete line equals word.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static bool line_is(const dialog_t *dialog, const char *word)
{
    return !dialog->line_too_long && (strcmp(dialog->line, word) == 0);
}

/*
 * Read one input line and classify it: q or Q cancels everywhere.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static input_t read_input(dialog_t *dialog)
{
    input_t input = INPUT_IO_ERROR;

    if (!dialog->io_failed && read_line(dialog))
    {
        input = (line_is(dialog, "q") || line_is(dialog, "Q")) ? INPUT_CANCEL : INPUT_LINE;
    }
    return input;
}

/*
 * Map a non-line input to the dialog result.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static calibration_result_t input_result(input_t input)
{
    return (input == INPUT_CANCEL) ? CALIBRATION_CANCELLED : CALIBRATION_IO_ERROR;
}

/*
 * Initialise NVS. If it is full or outdated, offer to erase the whole
 * partition, but only after the user has typed ERASE.
 * Design Document, Chapter 2, Section 2.2, and Chapter 6, Section 6.1,
 * Requirement 2.8.
 */
static calibration_result_t prepare_storage(dialog_t *dialog)
{
    calibration_result_t result = CALIBRATION_OK;

    if (dialog->io->storage_init() != PERSISTENCE_OK)
    {
        say(dialog, "NVS cannot be used (partition full or outdated).\r\n"
                    "Erasing deletes the calibration AND the pre-shared key.\r\n"
                    "Type ERASE to erase the NVS partition, anything else cancels: ");
        input_t input = read_input(dialog);

        if (input != INPUT_LINE)
        {
            result = input_result(input);
        }
        else if (!line_is(dialog, "ERASE"))
        {
            result = CALIBRATION_CANCELLED;
        }
        else if (dialog->io->storage_erase_and_init() != PERSISTENCE_OK)
        {
            say(dialog, "Erasing the NVS partition failed.\r\n");
            result = CALIBRATION_STORAGE_ERROR;
        }
        else
        {
            say(dialog, "NVS partition erased.\r\n");
        }
    }
    return result;
}

/*
 * Wait for Enter, then take a raw mean; on a sensor fault offer to retry.
 * Design Document, Chapter 6, Section 6.1, Requirements 2.2 and 2.8.
 */
static calibration_result_t measure_after_enter(dialog_t *dialog,
                                                const char *prompt,
                                                weight_result_t (*measure)(int32_t *),
                                                int32_t *counts)
{
    calibration_result_t result = CALIBRATION_OK;
    bool done = false;

    say(dialog, prompt);
    while (!done)
    {
        input_t input = read_input(dialog);

        if (input != INPUT_LINE)
        {
            result = input_result(input);
            done = true;
        }
        else if (measure(counts) == WEIGHT_RESULT_OK)
        {
            done = true;
        }
        else
        {
            say(dialog, "Sensor fault while measuring. Press Enter to retry or q to cancel: ");
        }
    }
    return result;
}

/*
 * Ask for the reference weight until a value from 1.000 g to 100.000 g is
 * entered.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static calibration_result_t ask_reference_weight(dialog_t *dialog, uint32_t *milligrams)
{
    calibration_result_t result = CALIBRATION_OK;
    bool done = false;

    say(dialog, "Step 2/3: Place the reference weight on the tray (1 g to 100 g,\r\n"
                "at least about 5 g recommended) and enter its weight in grams, e.g. 7.5: ");
    while (!done)
    {
        input_t input = read_input(dialog);

        if (input != INPUT_LINE)
        {
            result = input_result(input);
            done = true;
        }
        else if (!dialog->line_too_long && calibration_parse_grams(dialog->line, milligrams) &&
                 (*milligrams >= CALIBRATION_MINIMUM_REFERENCE_MILLIGRAMS) &&
                 (*milligrams <= CALIBRATION_MAXIMUM_REFERENCE_MILLIGRAMS))
        {
            done = true;
        }
        else
        {
            say(dialog, "Invalid weight. Enter 1 to 100 g with at most 3 decimals, or q: ");
        }
    }
    return result;
}

/*
 * Show the factor and ask whether to save it (y saves, q cancels).
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static calibration_result_t confirm_save(dialog_t *dialog, float counts_per_gram)
{
    calibration_result_t result = CALIBRATION_OK;
    bool done = false;

    say(dialog, "Calibration factor: ");
    say_milli(dialog, to_milli(counts_per_gram));
    say(dialog, " counts per gram.\r\nStep 3/3: Enter y to save this calibration or q to cancel: ");
    while (!done)
    {
        input_t input = read_input(dialog);

        if (input != INPUT_LINE)
        {
            result = input_result(input);
            done = true;
        }
        else if (line_is(dialog, "y") || line_is(dialog, "Y"))
        {
            done = true;
        }
        else
        {
            say(dialog, "Enter y or q: ");
        }
    }
    return result;
}

/*
 * True if the reloaded calibration matches the saved one: same tare, and a
 * factor within the storage resolution (0.001 count per gram) plus float
 * rounding.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static bool calibration_matches(const weight_calibration_t *saved,
                                const weight_calibration_t *loaded)
{
    double difference = fabs((double)saved->counts_per_gram - (double)loaded->counts_per_gram);
    double tolerance = STORAGE_RESOLUTION_HALF +
                       (fabs((double)saved->counts_per_gram) * FLOAT_RELATIVE_TOLERANCE);

    return (saved->tare_offset_counts == loaded->tare_offset_counts) &&
           (difference <= tolerance);
}

/*
 * Save, reload and compare the calibration, then take one control
 * measurement with the reloaded values.
 * Design Document, Chapter 2, Section 2.2, and Chapter 6, Section 6.1,
 * Requirement 2.8.
 */
static calibration_result_t save_and_verify(dialog_t *dialog,
                                            const weight_calibration_t *calibration)
{
    weight_calibration_t loaded = {0, 0.0F};
    weight_measurement_t measurement = {0, 0.0F};
    calibration_result_t result = CALIBRATION_OK;
    persistence_result_t stored = dialog->io->save(calibration);

    if (stored == PERSISTENCE_INVALID_VALUE)
    {
        say(dialog, "The calibration factor cannot be stored. Nothing saved.\r\n");
        result = CALIBRATION_INVALID_FACTOR;
    }
    else if (stored != PERSISTENCE_OK)
    {
        say(dialog, "Saving the calibration failed.\r\n");
        result = CALIBRATION_STORAGE_ERROR;
    }
    else if ((dialog->io->load(&loaded) != PERSISTENCE_OK) ||
             !calibration_matches(calibration, &loaded))
    {
        say(dialog, "The saved calibration could not be read back unchanged.\r\n");
        result = CALIBRATION_VERIFY_FAILED;
    }
    else if (dialog->io->measure(&loaded, &measurement) != WEIGHT_MEASURE_OK)
    {
        say(dialog, "Calibration saved, but the control measurement failed (sensor fault).\r\n");
        result = CALIBRATION_SENSOR_FAULT;
    }
    else
    {
        say(dialog, "Calibration saved. Control measurement: ");
        say_milli(dialog, to_milli(measurement.grams));
        say(dialog, " g\r\n");
    }
    return result;
}

/*
 * True if every function of the I/O description is set.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static bool io_is_complete(const calibration_io_t *io)
{
    return (io != NULL) && (io->read_byte != NULL) && (io->write_text != NULL) &&
           (io->storage_init != NULL) && (io->storage_erase_and_init != NULL) &&
           (io->measure_tare != NULL) && (io->measure_raw_mean != NULL) &&
           (io->save != NULL) && (io->load != NULL) && (io->measure != NULL);
}

/*
 * The commissioning dialog: storage, tare, reference weight, factor,
 * confirmation, save, verification. Nothing is saved before the user has
 * confirmed with y.
 * Design Document, Chapter 2, Sections 2.1 and 2.2, and Chapter 6,
 * Section 6.1, Requirement 2.8.
 */
calibration_result_t calibration_dialog_run(const calibration_io_t *io)
{
    dialog_t dialog;
    weight_calibration_t calibration = {0, 0.0F};
    int32_t reference_counts = 0;
    uint32_t reference_milligrams = 0U;
    calibration_result_t result;

    if (!io_is_complete(io))
    {
        return CALIBRATION_INVALID_ARGUMENT;
    }

    (void)memset(&dialog, 0, sizeof(dialog));
    dialog.io = io;

    say(&dialog, "\r\n=== Weight calibration (commissioning) ===\r\n"
                 "Enter q at any prompt to cancel without saving.\r\n");

    result = prepare_storage(&dialog);
    if (result == CALIBRATION_OK)
    {
        result = measure_after_enter(&dialog,
                                     "Step 1/3: Empty the tray completely, then press Enter: ",
                                     io->measure_tare, &calibration.tare_offset_counts);
    }
    if (result == CALIBRATION_OK)
    {
        result = ask_reference_weight(&dialog, &reference_milligrams);
    }
    if (result == CALIBRATION_OK)
    {
        if (io->measure_raw_mean(&reference_counts) != WEIGHT_RESULT_OK)
        {
            result = measure_after_enter(&dialog,
                                         "Sensor fault while measuring. "
                                         "Press Enter to retry or q to cancel: ",
                                         io->measure_raw_mean, &reference_counts);
        }
    }
    if (result == CALIBRATION_OK)
    {
        if (!calibration_compute_counts_per_gram(calibration.tare_offset_counts,
                                                 reference_counts, reference_milligrams,
                                                 &calibration.counts_per_gram))
        {
            say(&dialog, "The measured values give no valid calibration factor\r\n"
                         "(reference weight missing or too light?). Nothing saved.\r\n");
            result = CALIBRATION_INVALID_FACTOR;
        }
    }
    if (result == CALIBRATION_OK)
    {
        result = confirm_save(&dialog, calibration.counts_per_gram);
    }
    if (result == CALIBRATION_OK)
    {
        result = save_and_verify(&dialog, &calibration);
    }

    if (result == CALIBRATION_CANCELLED)
    {
        say(&dialog, "Calibration cancelled. Nothing saved.\r\n");
    }
    return result;
}
