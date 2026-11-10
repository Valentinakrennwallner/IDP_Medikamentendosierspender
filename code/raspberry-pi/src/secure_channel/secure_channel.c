#include "../../../protocol/include/secure_channel.h"

#include <limits.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>
#include <stdlib.h>
#include <string.h>

#define SECURE_CHANNEL_INTERNAL_MAGIC UINT32_C(0x53434331)
#define SECURE_CHANNEL_PROTOCOL_VERSION 1U
#define SECURE_CHANNEL_KEY_SIZE 32U
#define SECURE_CHANNEL_NONCE_PREFIX_SIZE 4U
#define SECURE_CHANNEL_DERIVED_KEY_SIZE 40U

typedef enum
{
    SECURE_CHANNEL_STATE_IDLE = 0,
    SECURE_CHANNEL_STATE_WAIT_SERVER_HELLO,
    SECURE_CHANNEL_STATE_WAIT_CLIENT_FINISH,
    SECURE_CHANNEL_STATE_ACTIVE,
    SECURE_CHANNEL_STATE_FAILED
} secure_channel_state_t;

typedef struct
{
    uint32_t magic;
    secure_channel_role_t role;
    secure_channel_state_t state;
    EVP_PKEY *ephemeral_key;
    uint8_t psk[SECURE_CHANNEL_PSK_SIZE];
    uint8_t client_hello[SECURE_CHANNEL_CLIENT_HELLO_SIZE];
    uint8_t server_hello[SECURE_CHANNEL_SERVER_HELLO_SIZE];
    uint8_t session_key[SECURE_CHANNEL_KEY_SIZE];
    uint8_t client_nonce_prefix[SECURE_CHANNEL_NONCE_PREFIX_SIZE];
    uint8_t server_nonce_prefix[SECURE_CHANNEL_NONCE_PREFIX_SIZE];
    uint64_t transmit_sequence;
    uint64_t receive_sequence;
} secure_channel_internal_t;

/* Access private heap state after checking its internal marker. */
static secure_channel_internal_t *secure_channel_internal(
    secure_channel_t *channel)
{
    secure_channel_internal_t *internal;

    if (channel == NULL)
    {
        return NULL;
    }

    internal = channel->backend_state;
    if (internal == NULL)
    {
        return NULL;
    }

    if (internal->magic != SECURE_CHANNEL_INTERNAL_MAGIC)
    {
        return NULL;
    }

    return internal;
}

/* Encode a 64-bit protocol number in network byte order. */
static void secure_channel_write_u64(uint8_t *output, uint64_t value)
{
    size_t index;

    for (index = 0U; index < 8U; index++)
    {
        output[index] = (uint8_t)(value >> (56U - (unsigned int)(index * 8U)));
    }
}

/* Decode a 64-bit protocol number in network byte order. */
static uint64_t secure_channel_read_u64(const uint8_t *input)
{
    uint64_t value = 0U;
    size_t index;

    for (index = 0U; index < 8U; index++)
    {
        value = (value << 8U) | (uint64_t)input[index];
    }

    return value;
}

/* Encode a 32-bit protocol number in network byte order. */
static void secure_channel_write_u32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

/* Decode a 32-bit protocol number in network byte order. */
static uint32_t secure_channel_read_u32(const uint8_t *input)
{
    return ((uint32_t)input[0] << 24U) |
           ((uint32_t)input[1] << 16U) |
           ((uint32_t)input[2] << 8U) |
           (uint32_t)input[3];
}

