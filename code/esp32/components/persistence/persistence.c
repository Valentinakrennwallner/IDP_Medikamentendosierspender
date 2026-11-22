/*
 * Non-volatile storage of the commissioning calibration (tare offset and
 * counts per gram) of the ESP32 sensor node.
 * Design Document, Chapter 2, Sections 2.1 and 2.2, Chapter 3, and
 * Chapter 6, Section 6.1, Requirement 2.8.
 *
 * Blob layout, version 1 (16 bytes, big-endian):
 *   offset  0  uint32  layout version (PERSISTENCE_CALIBRATION_VERSION)
 *   offset  4  int32   tare offset in HX711 counts
 *   offset  8  int32   calibration factor in counts per kilogram
 *                      (counts per gram x 1000, resolution 0.001 count/g)
 *   offset 12  uint32  CRC-32 (IEEE 802.3) over bytes 0 to 11
 * The factor is stored as a fixed-point integer so that no float object is
 * reinterpreted as an integer. The CRC-32 detects corrupted or foreign data;
 * it is no protection against deliberate manipulation.
 */
#include "persistence.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nvs.h"
#include "nvs_flash.h"

#define BLOB_VERSION_OFFSET 0U
#define BLOB_TARE_OFFSET 4U
#define BLOB_FACTOR_OFFSET 8U
#define BLOB_CRC_OFFSET 12U

#define COUNTS_PER_GRAM_SCALE 1000.0
#define CRC32_INITIAL 0xFFFFFFFFU
#define CRC32_POLYNOMIAL 0xEDB88320U
#define INT32_MAXIMUM_AS_DOUBLE 2147483647.0
#define INT32_MINIMUM_AS_DOUBLE (-2147483648.0)

/*
 * Write a 32-bit value big-endian into four bytes.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static void write_u32_be(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

/*
 * Read a big-endian 32-bit value from four bytes.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static uint32_t read_u32_be(const uint8_t *input)
{
    return ((uint32_t)input[0] << 24U) | ((uint32_t)input[1] << 16U) |
           ((uint32_t)input[2] << 8U) | (uint32_t)input[3];
}

/*
 * Convert the two's-complement bit pattern back to a signed value without
 * relying on implementation-defined unsigned-to-signed conversion.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static int32_t u32_to_i32(uint32_t value)
{
    int32_t result;

    if (value <= (uint32_t)INT32_MAX)
    {
        result = (int32_t)value;
    }
    else
    {
        uint32_t inverted = ~value;

        result = -(int32_t)inverted - 1;
    }
    return result;
}

/*
 * CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320, as used by zlib),
 * computed bitwise to keep the code small.
 * Design Document, Chapter 6, Section 6.1, Requirement 2.8.
 */
