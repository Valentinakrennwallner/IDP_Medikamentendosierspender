#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/platform_util.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "psk_commissioning.h"

/*
 * Host tests for the pre-shared key commissioning (components/
 * psk_commissioning): hex parsing and the dialog with a scripted terminal
 * and a fake NVS. Every test also checks that the terminal output contains
 * no part of the key (no 4 consecutive characters of its hex text, in lower
 * or upper case). Design Document, Chapter 6, Section 6.2, Requirements 4.7
 * and 4.9.
 */

#define OUTPUT_CAPACITY 8192U

/* A random-looking test key (never a real key) and its hex text. */
static const uint8_t test_key[PERSISTENCE_PSK_SIZE] = {
    0x3c, 0x9a, 0x51, 0x07, 0xe2, 0x6d, 0xb8, 0x14, 0x4f, 0xa3, 0x0e, 0x72, 0xc5, 0x29, 0x96, 0xdb,
    0x60, 0x1b, 0xf4, 0x87, 0x3e, 0xd2, 0x58, 0xab, 0x05, 0x91, 0x6e, 0xc7, 0x2a, 0xbd, 0x43, 0xf8,
};
static const char test_hex[] = "3c9a5107e26db8144fa30e72c52996db601bf4873ed258ab05916ec72abd43f8";
static const char test_hex_upper[] = "3C9A5107E26DB8144FA30E72C52996DB601BF4873ED258AB05916EC72ABD43F8";
static const char other_hex[] = "a1b2c3d4e5f60718293a4b5c6d7e8f90112233445566778899aabbccddeeff01";

/* Scripted terminal. */
static const char *script;
static size_t script_position;
static char output[OUTPUT_CAPACITY];
static size_t output_length;
static int write_fails;

/* Fake NVS. */
static persistence_result_t init_result;
static persistence_result_t save_result;
static int stored_valid;
static uint8_t stored_key[PERSISTENCE_PSK_SIZE];
static unsigned int save_calls;
static int corrupt_after_save;

/*
 * Link-only NVS stubs: persistence.c is linked for
 * persistence_psk_is_acceptable() only; the dialog uses the fake storage
 * below, so these are never expected to succeed.
 */
esp_err_t nvs_flash_init(void)
{
    return ESP_FAIL;
}

esp_err_t nvs_flash_erase(void)
{
    return ESP_FAIL;
}

esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode,
                   nvs_handle_t *out_handle)
{
    (void)namespace_name;
    (void)open_mode;
    (void)out_handle;
    return ESP_FAIL;
}

esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value, size_t *length)
{
    (void)handle;
    (void)key;
    (void)out_value;
    (void)length;
    return ESP_FAIL;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length)
{
    (void)handle;
    (void)key;
    (void)value;
    (void)length;
    return ESP_FAIL;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    return ESP_FAIL;
}

void nvs_close(nvs_handle_t handle)
{
    (void)handle;
}