/* Generate an ephemeral P-256 key and export its uncompressed point. */
static int secure_channel_generate_key(EVP_PKEY **key, uint8_t *public_key)
{
    int result = -1;
    EVP_PKEY_CTX *key_context = NULL;
    size_t public_key_size = SECURE_CHANNEL_PUBLIC_KEY_SIZE;
    char curve_name[] = "prime256v1";
    char point_format[] = "uncompressed";
    OSSL_PARAM parameters[3];

    parameters[0] = OSSL_PARAM_construct_utf8_string(
        OSSL_PKEY_PARAM_GROUP_NAME, curve_name, 0U);
    parameters[1] = OSSL_PARAM_construct_utf8_string(
        OSSL_PKEY_PARAM_EC_POINT_CONVERSION_FORMAT, point_format, 0U);
    parameters[2] = OSSL_PARAM_construct_end();

    key_context = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    if ((key_context == NULL) || (EVP_PKEY_keygen_init(key_context) <= 0) ||
        (EVP_PKEY_CTX_set_params(key_context, parameters) <= 0) ||
        (EVP_PKEY_generate(key_context, key) <= 0) ||
        (EVP_PKEY_get_octet_string_param(
             *key, OSSL_PKEY_PARAM_PUB_KEY, public_key, public_key_size,
             &public_key_size) <= 0) ||
        (public_key_size != SECURE_CHANNEL_PUBLIC_KEY_SIZE))
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    EVP_PKEY_CTX_free(key_context);
    return result;
}

/* Import and validate a peer's uncompressed P-256 public key. */
static EVP_PKEY *secure_channel_import_public_key(const uint8_t *public_key)
{
    EVP_PKEY *key = NULL;
    EVP_PKEY_CTX *key_context = NULL;
    char curve_name[] = "prime256v1";
    OSSL_PARAM parameters[3];

    parameters[0] = OSSL_PARAM_construct_utf8_string(
        OSSL_PKEY_PARAM_GROUP_NAME, curve_name, 0U);
    parameters[1] = OSSL_PARAM_construct_octet_string(
        OSSL_PKEY_PARAM_PUB_KEY, (void *)public_key,
        SECURE_CHANNEL_PUBLIC_KEY_SIZE);
    parameters[2] = OSSL_PARAM_construct_end();

    key_context = EVP_PKEY_CTX_new_from_name(NULL, "EC", NULL);
    if ((key_context == NULL) ||
        (EVP_PKEY_fromdata_init(key_context) <= 0) ||
        (EVP_PKEY_fromdata(key_context, &key, EVP_PKEY_PUBLIC_KEY,
                           parameters) <= 0))
    {
        EVP_PKEY_free(key);
        key = NULL;
    }

    EVP_PKEY_CTX_free(key_context);
    return key;
}

/* Compute a 32-byte HMAC-SHA-256 over three transcript parts. */
static int secure_channel_hmac(const uint8_t *key,
                               size_t key_size,
                               const uint8_t *part_one,
                               size_t part_one_size,
                               const uint8_t *part_two,
                               size_t part_two_size,
                               const uint8_t *part_three,
                               size_t part_three_size,
                               uint8_t output[32])
{
    int result = -1;
    size_t output_size = 0U;
    EVP_MAC *mac = EVP_MAC_fetch(NULL, "HMAC", NULL);
    EVP_MAC_CTX *mac_context = NULL;
    char digest_name[] = "SHA256";
    OSSL_PARAM parameters[2];

    parameters[0] = OSSL_PARAM_construct_utf8_string(
        OSSL_MAC_PARAM_DIGEST, digest_name, 0U);
    parameters[1] = OSSL_PARAM_construct_end();

    if (mac == NULL)
    {
        goto cleanup;
    }
    mac_context = EVP_MAC_CTX_new(mac);
    if ((mac_context == NULL) ||
        (EVP_MAC_init(mac_context, key, key_size, parameters) != 1) ||
        ((part_one_size > 0U) &&
         (EVP_MAC_update(mac_context, part_one, part_one_size) != 1)) ||
        ((part_two_size > 0U) &&
         (EVP_MAC_update(mac_context, part_two, part_two_size) != 1)) ||
        ((part_three_size > 0U) &&
         (EVP_MAC_update(mac_context, part_three, part_three_size) != 1)) ||
        (EVP_MAC_final(mac_context, output, &output_size, 32U) != 1) ||
        (output_size != 32U))
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    EVP_MAC_CTX_free(mac_context);
    EVP_MAC_free(mac);
    return result;
}

