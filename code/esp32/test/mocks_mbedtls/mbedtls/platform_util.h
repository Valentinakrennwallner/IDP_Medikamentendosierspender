#ifndef MOCK_MBEDTLS_PLATFORM_UTIL_H
#define MOCK_MBEDTLS_PLATFORM_UTIL_H

#include <stddef.h>

/*
 * Host stand-in for mbedtls_platform_zeroize() from Mbed TLS, so that the
 * host tests of persistence and psk_commissioning do not need an installed
 * Mbed TLS. Kept in its own directory: the secure-channel interop test uses
 * the real Mbed TLS headers together with ../mocks.
 */
void mbedtls_platform_zeroize(void *buf, size_t len);

/* Number of calls and total bytes since the last reset (for the tests). */
extern unsigned int mock_zeroize_calls;
extern size_t mock_zeroize_bytes;

#endif