/* Report a failed check with its line number and return 1. */
static int fail(int line)
{
    (void)fprintf(stderr, "psk_commissioning_test: check failed at line %d\n", line);
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

/* Terminal input: next scripted byte; false when the script is exhausted. */
static bool fake_read_byte(uint8_t *byte)
{
    if (script[script_position] == '\0')
    {
        return false;
    }
    *byte = (uint8_t)script[script_position];
    script_position++;
    return true;
}

/* Terminal output: append to the captured output. */
static bool fake_write_text(const char *text)
{
    size_t length = strlen(text);

    if (write_fails != 0)
    {
        return false;
    }
    if ((output_length + length) < OUTPUT_CAPACITY)
    {
        (void)memcpy(&output[output_length], text, length);
        output_length += length;
        output[output_length] = '\0';
    }
    return true;
}

static persistence_result_t fake_init(void)
{
    return init_result;
}

static persistence_result_t fake_save(const uint8_t psk[PERSISTENCE_PSK_SIZE])
{
    save_calls++;
    if (save_result == PERSISTENCE_OK)
    {
        (void)memcpy(stored_key, psk, PERSISTENCE_PSK_SIZE);
        stored_valid = 1;
        if (corrupt_after_save != 0)
        {
            stored_key[5] ^= 0x01U;
        }
    }
    return save_result;
}

static persistence_result_t fake_load(uint8_t psk[PERSISTENCE_PSK_SIZE])
{
    if (stored_valid == 0)
    {
        (void)memset(psk, 0, PERSISTENCE_PSK_SIZE);
        return PERSISTENCE_MISSING;
    }
    (void)memcpy(psk, stored_key, PERSISTENCE_PSK_SIZE);
    return PERSISTENCE_OK;
}

static const psk_commissioning_io_t fake_io = {
    fake_read_byte, fake_write_text, fake_init, fake_save, fake_load,
};

/* Start a run with the given terminal input and an empty, working NVS. */
static void start(const char *input)
{
    script = input;
    script_position = 0U;
    output[0] = '\0';
    output_length = 0U;
    write_fails = 0;
    init_result = PERSISTENCE_OK;
    save_result = PERSISTENCE_OK;
    stored_valid = 0;
    (void)memset(stored_key, 0, sizeof(stored_key));
    save_calls = 0U;
    corrupt_after_save = 0;
    mock_zeroize_calls = 0U;
}

/* True if text contains any 4 consecutive characters of hex. */
static bool contains_part_of(const char *text, const char *hex)
{
    char part[5];
    size_t index;

    for (index = 0U; (index + 4U) <= strlen(hex); index++)
    {
        (void)memcpy(part, &hex[index], 4U);
        part[4] = '\0';
        if (strstr(text, part) != NULL)
        {
            return true;
        }
    }
    return false;
}

/* The output never shows the key, not even partly. */
static bool output_hides_keys(void)
{
    return !contains_part_of(output, test_hex) && !contains_part_of(output, test_hex_upper) &&
           !contains_part_of(output, other_hex);
}

/* Number of '*' characters in the output. */
static size_t count_stars(void)
{
    size_t count = 0U;
    size_t index;

    for (index = 0U; index < output_length; index++)
    {
        if (output[index] == '*')
        {
            count++;
        }
    }
    return count;
}

/* Hex parsing: exactly 64 hex characters, either case; anything else rejected. */
static int test_parse_hex(void)
{
    uint8_t key[PERSISTENCE_PSK_SIZE];
    char text[80];

    CHECK(psk_commissioning_parse_hex(test_hex, key));
    CHECK(memcmp(key, test_key, sizeof(key)) == 0);
    CHECK(psk_commissioning_parse_hex(test_hex_upper, key));
    CHECK(memcmp(key, test_key, sizeof(key)) == 0);

    /* 63 and 65 characters. */
    (void)memcpy(text, test_hex, 63U);
    text[63] = '\0';
    (void)memset(key, 0xAA, sizeof(key));
    CHECK(!psk_commissioning_parse_hex(text, key));
    CHECK((key[0] == 0U) && (key[31] == 0U));
    (void)snprintf(text, sizeof(text), "%s0", test_hex);
    CHECK(!psk_commissioning_parse_hex(text, key));
    /* Non-hex character, spaces, 0x prefix. */
    (void)strcpy(text, test_hex);
    text[10] = 'g';
    CHECK(!psk_commissioning_parse_hex(text, key));
    text[10] = ' ';
    CHECK(!psk_commissioning_parse_hex(text, key));
    (void)snprintf(text, sizeof(text), "0x%.62s", test_hex);
    CHECK(!psk_commissioning_parse_hex(text, key));
    CHECK(!psk_commissioning_parse_hex(NULL, key));
    CHECK(!psk_commissioning_parse_hex(test_hex, NULL));
    return 0;
}

/*
 * Requirement 4.7: a complete run stores the key; the input is echoed as
 * '*' only (2 x 64), the key is never shown, and the buffers are zeroized.
 */
static int test_complete_run(void)
{
    char input[200];

    (void)snprintf(input, sizeof(input), "%s\r%s\r\n", test_hex, test_hex_upper);
    start(input);
    CHECK(psk_commissioning_dialog_run(&fake_io) == PSK_COMMISSIONING_OK);
    CHECK((save_calls == 1U) && (stored_valid != 0));
    CHECK(memcmp(stored_key, test_key, sizeof(stored_key)) == 0);
    CHECK(count_stars() == (2U * PSK_COMMISSIONING_HEX_LENGTH));
    CHECK(strstr(output, "saved and verified") != NULL);
    CHECK(output_hides_keys());
    CHECK(mock_zeroize_calls >= 4U);
    return 0;
}

/* Different entries: both are discarded and the user starts again. */
static int test_mismatch_then_match(void)
{
    char input[400];

    (void)snprintf(input, sizeof(input), "%s\r%s\r%s\r%s\r", test_hex, other_hex, test_hex,
                   test_hex);
    start(input);
    CHECK(psk_commissioning_dialog_run(&fake_io) == PSK_COMMISSIONING_OK);
    CHECK(strstr(output, "do not match") != NULL);
    CHECK(memcmp(stored_key, test_key, sizeof(stored_key)) == 0);
    CHECK(save_calls == 1U);
    CHECK(output_hides_keys());
    return 0;
}

/* Wrong length, too long and non-hex input are rejected and asked again. */
static int test_invalid_input_asked_again(void)
{
    char input[600];

    (void)snprintf(input, sizeof(input), "1234\r%s00\rxyz\r%s\r%s\r", test_hex, test_hex,
                   test_hex);
    start(input);
    CHECK(psk_commissioning_dialog_run(&fake_io) == PSK_COMMISSIONING_OK);
    CHECK(strstr(output, "exactly 64 hexadecimal characters") != NULL);
    CHECK(memcmp(stored_key, test_key, sizeof(stored_key)) == 0);
    CHECK(output_hides_keys());
    return 0;
}

/* Requirement 4.9: all-zero and sequence keys are rejected and asked again. */
static int test_weak_keys_rejected(void)
{
    char input[600];
    char zero[65];
    char sequence[65];
    unsigned int index;

    (void)memset(zero, '0', 64U);
    zero[64] = '\0';
    for (index = 0U; index < 32U; index++)
    {
        (void)snprintf(&sequence[2U * index], 3U, "%02x", index);
    }
    (void)snprintf(input, sizeof(input), "%s\r%s\r%s\r%s\r", zero, sequence, test_hex,
                   test_hex);
    start(input);
    CHECK(psk_commissioning_dialog_run(&fake_io) == PSK_COMMISSIONING_OK);
    CHECK(strstr(output, "Key rejected") != NULL);
    CHECK(memcmp(stored_key, test_key, sizeof(stored_key)) == 0);
    CHECK(save_calls == 1U);
    return 0;
}

/* Backspace inside the key works; nothing but '*' and "\b \b" is echoed. */
static int test_backspace(void)
{
    char input[300];

    (void)snprintf(input, sizeof(input), "%.10sZ\b%s\r%s\r", test_hex, &test_hex[10], test_hex);
    start(input);
    CHECK(psk_commissioning_dialog_run(&fake_io) == PSK_COMMISSIONING_OK);
    CHECK(memcmp(stored_key, test_key, sizeof(stored_key)) == 0);
    CHECK(strstr(output, "\b \b") != NULL);
    CHECK(strchr(output, 'Z') == NULL);
    CHECK(output_hides_keys());
    return 0;
}

/* q cancels at the first and at the second prompt; nothing is saved. */
static int test_cancel(void)
{
    char input[200];

    start("q\r");
    CHECK(psk_commissioning_dialog_run(&fake_io) == PSK_COMMISSIONING_CANCELLED);
    CHECK(save_calls == 0U);
    CHECK(strstr(output, "No key was saved") != NULL);

    (void)snprintf(input, sizeof(input), "%s\rQ\r", test_hex);
    start(input);
    CHECK(psk_commissioning_dialog_run(&fake_io) == PSK_COMMISSIONING_CANCELLED);
    CHECK(save_calls == 0U);
    CHECK(output_hides_keys());
    return 0;
}

/* A stored valid key is only replaced after typing REPLACE. */
static int test_replace_existing_key(void)
{
    char input[300];

    start("yes\r");
    stored_valid = 1;
    (void)memcpy(stored_key, test_key, sizeof(stored_key));
    CHECK(psk_commissioning_dialog_run(&fake_io) == PSK_COMMISSIONING_CANCELLED);
    CHECK(strstr(output, "already stored") != NULL);
    CHECK(save_calls == 0U);
    CHECK(memcmp(stored_key, test_key, sizeof(stored_key)) == 0);

    start("q\r");
    stored_valid = 1;
    CHECK(psk_commissioning_dialog_run(&fake_io) == PSK_COMMISSIONING_CANCELLED);
    CHECK(save_calls == 0U);

    (void)snprintf(input, sizeof(input), "REPLACE\r%s\r%s\r", other_hex, other_hex);
    start(input);
    stored_valid = 1;
    (void)memcpy(stored_key, test_key, sizeof(stored_key));
    CHECK(psk_commissioning_dialog_run(&fake_io) == PSK_COMMISSIONING_OK);
    CHECK(save_calls == 1U);
    CHECK(memcmp(stored_key, test_key, sizeof(stored_key)) != 0);
    CHECK(output_hides_keys());
    return 0;
}

/* NVS init and save errors, and a key that is not read back unchanged. */
static int test_storage_errors(void)
{
    char input[200];

    start("");
    init_result = PERSISTENCE_STORAGE_ERROR;
    CHECK(psk_commissioning_dialog_run(&fake_io) == PSK_COMMISSIONING_STORAGE_ERROR);
    CHECK(script_position == 0U);

    (void)snprintf(input, sizeof(input), "%s\r%s\r", test_hex, test_hex);
    start(input);
    save_result = PERSISTENCE_STORAGE_ERROR;
    CHECK(psk_commissioning_dialog_run(&fake_io) == PSK_COMMISSIONING_STORAGE_ERROR);
    CHECK(strstr(output, "could not be saved") != NULL);

    start(input);
    corrupt_after_save = 1;
    CHECK(psk_commissioning_dialog_run(&fake_io) == PSK_COMMISSIONING_VERIFY_FAILED);
    CHECK(output_hides_keys());
    return 0;
}

/* Serial I/O errors and an incomplete I/O description. */
static int test_io_errors_and_arguments(void)
{
    psk_commissioning_io_t incomplete = fake_io;

    start(test_hex);
    CHECK(psk_commissioning_dialog_run(&fake_io) == PSK_COMMISSIONING_IO_ERROR);
    CHECK(save_calls == 0U);

    start("q\r");
    write_fails = 1;
    CHECK(psk_commissioning_dialog_run(&fake_io) == PSK_COMMISSIONING_IO_ERROR);

    CHECK(psk_commissioning_dialog_run(NULL) == PSK_COMMISSIONING_INVALID_ARGUMENT);
    incomplete.load = NULL;
    CHECK(psk_commissioning_dialog_run(&incomplete) == PSK_COMMISSIONING_INVALID_ARGUMENT);
    return 0;
}

/* Run all pre-shared key commissioning tests; exit status 0 only if all pass. */
int main(void)
{
    int failures = 0;

    failures += test_parse_hex();
    failures += test_complete_run();
    failures += test_mismatch_then_match();
    failures += test_invalid_input_asked_again();
    failures += test_weak_keys_rejected();
    failures += test_backspace();
    failures += test_cancel();
    failures += test_replace_existing_key();
    failures += test_storage_errors();
    failures += test_io_errors_and_arguments();

    if (failures != 0)
    {
        (void)fprintf(stderr, "psk_commissioning_test: %d test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    (void)printf("psk_commissioning_test: all tests passed\n");
    return EXIT_SUCCESS;
}