/* Hash the authenticated handshake transcript with SHA-256. */
static int secure_channel_transcript_hash(
    const secure_channel_internal_t *internal,
    uint8_t output[32])
{
    int result = -1;
    unsigned int output_size = 0U;
    EVP_MD_CTX *digest_context = EVP_MD_CTX_new();

    if ((digest_context != NULL) &&
        (EVP_DigestInit_ex(digest_context, EVP_sha256(), NULL) == 1) &&
        (EVP_DigestUpdate(digest_context, internal->client_hello,
                          sizeof(internal->client_hello)) == 1) &&
        (EVP_DigestUpdate(digest_context, internal->server_hello,
                          sizeof(internal->server_hello)) == 1) &&
        (EVP_DigestFinal_ex(digest_context, output, &output_size) == 1) &&
        (output_size == 32U))
    {
        result = 0;
    }

    EVP_MD_CTX_free(digest_context);
    return result;
}

/* Derive the ECDH secret and session key from an authenticated peer key. */
static int secure_channel_derive_key(secure_channel_internal_t *internal,
                                     const uint8_t *peer_public_key)
{
    int result = -1;
    size_t shared_secret_size = 0U;
    uint8_t shared_secret[SECURE_CHANNEL_PUBLIC_KEY_SIZE];
    uint8_t transcript_hash[32];
    uint8_t derived_key[SECURE_CHANNEL_DERIVED_KEY_SIZE];
    EVP_PKEY *peer_key = NULL;
    EVP_PKEY_CTX *derive_context = NULL;
    EVP_KDF *kdf = NULL;
    EVP_KDF_CTX *kdf_context = NULL;
    char digest_name[] = "SHA256";
    OSSL_PARAM parameters[5];

    peer_key = secure_channel_import_public_key(peer_public_key);
    if (peer_key == NULL)
    {
        goto cleanup;
    }

    derive_context = EVP_PKEY_CTX_new(internal->ephemeral_key, NULL);
    if ((derive_context == NULL) ||
        (EVP_PKEY_derive_init(derive_context) <= 0) ||
        (EVP_PKEY_derive_set_peer(derive_context, peer_key) <= 0) ||
        (EVP_PKEY_derive(derive_context, NULL, &shared_secret_size) <= 0) ||
        (shared_secret_size > sizeof(shared_secret)) ||
        (EVP_PKEY_derive(derive_context, shared_secret, &shared_secret_size) <=
         0) ||
        (secure_channel_transcript_hash(internal, transcript_hash) != 0))
    {
        goto cleanup;
    }

    parameters[0] = OSSL_PARAM_construct_utf8_string(
        OSSL_KDF_PARAM_DIGEST, digest_name, 0U);
    parameters[1] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_SALT, internal->psk, sizeof(internal->psk));
    parameters[2] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_KEY, shared_secret, shared_secret_size);
    parameters[3] = OSSL_PARAM_construct_octet_string(
        OSSL_KDF_PARAM_INFO, transcript_hash, sizeof(transcript_hash));
    parameters[4] = OSSL_PARAM_construct_end();

    kdf = EVP_KDF_fetch(NULL, "HKDF", NULL);
    if (kdf == NULL)
    {
        goto cleanup;
    }
    kdf_context = EVP_KDF_CTX_new(kdf);
    if ((kdf_context == NULL) ||
        (EVP_KDF_derive(kdf_context, derived_key, sizeof(derived_key),
                        parameters) != 1))
    {
        goto cleanup;
    }

    (void)memcpy(internal->session_key, derived_key,
                 sizeof(internal->session_key));
    (void)memcpy(internal->client_nonce_prefix,
                 &derived_key[SECURE_CHANNEL_KEY_SIZE],
                 SECURE_CHANNEL_NONCE_PREFIX_SIZE);
    (void)memcpy(internal->server_nonce_prefix,
                 &derived_key[SECURE_CHANNEL_KEY_SIZE +
                              SECURE_CHANNEL_NONCE_PREFIX_SIZE],
                 SECURE_CHANNEL_NONCE_PREFIX_SIZE);
    result = 0;

