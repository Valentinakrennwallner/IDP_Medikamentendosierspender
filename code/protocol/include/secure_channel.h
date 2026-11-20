#ifndef SECURE_CHANNEL_H
#define SECURE_CHANNEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SECURE_CHANNEL_PSK_SIZE 32U
#define SECURE_CHANNEL_NONCE_SIZE 32U
#define SECURE_CHANNEL_PUBLIC_KEY_SIZE 65U
#define SECURE_CHANNEL_CLIENT_HELLO_SIZE 102U
#define SECURE_CHANNEL_SERVER_HELLO_SIZE 134U
#define SECURE_CHANNEL_CLIENT_FINISH_SIZE 37U
#define SECURE_CHANNEL_TAG_SIZE 16U
#define SECURE_CHANNEL_HEADER_SIZE 20U
#define SECURE_CHANNEL_OVERHEAD \
    (SECURE_CHANNEL_HEADER_SIZE + SECURE_CHANNEL_TAG_SIZE)
#define SECURE_CHANNEL_MAX_MESSAGE_SIZE 4096U

typedef enum
{
    SECURE_CHANNEL_ROLE_CLIENT = 1,
    SECURE_CHANNEL_ROLE_SERVER = 2
} secure_channel_role_t;

typedef enum
{
    SECURE_CHANNEL_MESSAGE_STARTUP = 1,
    SECURE_CHANNEL_MESSAGE_HEARTBEAT = 2,
    SECURE_CHANNEL_MESSAGE_ACKNOWLEDGEMENT = 3,
    SECURE_CHANNEL_MESSAGE_RESULT = 4,
    SECURE_CHANNEL_MESSAGE_DAY_END = 5,
    SECURE_CHANNEL_MESSAGE_FAULT = 6
} secure_channel_message_type_t;

/*
 * Private per-session state. Each backend defines this structure in its own
 * source file and keeps its instances in a fixed static pool of
 * SECURE_CHANNEL_POOL_SIZE slots (default 1, set per backend). No dynamic
 * memory is allocated by the backends' own code (MISRA C:2012 Rule 21.3).
 */
struct secure_channel_backend_state;

typedef struct
{
    struct secure_channel_backend_state *backend_state;
} secure_channel_t;

/*
 * Initialize a role-specific P-256 ECDHE/PSK session. The call takes one free
 * slot from the backend's static pool and copies the PSK into it; it fails
 * (returns -1) when no slot is free. Call secure_channel_clear() before
 * reinitializing an active channel, otherwise its slot stays occupied.
 *
 * secure_channel_init() and secure_channel_clear() are not thread-safe: the
 * pool is not locked. Call them from one task/thread only (ESP32: the NimBLE
 * host task; Raspberry Pi: the BLE worker thread of bluez_central.c).
 */
int secure_channel_init(secure_channel_t *channel,
                        secure_channel_role_t role,
                        const uint8_t psk[SECURE_CHANNEL_PSK_SIZE]);

/* Begin the client handshake by writing the fixed-size ClientHello. The ESP32
 * backend is server-only and does not provide this function. */
int secure_channel_start(secure_channel_t *channel,
                         uint8_t *output,
                         size_t output_capacity,
                         size_t *output_size);

/*
 * Process a complete handshake message. ServerHello and ClientFinish are
 * emitted by the respective call; established becomes true only after the
 * PSK-authenticated transcript and session key are ready.
 */
int secure_channel_process_handshake(secure_channel_t *channel,
                                     const uint8_t *input,
                                     size_t input_size,
                                     uint8_t *output,
                                     size_t output_capacity,
                                     size_t *output_size,
                                     bool *established);

/* Encrypt and authenticate one typed application message. */
int secure_channel_seal(secure_channel_t *channel,
                        secure_channel_message_type_t message_type,
                        const uint8_t *plaintext,
                        size_t plaintext_size,
                        uint8_t *output,
                        size_t output_capacity,
                        size_t *output_size);

/*
 * Authenticate, decrypt, and anti-replay-check one complete protected
 * message. Output message type and plaintext are valid only on success.
 */
int secure_channel_open(secure_channel_t *channel,
                        const uint8_t *input,
                        size_t input_size,
                        secure_channel_message_type_t *message_type,
                        uint8_t *plaintext,
                        size_t plaintext_capacity,
                        size_t *plaintext_size);

/*
 * Erase session key material, release the pool slot, and set the handle to
 * uninitialized. Safe to call on a handle whose backend_state is NULL.
 */
void secure_channel_clear(secure_channel_t *channel);

#endif