static uint32_t crc32_compute(const uint8_t *data, size_t length)
{
    uint32_t crc = CRC32_INITIAL;
    size_t index;
    uint32_t bit;

    for (index = 0U; index < length; ++index)
    {
        crc ^= (uint32_t)data[index];
        for (bit = 0U; bit < 8U; ++bit)
        {
            if ((crc & 1U) != 0U)
            {
                crc = (crc >> 1U) ^ CRC32_POLYNOMIAL;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }
    return ~crc;
}

/*
 * Accept only tare offsets inside the signed 24-bit HX711 range without the
 * two limit values that mark a faulty conversion.
 * Design Document, Chapter 6, Section 6.1, Requirements 2.2 and 2.8.
 */
static bool tare_is_valid(int32_t tare_offset_counts)
{
    return (tare_offset_counts >= PERSISTENCE_TARE_MINIMUM_COUNTS) &&
           (tare_offset_counts <= PERSISTENCE_TARE_MAXIMUM_COUNTS);
}

/*
 * Convert counts per gram to rounded counts per kilogram. Returns false if
 * the factor is not finite, rounds to zero, or does not fit into int32_t.
 * Design Document, Chapter 2, Section 2.1, and Chapter 6, Section 6.1,
 * Requirement 2.8.
 */
static bool factor_to_counts_per_kilogram(float counts_per_gram,
                                          int32_t *counts_per_kilogram)
{
    bool valid = false;

    if (isfinite(counts_per_gram) != 0)
    {
        double scaled = (double)counts_per_gram * COUNTS_PER_GRAM_SCALE;

        if (scaled >= 0.0)
        {
            scaled = floor(scaled + 0.5);
        }
        else
        {
            scaled = ceil(scaled - 0.5);
        }
        if ((scaled <= INT32_MAXIMUM_AS_DOUBLE) &&
            (scaled >= INT32_MINIMUM_AS_DOUBLE) && (fabs(scaled) >= 1.0))
        {
            *counts_per_kilogram = (int32_t)scaled;
            valid = true;
        }
    }
    return valid;
}

/*
 * Validate the calibration and serialise it into the version-1 blob.
 * Design Document, Chapter 2, Section 2.1, and Chapter 6, Section 6.1,
 * Requirement 2.8.
 */
static persistence_result_t encode_calibration(const weight_calibration_t *calibration,
                                               uint8_t *blob)
{
    int32_t counts_per_kilogram = 0;
    persistence_result_t result = PERSISTENCE_INVALID_VALUE;

    if (tare_is_valid(calibration->tare_offset_counts) &&
        factor_to_counts_per_kilogram(calibration->counts_per_gram,
                                      &counts_per_kilogram))
    {
        write_u32_be(&blob[BLOB_VERSION_OFFSET], PERSISTENCE_CALIBRATION_VERSION);
        write_u32_be(&blob[BLOB_TARE_OFFSET], (uint32_t)calibration->tare_offset_counts);
        write_u32_be(&blob[BLOB_FACTOR_OFFSET], (uint32_t)counts_per_kilogram);
        write_u32_be(&blob[BLOB_CRC_OFFSET], crc32_compute(blob, BLOB_CRC_OFFSET));
        result = PERSISTENCE_OK;
    }
    return result;
}

/*
 * Check checksum, version and values of a blob read from NVS and convert it
 * to a calibration. The output is written only on PERSISTENCE_OK.
 * Design Document, Chapter 2, Section 2.1, and Chapter 6, Section 6.1,
 * Requirement 2.8.
 */
static persistence_result_t decode_calibration(const uint8_t *blob,
                                               weight_calibration_t *calibration)
{
    persistence_result_t result = PERSISTENCE_OK;

    if (read_u32_be(&blob[BLOB_CRC_OFFSET]) != crc32_compute(blob, BLOB_CRC_OFFSET))
    {
        result = PERSISTENCE_INVALID_CHECKSUM;
    }
    else if (read_u32_be(&blob[BLOB_VERSION_OFFSET]) != PERSISTENCE_CALIBRATION_VERSION)
    {
        result = PERSISTENCE_INVALID_VERSION;
    }
    else
    {
        int32_t tare_offset_counts = u32_to_i32(read_u32_be(&blob[BLOB_TARE_OFFSET]));
        int32_t counts_per_kilogram =
            u32_to_i32(read_u32_be(&blob[BLOB_FACTOR_OFFSET]));

        if (!tare_is_valid(tare_offset_counts) || (counts_per_kilogram == 0))
        {
            result = PERSISTENCE_INVALID_VALUE;
        }
        else
        {
            calibration->tare_offset_counts = tare_offset_counts;
            calibration->counts_per_gram =
                (float)((double)counts_per_kilogram / COUNTS_PER_GRAM_SCALE);
        }
    }
    return result;
}

/*
 * Initialise NVS without ever erasing the partition (no silent loss of the
 * calibration or, later, the pre-shared key).
 * Design Document, Chapter 2, Section 2.2, and Chapter 6, Section 6.1,
 * Requirement 2.8.
 */
persistence_result_t persistence_init(void)
{
    persistence_result_t result = PERSISTENCE_STORAGE_ERROR;

    if (nvs_flash_init() == ESP_OK)
    {
        result = PERSISTENCE_OK;
    }
    return result;
}

/*
 * Read the calibration blob with nvs_get_blob() and validate it. Missing
 * data (namespace or key not found) is reported as PERSISTENCE_MISSING, all
 * other problems with their own result; *calibration stays unchanged.
 * Design Document, Chapter 2, Sections 2.1 and 2.2, Chapter 3, and
 * Chapter 6, Section 6.1, Requirement 2.8.
 */
persistence_result_t persistence_calibration_load(weight_calibration_t *calibration)
{
    uint8_t blob[PERSISTENCE_CALIBRATION_BLOB_SIZE] = {0};
    size_t length = PERSISTENCE_CALIBRATION_BLOB_SIZE;
    nvs_handle_t handle = 0U;
    weight_calibration_t loaded = {0, 0.0F};
    esp_err_t error;
    persistence_result_t result;

    if (calibration == NULL)
    {
        return PERSISTENCE_INVALID_ARGUMENT;
    }

    error = nvs_open(PERSISTENCE_NAMESPACE, NVS_READONLY, &handle);
    if (error == ESP_ERR_NVS_NOT_FOUND)
    {
        return PERSISTENCE_MISSING;
    }
    if (error != ESP_OK)
    {
        return PERSISTENCE_STORAGE_ERROR;
    }

    error = nvs_get_blob(handle, PERSISTENCE_CALIBRATION_KEY, blob, &length);
    nvs_close(handle);

    if (error == ESP_ERR_NVS_NOT_FOUND)
    {
        result = PERSISTENCE_MISSING;
    }
    else if (error == ESP_ERR_NVS_INVALID_LENGTH)
    {
        result = PERSISTENCE_INVALID_SIZE;
    }
    else if (error != ESP_OK)
    {
        result = PERSISTENCE_STORAGE_ERROR;
    }
    else if (length != PERSISTENCE_CALIBRATION_BLOB_SIZE)
    {
        result = PERSISTENCE_INVALID_SIZE;
    }
    else
    {
        result = decode_calibration(blob, &loaded);
    }

    if (result == PERSISTENCE_OK)
    {
        *calibration = loaded;
    }
    return result;
}

/*
 * Validate and serialise the calibration, then store it with nvs_set_blob()
 * and nvs_commit(). Nothing is written for invalid values.
 * Design Document, Chapter 2, Sections 2.1 and 2.2, and Chapter 6,
 * Section 6.1, Requirement 2.8.
 */
persistence_result_t persistence_calibration_save(const weight_calibration_t *calibration)
{
    uint8_t blob[PERSISTENCE_CALIBRATION_BLOB_SIZE] = {0};
    nvs_handle_t handle = 0U;
    persistence_result_t result;

    if (calibration == NULL)
    {
        return PERSISTENCE_INVALID_ARGUMENT;
    }

    result = encode_calibration(calibration, blob);
    if (result != PERSISTENCE_OK)
    {
        return result;
    }

    if (nvs_open(PERSISTENCE_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK)
    {
        return PERSISTENCE_STORAGE_ERROR;
    }
    if ((nvs_set_blob(handle, PERSISTENCE_CALIBRATION_KEY, blob,
                      PERSISTENCE_CALIBRATION_BLOB_SIZE) != ESP_OK) ||
        (nvs_commit(handle) != ESP_OK))
    {
        result = PERSISTENCE_STORAGE_ERROR;
    }
    nvs_close(handle);
    return result;
}
