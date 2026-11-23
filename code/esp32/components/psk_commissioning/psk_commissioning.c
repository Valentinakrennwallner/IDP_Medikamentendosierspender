/*
 * Serial commissioning of the pre-shared key: hex parsing and the dialog.
 * All hardware access goes through psk_commissioning_io_t, so this file is
 * host-testable; the UART and NVS binding is in psk_commissioning_uart.c.
 * Logically part of the module "Bluetooth communication and encryption"
 * (Design Document, Chapter 2, Section 2.1).
 * Design Document, Chapter 2, Sections 2.1 and 2.2, and Chapter 6,
 * Section 6.2, Requirements 4.7 and 4.9.
 *
 * The key is never shown: the input is echoed as '*', and no output
 * contains a character, byte, checksum or fingerprint of the key. Every
 * buffer that held the key or its hex text is overwritten with
 * mbedtls_platform_zeroize(), which the compiler must not remove as a dead
 * store (unlike memset()). Nothing is written to the log.
 */
#include "psk_commissioning.h"

#include <stddef.h>
#include <string.h>

#include "mbedtls/platform_util.h"
#include "serial_line.h"

#define HEX_DIGIT_COUNT 16U
#define BITS_PER_HEX_DIGIT 4U

static const char hex_lower[] = "0123456789abcdef";
static const char hex_upper[] = "0123456789ABCDEF";

typedef enum
{
    PSK_INPUT_LINE = 0,
    PSK_INPUT_CANCEL = 1,
    PSK_INPUT_IO_ERROR = 2
} psk_input_t;

typedef struct
{
    const psk_commissioning_io_t *io;
    /* Hex text of the key while it is typed; overwritten after every line. */
    char line[PSK_COMMISSIONING_HEX_LENGTH + 1U];
    /* Masked line input (components/serial_line), writes into line. */
    serial_line_t input;
} psk_dialog_t;

/*
 * Value of one hexadecimal digit (table lookup, no character arithmetic).
 * Returns false for any other character.
 * Design Document, Chapter 6, Section 6.2, Requirement 4.7.
 */
static bool psk_hex_value(char character, uint8_t *value)
{
    bool found = false;
    uint32_t index;

    for (index = 0U; (index < HEX_DIGIT_COUNT) && !found; ++index)
    {
        if ((character == hex_lower[index]) || (character == hex_upper[index]))
        {
            *value = (uint8_t)index;
            found = true;
        }
    }
    return found;
}

/*
 * Convert exactly 64 hexadecimal characters into the 32-byte key. Returns
 * false and overwrites psk with zeros for any other text.
 * Design Document, Chapter 6, Section 6.2, Requirement 4.7.
 */
bool psk_commissioning_parse_hex(const char *text, uint8_t psk[PERSISTENCE_PSK_SIZE])
{
    bool valid;
    uint8_t high = 0U;
    uint8_t low = 0U;
    uint32_t index;

    if (psk == NULL)
    {
        return false;
    }

    valid = (text != NULL) && (strlen(text) == PSK_COMMISSIONING_HEX_LENGTH);
    for (index = 0U; (index < PERSISTENCE_PSK_SIZE) && valid; ++index)
    {
        valid = psk_hex_value(text[2U * index], &high) && psk_hex_value(text[(2U * index) + 1U], &low);
        if (valid)
        {
            psk[index] = (uint8_t)(((uint32_t)high << BITS_PER_HEX_DIGIT) | (uint32_t)low);
        }
    }
    if (!valid)
    {
        mbedtls_platform_zeroize(psk, PERSISTENCE_PSK_SIZE);
    }
    return valid;
}

/*
 * Compare two keys without an early exit (the time does not depend on the
 * position of the first difference). Design Document, Chapter 6,
 * Section 6.2, Requirement 4.7.
 */
static bool psk_keys_equal(const uint8_t *first, const uint8_t *second)
{
    uint8_t difference = 0U;
    uint32_t index;

    for (index = 0U; index < PERSISTENCE_PSK_SIZE; ++index)
    {
        difference |= (uint8_t)(first[index] ^ second[index]);
    }
    return (difference == 0U);
}

/*
 * Send a text to the terminal. Design Document, Chapter 6, Section 6.2,
 * Requirement 4.7.
 */
static void psk_say(psk_dialog_t *dialog, const char *text)
{
    serial_line_say(&dialog->input, text);
}

