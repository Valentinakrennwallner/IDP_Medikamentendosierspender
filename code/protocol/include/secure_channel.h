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

typedef struct
{
    void *backend_state;
} secure_channel_t;

/*
 * Initialize a role-specific P-256 ECDHE/PSK session. The channel object is
 * initialized by this call, and the PSK is copied into its private state.
 * Call secure_channel_clear() before reinitializing an active channel.
 */
int secure_channel_init(secure_channel_t *channel,
                        secure_channel_role_t role,
                        const uint8_t psk[SECURE_CHANNEL_PSK_SIZE]);

/* Begin the client handshake by writing the fixed-size ClientHello. */
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

/* Erase session key material and return the state to uninitialized. */
void secure_channel_clear(secure_channel_t *channel);

#endif