cleanup:
    OPENSSL_cleanse(shared_secret, sizeof(shared_secret));
    OPENSSL_cleanse(transcript_hash, sizeof(transcript_hash));
    OPENSSL_cleanse(derived_key, sizeof(derived_key));
    EVP_KDF_CTX_free(kdf_context);
    EVP_KDF_free(kdf);
    EVP_PKEY_CTX_free(derive_context);
    EVP_PKEY_free(peer_key);
    return result;
}

/* Validate the common framing prefix for a handshake message. */
static bool secure_channel_handshake_prefix_valid(const uint8_t *input,
                                                 size_t input_size,
                                                 uint8_t message_type,
                                                 size_t expected_size)
{
    return (input != NULL) && (input_size == expected_size) &&
           (memcmp(input, "MDP1", 4U) == 0) &&
           (input[4] == message_type);
}

/*
 * Initialize a role-specific secure session with the commissioned PSK.
 * Design Document, Chapter 2, Section 2.1, and Chapter 6, Section 6.2,
 * Requirements 4.1 and 4.7–4.9.
 */
int secure_channel_init(secure_channel_t *channel,
                        secure_channel_role_t role,
                        const uint8_t psk[SECURE_CHANNEL_PSK_SIZE])
{
    secure_channel_internal_t *internal;

    if ((channel == NULL) || (psk == NULL) ||
        ((role != SECURE_CHANNEL_ROLE_CLIENT) &&
         (role != SECURE_CHANNEL_ROLE_SERVER)))
    {
        return -1;
    }

    internal = calloc(1U, sizeof(*internal));
    if (internal == NULL)
    {
        return -1;
    }
    internal->magic = SECURE_CHANNEL_INTERNAL_MAGIC;
    internal->role = role;
    internal->state = SECURE_CHANNEL_STATE_IDLE;
    (void)memcpy(internal->psk, psk, SECURE_CHANNEL_PSK_SIZE);
    channel->backend_state = internal;
    return 0;
}

/*
 * Start the Pi-client handshake by emitting an ephemeral P-256 key and nonce.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.7–4.9.
 */
int secure_channel_start(secure_channel_t *channel,
                         uint8_t *output,
                         size_t output_capacity,
                         size_t *output_size)
{
    secure_channel_internal_t *internal = secure_channel_internal(channel);

    if ((internal == NULL) ||
        (internal->role != SECURE_CHANNEL_ROLE_CLIENT) ||
        (internal->state != SECURE_CHANNEL_STATE_IDLE) || (output == NULL) ||
        (output_size == NULL) ||
        (output_capacity < SECURE_CHANNEL_CLIENT_HELLO_SIZE))
    {
        return -1;
    }

    (void)memcpy(internal->client_hello, "MDP1", 4U);
    internal->client_hello[4] = 1U;
    if ((RAND_bytes(&internal->client_hello[5],
                    (int)SECURE_CHANNEL_NONCE_SIZE) != 1) ||
        (secure_channel_generate_key(
             &internal->ephemeral_key,
             &internal->client_hello[37]) != 0))
    {
        internal->state = SECURE_CHANNEL_STATE_FAILED;
        return -1;
    }

    (void)memcpy(output, internal->client_hello,
                 sizeof(internal->client_hello));
    *output_size = sizeof(internal->client_hello);
    internal->state = SECURE_CHANNEL_STATE_WAIT_SERVER_HELLO;
    return 0;
}

/*
 * Authenticate the peer transcript and derive the new session key. Design
 * Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.7–4.9.
 */