/*
 * Read one input line and classify it: q or Q cancels everywhere.
 * Design Document, Chapter 6, Section 6.2, Requirement 4.7.
 */
static psk_input_t psk_read_input(psk_dialog_t *dialog)
{
    psk_input_t input = PSK_INPUT_IO_ERROR;

    if (serial_line_read(&dialog->input))
    {
        input = (serial_line_is(&dialog->input, "q") || serial_line_is(&dialog->input, "Q"))
                    ? PSK_INPUT_CANCEL
                    : PSK_INPUT_LINE;
    }
    return input;
}

/*
 * Map a non-line input to the dialog result.
 * Design Document, Chapter 6, Section 6.2, Requirement 4.7.
 */
static psk_commissioning_result_t psk_input_result(psk_input_t input)
{
    return (input == PSK_INPUT_CANCEL) ? PSK_COMMISSIONING_CANCELLED : PSK_COMMISSIONING_IO_ERROR;
}

/*
 * If a valid key is already stored, overwrite it only after the user has
 * typed REPLACE; anything else cancels and keeps the stored key.
 * Design Document, Chapter 6, Section 6.2, Requirement 4.7.
 */
static psk_commissioning_result_t psk_confirm_replace(psk_dialog_t *dialog)
{
    psk_commissioning_result_t result = PSK_COMMISSIONING_OK;
    uint8_t existing[PERSISTENCE_PSK_SIZE];
    bool stored = (dialog->io->load(existing) == PERSISTENCE_OK);
    psk_input_t input;

    mbedtls_platform_zeroize(existing, sizeof(existing));
    if (stored)
    {
        psk_say(dialog, "A valid pre-shared key is already stored.\r\n"
                    "Type REPLACE to replace it, anything else cancels: ");
        input = psk_read_input(dialog);
        if (input == PSK_INPUT_IO_ERROR)
        {
            result = PSK_COMMISSIONING_IO_ERROR;
        }
        else if ((input == PSK_INPUT_CANCEL) || !serial_line_is(&dialog->input, "REPLACE"))
        {
            result = PSK_COMMISSIONING_CANCELLED;
        }
        else
        {
            /* REPLACE confirmed: continue with the key entry. */
        }
        mbedtls_platform_zeroize(dialog->line, sizeof(dialog->line));
    }
    return result;
}

/*
 * Ask for the key until 64 hexadecimal characters of an acceptable key are
 * typed, or until q or an I/O error. The typed text is overwritten right
 * after parsing. Design Document, Chapter 6, Section 6.2, Requirements 4.7
 * and 4.9.
 */
static psk_commissioning_result_t psk_read_key(psk_dialog_t *dialog, const char *prompt,
                                           uint8_t key[PERSISTENCE_PSK_SIZE])
{
    psk_commissioning_result_t result = PSK_COMMISSIONING_OK;
    bool done = false;
    bool parsed;
    psk_input_t input;

    while (!done)
    {
        psk_say(dialog, prompt);
        input = psk_read_input(dialog);
        parsed = (input == PSK_INPUT_LINE) && !dialog->input.too_long &&
                 psk_commissioning_parse_hex(dialog->line, key);
        mbedtls_platform_zeroize(dialog->line, sizeof(dialog->line));

        if (input != PSK_INPUT_LINE)
        {
            result = psk_input_result(input);
            done = true;
        }
        else if (!parsed)
        {
            psk_say(dialog, "Invalid input: exactly 64 hexadecimal characters (0-9, a-f) "
                        "are required.\r\n");
        }
        else if (!persistence_psk_is_acceptable(key))
        {
            mbedtls_platform_zeroize(key, PERSISTENCE_PSK_SIZE);
            psk_say(dialog, "Key rejected: it is not random (e.g. all bytes equal). "
                        "Use a randomly generated key.\r\n");
        }
        else
        {
            done = true;
        }
    }
    return result;
}

/*
 * Read the key twice until both entries match, or until q or an I/O error.
 * On a mismatch both entries are discarded and the user starts again.
 * Design Document, Chapter 6, Section 6.2, Requirement 4.7.
 */
