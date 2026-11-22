#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "persistence.h"

/*
 * Host tests for the calibration persistence. The NVS API is replaced by an
 * in-memory store holding one blob; every NVS call can be made to fail.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */

#define STORE_CAPACITY 32U

/* In-memory NVS: one blob under the calibration namespace and key. */
static unsigned char stored_blob[STORE_CAPACITY];
static size_t stored_length;
static int namespace_exists;
static int blob_exists;
static unsigned int set_blob_calls;
static unsigned int open_handles;
static unsigned int erase_calls;
static unsigned int init_calls;

/* Injected errors per NVS call (ESP_OK = no error). */
static esp_err_t flash_init_error;
static esp_err_t flash_erase_error;
static esp_err_t open_error;
static esp_err_t get_error;
static esp_err_t set_error;
static esp_err_t commit_error;

/* Report a failed check with its line number and return 1. */
static int fail(int line)
{
    (void)fprintf(stderr, "persistence_test: check failed at line %d\n", line);
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

/* Empty store, no injected errors. */
static void reset_store(void)
{
    (void)memset(stored_blob, 0, sizeof(stored_blob));
    stored_length = 0U;
    namespace_exists = 0;
    blob_exists = 0;
    set_blob_calls = 0U;
    open_handles = 0U;
    erase_calls = 0U;
    init_calls = 0U;
    flash_init_error = ESP_OK;
    flash_erase_error = ESP_OK;
    open_error = ESP_OK;
    get_error = ESP_OK;
    set_error = ESP_OK;
    commit_error = ESP_OK;
}

esp_err_t nvs_flash_init(void)
{
    init_calls++;
    return flash_init_error;
}

/* Erasing removes the stored blob and the namespace. */
esp_err_t nvs_flash_erase(void)
{
    erase_calls++;
    if (flash_erase_error != ESP_OK)
    {
        return flash_erase_error;
    }
    namespace_exists = 0;
    blob_exists = 0;
    stored_length = 0U;
    return ESP_OK;
}

/* Like ESP-IDF: opening a missing namespace read-only reports NOT_FOUND. */
esp_err_t nvs_open(const char *namespace_name, nvs_open_mode_t open_mode,
                   nvs_handle_t *out_handle)
{
    if (open_error != ESP_OK)
    {
        return open_error;
    }
    if (strcmp(namespace_name, PERSISTENCE_NAMESPACE) != 0)
    {
        return ESP_FAIL;
    }
    if ((open_mode == NVS_READONLY) && (namespace_exists == 0))
    {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    namespace_exists = 1;
    open_handles++;
    *out_handle = 1U;
    return ESP_OK;
}

/* Like ESP-IDF: a buffer smaller than the stored blob is INVALID_LENGTH. */
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *out_value,
                       size_t *length)
{
    (void)handle;
    if (get_error != ESP_OK)
    {
        return get_error;
    }
    if ((strcmp(key, PERSISTENCE_CALIBRATION_KEY) != 0) || (blob_exists == 0))
    {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (*length < stored_length)
    {
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    (void)memcpy(out_value, stored_blob, stored_length);
    *length = stored_length;
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value,
                       size_t length)
{
    (void)handle;
    set_blob_calls++;
    if (set_error != ESP_OK)
    {
        return set_error;
    }
    if ((strcmp(key, PERSISTENCE_CALIBRATION_KEY) != 0) || (length > STORE_CAPACITY))
    {
        return ESP_FAIL;
    }
    (void)memcpy(stored_blob, value, length);
    stored_length = length;
    blob_exists = 1;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    return commit_error;
}

void nvs_close(nvs_handle_t handle)
{
    (void)handle;
    open_handles--;
}

/* Reference CRC-32 (zlib) for hand-built blobs. */
static uint32_t reference_crc32(const unsigned char *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    size_t index;
    int bit;

    for (index = 0U; index < length; index++)
    {
        crc ^= data[index];
        for (bit = 0; bit < 8; bit++)
        {
            crc = ((crc & 1U) != 0U) ? ((crc >> 1) ^ 0xEDB88320U) : (crc >> 1);
        }
    }
    return ~crc;
}

static void put_u32_be(unsigned char *output, uint32_t value)
{
    output[0] = (unsigned char)(value >> 24);
    output[1] = (unsigned char)(value >> 16);
    output[2] = (unsigned char)(value >> 8);
    output[3] = (unsigned char)value;
}

/* Store a hand-built 16-byte blob with a correct CRC. */
static void store_blob(uint32_t version, uint32_t tare, uint32_t counts_per_kilogram)
{
    put_u32_be(&stored_blob[0], version);
    put_u32_be(&stored_blob[4], tare);
    put_u32_be(&stored_blob[8], counts_per_kilogram);
    put_u32_be(&stored_blob[12], reference_crc32(stored_blob, 12U));
    stored_length = PERSISTENCE_CALIBRATION_BLOB_SIZE;
    namespace_exists = 1;
    blob_exists = 1;
}

/* The reference CRC itself matches the standard check value. */
static int test_reference_crc(void)
{
    static const unsigned char check[] = "123456789";

    CHECK(reference_crc32(check, 9U) == 0xCBF43926U);
    return 0;
}

/* nvs_flash_init() errors are never repaired by erasing (fail-closed). */
static int test_init(void)
{
    reset_store();
    CHECK(persistence_init() == PERSISTENCE_OK);
    flash_init_error = ESP_ERR_NVS_NO_FREE_PAGES;
    CHECK(persistence_init() == PERSISTENCE_STORAGE_ERROR);
    flash_init_error = ESP_ERR_NVS_NEW_VERSION_FOUND;
    CHECK(persistence_init() == PERSISTENCE_STORAGE_ERROR);
    CHECK(erase_calls == 0U);
    return 0;
}

/* Explicit erase: deletes the calibration, then initialises NVS again. */
static int test_erase_and_init(void)
{
    const weight_calibration_t saved = {0, 17660.0F};
    weight_calibration_t loaded = {7, 7.0F};

    reset_store();
    CHECK(persistence_calibration_save(&saved) == PERSISTENCE_OK);
    CHECK(persistence_erase_and_init() == PERSISTENCE_OK);
    CHECK((erase_calls == 1U) && (init_calls == 1U));
    CHECK(persistence_calibration_load(&loaded) == PERSISTENCE_MISSING);

    reset_store();
    flash_erase_error = ESP_FAIL;
    CHECK(persistence_erase_and_init() == PERSISTENCE_STORAGE_ERROR);
    CHECK(init_calls == 0U);

    reset_store();
    flash_init_error = ESP_ERR_NVS_NO_FREE_PAGES;
    CHECK(persistence_erase_and_init() == PERSISTENCE_STORAGE_ERROR);
    return 0;
}

/* Requirement 2.8: the commissioning values survive save and load. */
static int test_round_trip(void)
{
    const weight_calibration_t saved = {123456, WEIGHT_COUNTS_PER_GRAM};
    weight_calibration_t loaded = {0, 0.0F};

    reset_store();
    CHECK(persistence_calibration_save(&saved) == PERSISTENCE_OK);
    CHECK(stored_length == PERSISTENCE_CALIBRATION_BLOB_SIZE);
    CHECK(persistence_calibration_load(&loaded) == PERSISTENCE_OK);
    CHECK(loaded.tare_offset_counts == 123456);
    CHECK(loaded.counts_per_gram == 17660.0F);
    CHECK(open_handles == 0U);
    return 0;
}

/* Byte layout: version 1, tare, counts per kilogram, CRC-32, big-endian. */
static int test_blob_layout(void)
{
    const weight_calibration_t saved = {-2, 17660.0F};
    unsigned char expected[PERSISTENCE_CALIBRATION_BLOB_SIZE];

    reset_store();
    CHECK(persistence_calibration_save(&saved) == PERSISTENCE_OK);
    put_u32_be(&expected[0], 1U);
    put_u32_be(&expected[4], 0xFFFFFFFEU);
    put_u32_be(&expected[8], 17660000U);
    put_u32_be(&expected[12], reference_crc32(expected, 12U));
    CHECK(memcmp(stored_blob, expected, sizeof(expected)) == 0);
    return 0;
}

/* Negative values, rounding to 0.001 count/g, and the tare range limits. */
static int test_values_and_rounding(void)
{
    const weight_calibration_t negative = {PERSISTENCE_TARE_MINIMUM_COUNTS, -17660.4F};
    const weight_calibration_t upper = {PERSISTENCE_TARE_MAXIMUM_COUNTS, 0.0006F};
    weight_calibration_t loaded = {0, 0.0F};

    reset_store();
    CHECK(persistence_calibration_save(&negative) == PERSISTENCE_OK);
    CHECK(persistence_calibration_load(&loaded) == PERSISTENCE_OK);
    CHECK(loaded.tare_offset_counts == PERSISTENCE_TARE_MINIMUM_COUNTS);
    CHECK(fabsf(loaded.counts_per_gram - (-17660.4F)) < 0.001F);

    CHECK(persistence_calibration_save(&upper) == PERSISTENCE_OK);
    CHECK(persistence_calibration_load(&loaded) == PERSISTENCE_OK);
    CHECK(loaded.tare_offset_counts == PERSISTENCE_TARE_MAXIMUM_COUNTS);
    CHECK(loaded.counts_per_gram == 0.001F);
    return 0;
}

/* Requirement 2.8: never commissioned means MISSING, output unchanged. */
static int test_missing(void)
{
    weight_calibration_t loaded = {7, 7.0F};

    reset_store();
    CHECK(persistence_calibration_load(&loaded) == PERSISTENCE_MISSING);
    namespace_exists = 1;
    CHECK(persistence_calibration_load(&loaded) == PERSISTENCE_MISSING);
    CHECK((loaded.tare_offset_counts == 7) && (loaded.counts_per_gram == 7.0F));
    CHECK(open_handles == 0U);
    return 0;
}

/* Requirement 2.8: corrupt data is rejected and the output stays unchanged. */
static int test_corrupt_data(void)
{
    weight_calibration_t loaded = {7, 7.0F};

    reset_store();
    store_blob(1U, 100U, 17660000U);
    stored_blob[5] ^= 0x01U;
    CHECK(persistence_calibration_load(&loaded) == PERSISTENCE_INVALID_CHECKSUM);

    store_blob(1U, 100U, 17660000U);
    stored_blob[15] ^= 0x80U;
    CHECK(persistence_calibration_load(&loaded) == PERSISTENCE_INVALID_CHECKSUM);

    store_blob(2U, 100U, 17660000U);
    CHECK(persistence_calibration_load(&loaded) == PERSISTENCE_INVALID_VERSION);

    store_blob(1U, 100U, 0U);
    CHECK(persistence_calibration_load(&loaded) == PERSISTENCE_INVALID_VALUE);

    store_blob(1U, 0x007FFFFFU, 17660000U);
    CHECK(persistence_calibration_load(&loaded) == PERSISTENCE_INVALID_VALUE);

    store_blob(1U, 0xFF800000U, 17660000U);
    CHECK(persistence_calibration_load(&loaded) == PERSISTENCE_INVALID_VALUE);

    store_blob(1U, 100U, 17660000U);
    stored_length = 12U;
    CHECK(persistence_calibration_load(&loaded) == PERSISTENCE_INVALID_SIZE);

    store_blob(1U, 100U, 17660000U);
    stored_length = 20U;
    CHECK(persistence_calibration_load(&loaded) == PERSISTENCE_INVALID_SIZE);

    CHECK((loaded.tare_offset_counts == 7) && (loaded.counts_per_gram == 7.0F));
    CHECK(open_handles == 0U);
    return 0;
}

/* Invalid values are rejected before anything is written to NVS. */
static int test_save_rejects_invalid_values(void)
{
    static const float bad_factors[] = {0.0F, 0.0004F, -0.0004F, 3.0e6F, -3.0e6F};
    weight_calibration_t calibration = {0, 0.0F};
    size_t index;

    reset_store();
    for (index = 0U; index < (sizeof(bad_factors) / sizeof(bad_factors[0])); index++)
    {
        calibration.tare_offset_counts = 0;
        calibration.counts_per_gram = bad_factors[index];
        CHECK(persistence_calibration_save(&calibration) == PERSISTENCE_INVALID_VALUE);
    }
    calibration.counts_per_gram = NAN;
    CHECK(persistence_calibration_save(&calibration) == PERSISTENCE_INVALID_VALUE);
    calibration.counts_per_gram = INFINITY;
    CHECK(persistence_calibration_save(&calibration) == PERSISTENCE_INVALID_VALUE);

    calibration.counts_per_gram = 17660.0F;
    calibration.tare_offset_counts = PERSISTENCE_TARE_MAXIMUM_COUNTS + 1;
    CHECK(persistence_calibration_save(&calibration) == PERSISTENCE_INVALID_VALUE);
    calibration.tare_offset_counts = PERSISTENCE_TARE_MINIMUM_COUNTS - 1;
    CHECK(persistence_calibration_save(&calibration) == PERSISTENCE_INVALID_VALUE);

    CHECK((set_blob_calls == 0U) && (blob_exists == 0));
    return 0;
}

/* NVS errors on open, read, write and commit are storage errors. */
static int test_storage_errors(void)
{
    const weight_calibration_t saved = {0, 17660.0F};
    weight_calibration_t loaded = {7, 7.0F};

    reset_store();
    open_error = ESP_FAIL;
    CHECK(persistence_calibration_save(&saved) == PERSISTENCE_STORAGE_ERROR);
    CHECK(persistence_calibration_load(&loaded) == PERSISTENCE_STORAGE_ERROR);

    reset_store();
    set_error = ESP_FAIL;
    CHECK(persistence_calibration_save(&saved) == PERSISTENCE_STORAGE_ERROR);
    CHECK(open_handles == 0U);

    reset_store();
    commit_error = ESP_FAIL;
    CHECK(persistence_calibration_save(&saved) == PERSISTENCE_STORAGE_ERROR);
    CHECK(open_handles == 0U);

    reset_store();
    store_blob(1U, 100U, 17660000U);
    get_error = ESP_FAIL;
    CHECK(persistence_calibration_load(&loaded) == PERSISTENCE_STORAGE_ERROR);
    CHECK(open_handles == 0U);

    CHECK((loaded.tare_offset_counts == 7) && (loaded.counts_per_gram == 7.0F));
    return 0;
}

/* Missing arguments are rejected without touching NVS. */
static int test_invalid_arguments(void)
{
    reset_store();
    CHECK(persistence_calibration_load(NULL) == PERSISTENCE_INVALID_ARGUMENT);
    CHECK(persistence_calibration_save(NULL) == PERSISTENCE_INVALID_ARGUMENT);
    CHECK((set_blob_calls == 0U) && (namespace_exists == 0));
    return 0;
}

/* Run the calibration persistence tests without ESP-IDF or hardware. */
int main(void)
{
    if ((test_reference_crc() != 0) || (test_init() != 0) ||
        (test_erase_and_init() != 0) ||
        (test_round_trip() != 0) || (test_blob_layout() != 0) ||
        (test_values_and_rounding() != 0) || (test_missing() != 0) ||
        (test_corrupt_data() != 0) || (test_save_rejects_invalid_values() != 0) ||
        (test_storage_errors() != 0) || (test_invalid_arguments() != 0))
    {
        (void)fputs("persistence tests failed\n", stderr);
        return EXIT_FAILURE;
    }

    (void)puts("persistence tests passed");
    return EXIT_SUCCESS;
}