int secure_channel_process_handshake(secure_channel_t *channel,
                                     const uint8_t *input,
                                     size_t input_size,
                                     uint8_t *output,
                                     size_t output_capacity,
                                     size_t *output_size,
                                     bool *established)
{
    int result = -1;
    uint8_t expected_mac[32];
    secure_channel_internal_t *internal = secure_channel_internal(channel);
    static const uint8_t server_label[] = "meddispenser-server-v1";
    static const uint8_t client_label[] = "meddispenser-client-v1";

    if ((internal == NULL) || (input == NULL) || (output_size == NULL) ||
        (established == NULL))
    {
        return -1;
    }

    *output_size = 0U;
    *established = false;
    if (internal->role == SECURE_CHANNEL_ROLE_SERVER &&
        (internal->state == SECURE_CHANNEL_STATE_IDLE))
    {
        size_t expected_server_capacity = SECURE_CHANNEL_SERVER_HELLO_SIZE;

        if (!secure_channel_handshake_prefix_valid(
                input, input_size, 1U, SECURE_CHANNEL_CLIENT_HELLO_SIZE) ||
            (output == NULL) || (output_capacity < expected_server_capacity))
        {
            goto failure;
        }

        (void)memcpy(internal->client_hello, input,
                     SECURE_CHANNEL_CLIENT_HELLO_SIZE);
        (void)memcpy(internal->server_hello, "MDP1", 4U);
        internal->server_hello[4] = 2U;
        if ((RAND_bytes(&internal->server_hello[5],
                        (int)SECURE_CHANNEL_NONCE_SIZE) != 1) ||
            (secure_channel_generate_key(
                 &internal->ephemeral_key, &internal->server_hello[37]) != 0))
        {
            goto failure;
        }

        if (secure_channel_hmac(
                internal->psk, sizeof(internal->psk), server_label,
                sizeof(server_label) - 1U, internal->client_hello,
                sizeof(internal->client_hello), internal->server_hello, 102U,
                expected_mac) != 0)
        {
            goto failure;
        }
        (void)memcpy(&internal->server_hello[102], expected_mac,
                     sizeof(expected_mac));
        if (secure_channel_derive_key(internal, &internal->client_hello[37]) !=
            0)
        {
            goto failure;
        }

        (void)memcpy(output, internal->server_hello,
                     sizeof(internal->server_hello));
        *output_size = sizeof(internal->server_hello);
        internal->state = SECURE_CHANNEL_STATE_WAIT_CLIENT_FINISH;
        result = 0;
    }
    else if ((internal->role == SECURE_CHANNEL_ROLE_CLIENT) &&
             (internal->state == SECURE_CHANNEL_STATE_WAIT_SERVER_HELLO))
    {
        uint8_t server_prefix[102];
        uint8_t transcript[sizeof(internal->client_hello) +
                           sizeof(internal->server_hello)];

        if (!secure_channel_handshake_prefix_valid(
                input, input_size, 2U, SECURE_CHANNEL_SERVER_HELLO_SIZE) ||
            (output == NULL) ||
            (output_capacity < SECURE_CHANNEL_CLIENT_FINISH_SIZE))
        {
            goto failure;
        }

        (void)memcpy(internal->server_hello, input,
                     sizeof(internal->server_hello));
        (void)memcpy(server_prefix, internal->server_hello,
                     sizeof(server_prefix));
        if (secure_channel_hmac(
                internal->psk, sizeof(internal->psk), server_label,
                sizeof(server_label) - 1U, internal->client_hello,
                sizeof(internal->client_hello), server_prefix,
                sizeof(server_prefix), expected_mac) != 0)
        {
            goto failure;
        }
        if (CRYPTO_memcmp(expected_mac, &internal->server_hello[102],
                          sizeof(expected_mac)) != 0)
        {
            goto failure;
        }
        if (secure_channel_derive_key(internal, &internal->server_hello[37]) !=
            0)
        {
            goto failure;
        }

        (void)memcpy(transcript, internal->client_hello,
                     sizeof(internal->client_hello));
        (void)memcpy(&transcript[sizeof(internal->client_hello)],
                     internal->server_hello, sizeof(internal->server_hello));
        (void)memcpy(output, "MDP1", 4U);
        output[4] = 3U;
        if (secure_channel_hmac(
                internal->psk, sizeof(internal->psk), client_label,
                sizeof(client_label) - 1U, transcript, sizeof(transcript),
                NULL, 0U, &output[5]) != 0)
        {
            goto failure;
        }

        *output_size = SECURE_CHANNEL_CLIENT_FINISH_SIZE;
        internal->state = SECURE_CHANNEL_STATE_ACTIVE;
        *established = true;
        result = 0;
    }
    else if ((internal->role == SECURE_CHANNEL_ROLE_SERVER) &&
             (internal->state == SECURE_CHANNEL_STATE_WAIT_CLIENT_FINISH))
    {
        uint8_t transcript[sizeof(internal->client_hello) +
                           sizeof(internal->server_hello)];

        if (!secure_channel_handshake_prefix_valid(
                input, input_size, 3U, SECURE_CHANNEL_CLIENT_FINISH_SIZE))
        {
            goto failure;
        }

        (void)memcpy(transcript, internal->client_hello,
                     sizeof(internal->client_hello));
        (void)memcpy(&transcript[sizeof(internal->client_hello)],
                     internal->server_hello, sizeof(internal->server_hello));
        if (secure_channel_hmac(
                internal->psk, sizeof(internal->psk), client_label,
                sizeof(client_label) - 1U, transcript, sizeof(transcript),
                NULL, 0U, expected_mac) != 0)
        {
            goto failure;
        }
        if (CRYPTO_memcmp(expected_mac, &input[5], sizeof(expected_mac)) != 0)
        {
            goto failure;
        }

        internal->state = SECURE_CHANNEL_STATE_ACTIVE;
        *established = true;
        result = 0;
    }

    OPENSSL_cleanse(expected_mac, sizeof(expected_mac));
    return result;

failure:
    OPENSSL_cleanse(expected_mac, sizeof(expected_mac));
    internal->state = SECURE_CHANNEL_STATE_FAILED;
    return -1;
}

