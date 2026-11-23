#ifndef PSK_COMMISSIONING_H
#define PSK_COMMISSIONING_H

/*
 * Serial commissioning of the 256-bit pre-shared key of the ESP32: the key
 * is typed as 64 hexadecimal characters (the same text as psk_hex in the
 * Raspberry Pi configuration file), entered twice, stored in NVS with the
 * persistence component and verified by reloading it. The key is never
 * shown, logged or printed, not even in part: the input is echoed as '*'.
 * All I/O goes through psk_commissioning_io_t, so the dialog is
 * host-testable; the UART binding is psk_commission().
 * Design Document, Chapter 2, Sections 2.1 (Bluetooth communication and
 * encryption) and 2.2, Chapter 5, Section 5.1 (Threat 6), and Chapter 6,
 * Section 6.2, Requirements 4.7 and 4.9.
 */

#include <stdbool.h>
#include <stdint.h>

#include "persistence.h"

/* Number of hexadecimal characters of the key (2 per byte). */
#define PSK_COMMISSIONING_HEX_LENGTH (2U * PERSISTENCE_PSK_SIZE)

typedef enum
{
    /* Key saved and verified by reloading it. */
    PSK_COMMISSIONING_OK = 0,
    /* Cancelled with q, or REPLACE not confirmed; nothing was saved. */
    PSK_COMMISSIONING_CANCELLED = 1,
    /* NVS could not be initialised or written. */
    PSK_COMMISSIONING_STORAGE_ERROR = 2,
    /* The saved key could not be reloaded unchanged. */
    PSK_COMMISSIONING_VERIFY_FAILED = 3,
    /* Missing or incomplete I/O description. */
    PSK_COMMISSIONING_INVALID_ARGUMENT = 4,
    /* Reading from or writing to the serial interface failed. */
    PSK_COMMISSIONING_IO_ERROR = 5
} psk_commissioning_result_t;

/* Hardware access of the dialog (UART and NVS on the ESP32, fakes in the tests). */
typedef struct
{
    /* Wait for one received byte; false on a read error. */
    bool (*read_byte)(uint8_t *byte);
    /* Send a zero-terminated text; false on a write error. */
    bool (*write_text)(const char *text);
    persistence_result_t (*storage_init)(void);
    persistence_result_t (*save)(const uint8_t psk[PERSISTENCE_PSK_SIZE]);
    persistence_result_t (*load)(uint8_t psk[PERSISTENCE_PSK_SIZE]);
} psk_commissioning_io_t;

/*
 * Convert exactly 64 hexadecimal characters (0-9, a-f, A-F; no spaces, no
 * 0x prefix) into the 32-byte key. Returns false and overwrites psk with
 * zeros for any other text. Design Document, Chapter 6, Section 6.2,
 * Requirement 4.7.
 */
bool psk_commissioning_parse_hex(const char *text, uint8_t psk[PERSISTENCE_PSK_SIZE]);

/*
 * Run the commissioning dialog: initialise NVS, ask for REPLACE if a valid
 * key is already stored, read the key twice (masked), reject invalid or
 * obviously non-random keys and mismatches (and ask again), save it and
 * verify it by reloading. q cancels at every prompt. Every copy of the key
 * in memory is overwritten with mbedtls_platform_zeroize() before return.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.7 and 4.9.
 */
psk_commissioning_result_t psk_commissioning_dialog_run(const psk_commissioning_io_t *io);

/*
 * Run the dialog on the console UART with the NVS persistence functions.
 * To be called at start-up when persistence_psk_load() does not return
 * PERSISTENCE_OK (tasks 18 and 21). Design Document, Chapter 6, Section 6.2,
 * Requirement 4.7.
 */
psk_commissioning_result_t psk_commission(void);

#endif