static psk_commissioning_result_t psk_read_key_twice(psk_dialog_t *dialog,
                                                 uint8_t key[PERSISTENCE_PSK_SIZE])
{
    psk_commissioning_result_t result = PSK_COMMISSIONING_OK;
    uint8_t repeated[PERSISTENCE_PSK_SIZE];
    bool done = false;

    while (!done)
    {
        result = psk_read_key(dialog, "Enter the key (64 hexadecimal characters): ", key);
        if (result == PSK_COMMISSIONING_OK)
        {
            result = psk_read_key(dialog, "Enter the key again: ", repeated);
        }
        if (result != PSK_COMMISSIONING_OK)
        {
            done = true;
        }
        else if (psk_keys_equal(key, repeated))
        {
            done = true;
        }
        else
        {
            mbedtls_platform_zeroize(key, PERSISTENCE_PSK_SIZE);
            psk_say(dialog, "The two entries do not match. Please start again.\r\n");
        }
        mbedtls_platform_zeroize(repeated, sizeof(repeated));
    }
    return result;
}

/*
 * Save the key and verify it by reloading it and comparing.
 * Design Document, Chapter 2, Section 2.2, and Chapter 6, Section 6.2,
 * Requirement 4.7.
 */
static psk_commissioning_result_t psk_save_and_verify(psk_dialog_t *dialog,
                                                  const uint8_t key[PERSISTENCE_PSK_SIZE])
{
    psk_commissioning_result_t result = PSK_COMMISSIONING_OK;
    uint8_t reloaded[PERSISTENCE_PSK_SIZE];

    if (dialog->io->save(key) != PERSISTENCE_OK)
    {
        psk_say(dialog, "The key could not be saved. Nothing was changed.\r\n");
        result = PSK_COMMISSIONING_STORAGE_ERROR;
    }
    else if ((dialog->io->load(reloaded) != PERSISTENCE_OK) || !psk_keys_equal(key, reloaded))
    {
        psk_say(dialog, "The saved key could not be read back unchanged. "
                    "Repeat the commissioning.\r\n");
        result = PSK_COMMISSIONING_VERIFY_FAILED;
    }
    else
    {
        psk_say(dialog, "Pre-shared key saved and verified.\r\n");
    }
    mbedtls_platform_zeroize(reloaded, sizeof(reloaded));
    return result;
}

/*
 * True if every function of the I/O description is present.
 * Design Document, Chapter 6, Section 6.2, Requirement 4.7.
 */
static bool psk_io_is_complete(const psk_commissioning_io_t *io)
{
    return (io != NULL) && (io->read_byte != NULL) && (io->write_text != NULL) &&
           (io->storage_init != NULL) && (io->save != NULL) && (io->load != NULL);
}

/*
 * Run the commissioning dialog (see psk_commissioning.h). Every copy of the
 * key is overwritten before return.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.7 and 4.9.
 */
psk_commissioning_result_t psk_commissioning_dialog_run(const psk_commissioning_io_t *io)
{
    psk_dialog_t dialog;
    uint8_t key[PERSISTENCE_PSK_SIZE] = {0};
    psk_commissioning_result_t result = PSK_COMMISSIONING_OK;

    if (!psk_io_is_complete(io))
    {
        return PSK_COMMISSIONING_INVALID_ARGUMENT;
    }

    (void)memset(&dialog, 0, sizeof(dialog));
    dialog.io = io;
    serial_line_init(&dialog.input, io->read_byte, io->write_text, dialog.line,
                     PSK_COMMISSIONING_HEX_LENGTH, SERIAL_LINE_ECHO_MASKED);

    psk_say(&dialog, "\r\n=== Pre-shared key (commissioning) ===\r\n"
                 "Use the same 64 hexadecimal characters as psk_hex on the Raspberry Pi.\r\n"
                 "The key is not shown. Enter q at any prompt to cancel without saving.\r\n");

    if (io->storage_init() != PERSISTENCE_OK)
    {
        psk_say(&dialog, "NVS cannot be used. Nothing was saved.\r\n");
        result = PSK_COMMISSIONING_STORAGE_ERROR;
    }
    if (result == PSK_COMMISSIONING_OK)
    {
        result = psk_confirm_replace(&dialog);
    }
    if (result == PSK_COMMISSIONING_OK)
    {
        result = psk_read_key_twice(&dialog, key);
    }
    if (result == PSK_COMMISSIONING_OK)
    {
        result = psk_save_and_verify(&dialog, key);
    }
    if (result == PSK_COMMISSIONING_CANCELLED)
    {
        psk_say(&dialog, "Commissioning cancelled. No key was saved.\r\n");
    }

    mbedtls_platform_zeroize(key, sizeof(key));
    mbedtls_platform_zeroize(dialog.line, sizeof(dialog.line));
    return result;
}