/* Construct the role-specific AES-GCM nonce for a sequence number. */
static void secure_channel_make_nonce(
    const secure_channel_internal_t *internal,
    bool transmit,
    uint64_t sequence,
    uint8_t nonce[12])
{
    const uint8_t *prefix;

    if (transmit)
    {
        prefix = (internal->role == SECURE_CHANNEL_ROLE_CLIENT)
                     ? internal->client_nonce_prefix
                     : internal->server_nonce_prefix;
    }
    else
    {
        prefix = (internal->role == SECURE_CHANNEL_ROLE_CLIENT)
                     ? internal->server_nonce_prefix
                     : internal->client_nonce_prefix;
    }

    (void)memcpy(nonce, prefix, SECURE_CHANNEL_NONCE_PREFIX_SIZE);
    secure_channel_write_u64(&nonce[SECURE_CHANNEL_NONCE_PREFIX_SIZE],
                             sequence);
}

/*
 * Seal one application message using AES-256-GCM and a monotonic sequence.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.2 and 4.3.
 */
int secure_channel_seal(secure_channel_t *channel,
                        secure_channel_message_type_t message_type,
                        const uint8_t *plaintext,
                        size_t plaintext_size,
                        uint8_t *output,
                        size_t output_capacity,
                        size_t *output_size)
{
    int result = -1;
    int output_length = 0;
    int final_length = 0;
    uint8_t nonce[12];
    EVP_CIPHER_CTX *cipher_context = NULL;
    secure_channel_internal_t *internal = secure_channel_internal(channel);

    if ((internal == NULL) || (internal->state != SECURE_CHANNEL_STATE_ACTIVE) ||
        (output == NULL) || (output_size == NULL) ||
        ((plaintext == NULL) && (plaintext_size != 0U)) ||
        (plaintext_size > SECURE_CHANNEL_MAX_MESSAGE_SIZE) ||
        (plaintext_size > (size_t)INT_MAX) ||
        (output_capacity <
         (SECURE_CHANNEL_OVERHEAD + plaintext_size)) ||
        ((message_type < SECURE_CHANNEL_MESSAGE_STARTUP) ||
         (message_type > SECURE_CHANNEL_MESSAGE_FAULT)) ||
        (internal->transmit_sequence == UINT64_MAX))
    {
        return -1;
    }

    internal->transmit_sequence++;
    (void)memcpy(output, "MDP1", 4U);
    output[4] = SECURE_CHANNEL_PROTOCOL_VERSION;
    output[5] = (uint8_t)message_type;
    output[6] = 0U;
    output[7] = 0U;
    secure_channel_write_u64(&output[8], internal->transmit_sequence);
    secure_channel_write_u32(&output[16], (uint32_t)plaintext_size);
    secure_channel_make_nonce(internal, true, internal->transmit_sequence,
                              nonce);

    cipher_context = EVP_CIPHER_CTX_new();
    if ((cipher_context == NULL) ||
        (EVP_EncryptInit_ex(cipher_context, EVP_aes_256_gcm(), NULL, NULL,
                            NULL) != 1) ||
        (EVP_CIPHER_CTX_ctrl(cipher_context, EVP_CTRL_GCM_SET_IVLEN,
                             (int)sizeof(nonce), NULL) != 1) ||
        (EVP_EncryptInit_ex(cipher_context, NULL, NULL, internal->session_key,
                            nonce) != 1) ||
        (EVP_EncryptUpdate(cipher_context, NULL, &output_length, output,
                           (int)SECURE_CHANNEL_HEADER_SIZE) != 1) ||
        ((plaintext_size > 0U) &&
         (EVP_EncryptUpdate(
              cipher_context, &output[SECURE_CHANNEL_HEADER_SIZE],
              &output_length, plaintext, (int)plaintext_size) != 1)) ||
        (EVP_EncryptFinal_ex(
             cipher_context,
             &output[SECURE_CHANNEL_HEADER_SIZE + plaintext_size],
             &final_length) != 1) ||
        ((size_t)(output_length + final_length) != plaintext_size) ||
        (EVP_CIPHER_CTX_ctrl(
             cipher_context, EVP_CTRL_GCM_GET_TAG,
             (int)SECURE_CHANNEL_TAG_SIZE,
             &output[SECURE_CHANNEL_HEADER_SIZE + plaintext_size]) != 1))
    {
        internal->state = SECURE_CHANNEL_STATE_FAILED;
        goto cleanup;
    }

    *output_size = SECURE_CHANNEL_OVERHEAD + plaintext_size;
    result = 0;

cleanup:
    OPENSSL_cleanse(nonce, sizeof(nonce));
    EVP_CIPHER_CTX_free(cipher_context);
    return result;
}

