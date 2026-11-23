#include "mbedtls/platform_util.h"

/*
 * Host implementation of mbedtls_platform_zeroize(): writes through a
 * volatile pointer, so the compiler cannot remove the stores, and counts
 * the calls for the tests.
 */
unsigned int mock_zeroize_calls;
size_t mock_zeroize_bytes;

void mbedtls_platform_zeroize(void *buf, size_t len)
{
    volatile unsigned char *bytes = (volatile unsigned char *)buf;
    size_t index;

    mock_zeroize_calls++;
    mock_zeroize_bytes += len;
    for (index = 0U; index < len; index++)
    {
        bytes[index] = 0U;
    }
}
