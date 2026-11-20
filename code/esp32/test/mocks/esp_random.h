#ifndef MOCK_ESP_RANDOM_H
#define MOCK_ESP_RANDOM_H

#include <stddef.h>

/*
 * Host stand-in for the ESP-IDF hardware RNG used by the mbedTLS secure
 * channel backend. The test executable provides the implementation.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.9.
 */
void esp_fill_random(void *buf, size_t len);

#endif