/*
 * Authenticate, decrypt, and reject replayed application messages. Design
 * Document, Chapter 6, Section 6.2, Requirements 4.2 and 4.3.
 */
int secure_channel_open(secure_channel_t *channel,
                        const uint8_t *input,
                        size_t input_size,
                        secure_channel_message_type_t *message_type,
                        uint8_t *plaintext,
                        size_t plaintext_capacity,
                        size_t *plaintext_size)
{
    int result = -1;
    int output_length = 0;
    int final_length = 0;
    uint32_t encoded_size;
    uint64_t sequence;
    uint8_t nonce[12];
    uint8_t empty_plaintext = 0U;
    EVP_CIPHER_CTX *cipher_context = NULL;
    secure_channel_internal_t *internal = secure_channel_internal(channel);

    if ((internal == NULL) || (internal->state != SECURE_CHANNEL_STATE_ACTIVE) ||
        (input == NULL) || (message_type == NULL) ||
        (plaintext_size == NULL) ||
        (input_size < SECURE_CHANNEL_OVERHEAD))
    {
        return -1;
    }

    encoded_size = secure_channel_read_u32(&input[16]);
    sequence = secure_channel_read_u64(&input[8]);
    if ((memcmp(input, "MDP1", 4U) != 0) ||
        (input[4] != SECURE_CHANNEL_PROTOCOL_VERSION) ||
        (input[6] != 0U) || (input[7] != 0U) ||
        (input[5] < (uint8_t)SECURE_CHANNEL_MESSAGE_STARTUP) ||
        (input[5] > (uint8_t)SECURE_CHANNEL_MESSAGE_FAULT) ||
        (encoded_size > SECURE_CHANNEL_MAX_MESSAGE_SIZE) ||
        (input_size != (SECURE_CHANNEL_OVERHEAD + (size_t)encoded_size)) ||
        ((size_t)encoded_size > plaintext_capacity) ||
        ((encoded_size > 0U) && (plaintext == NULL)) ||
        (sequence == 0U) || (sequence <= internal->receive_sequence))
    {
        internal->state = SECURE_CHANNEL_STATE_FAILED;
        return -1;
    }

    secure_channel_make_nonce(internal, false, sequence, nonce);
    cipher_context = EVP_CIPHER_CTX_new();
    if ((cipher_context == NULL) ||
        (EVP_DecryptInit_ex(cipher_context, EVP_aes_256_gcm(), NULL, NULL,
                            NULL) != 1) ||
        (EVP_CIPHER_CTX_ctrl(cipher_context, EVP_CTRL_GCM_SET_IVLEN,
                             (int)sizeof(nonce), NULL) != 1) ||
        (EVP_DecryptInit_ex(cipher_context, NULL, NULL, internal->session_key,
                            nonce) != 1) ||
        (EVP_DecryptUpdate(cipher_context, NULL, &output_length, input,
                           (int)SECURE_CHANNEL_HEADER_SIZE) != 1) ||
        ((encoded_size > 0U) &&
         (EVP_DecryptUpdate(
              cipher_context, plaintext, &output_length,
              &input[SECURE_CHANNEL_HEADER_SIZE], (int)encoded_size) != 1)) ||
        (EVP_CIPHER_CTX_ctrl(
             cipher_context, EVP_CTRL_GCM_SET_TAG,
             (int)SECURE_CHANNEL_TAG_SIZE,
             (void *)&input[SECURE_CHANNEL_HEADER_SIZE + encoded_size]) != 1) ||
        (EVP_DecryptFinal_ex(
             cipher_context,
             (encoded_size > 0U) ? &plaintext[encoded_size]
                                 : &empty_plaintext,
             &final_length) != 1) ||
        ((uint32_t)(output_length + final_length) != encoded_size))
    {
        if ((plaintext != NULL) && (encoded_size <= plaintext_capacity))
        {
            OPENSSL_cleanse(plaintext, encoded_size);
        }
        internal->state = SECURE_CHANNEL_STATE_FAILED;
        goto cleanup;
    }

    internal->receive_sequence = sequence;
    *message_type = (secure_channel_message_type_t)input[5];
    *plaintext_size = (size_t)encoded_size;
    result = 0;

cleanup:
    OPENSSL_cleanse(nonce, sizeof(nonce));
    EVP_CIPHER_CTX_free(cipher_context);
    return result;
}

/*
 * Erase ephemeral keys, PSK, and session keys before releasing the channel.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.7–4.9.
 */
void secure_channel_clear(secure_channel_t *channel)
{
    secure_channel_internal_t *internal = secure_channel_internal(channel);

    if (internal != NULL)
    {
        EVP_PKEY_free(internal->ephemeral_key);
        OPENSSL_cleanse(internal, sizeof(*internal));
        free(internal);
        channel->backend_state = NULL;
    }
}
