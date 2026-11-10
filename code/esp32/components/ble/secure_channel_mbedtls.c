#include "secure_channel.h"

#include <stdlib.h>
#include <string.h>

#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"
#include "mbedtls/gcm.h"
#include "mbedtls/hkdf.h"
#include "mbedtls/md.h"
#include "mbedtls/platform_util.h"
#include "esp_random.h"

#define CLIENT_HELLO_SIZE SECURE_CHANNEL_CLIENT_HELLO_SIZE
#define SERVER_HELLO_SIZE SECURE_CHANNEL_SERVER_HELLO_SIZE
#define CLIENT_FINISH_SIZE SECURE_CHANNEL_CLIENT_FINISH_SIZE
#define HANDSHAKE_MAC_SIZE 32U
#define SERVER_HELLO_BODY_SIZE (SERVER_HELLO_SIZE - HANDSHAKE_MAC_SIZE)
#define PUBLIC_KEY_OFFSET 37U
#define SERVER_LABEL "meddispenser-server-v1"
#define CLIENT_LABEL "meddispenser-client-v1"
#define LABEL_SIZE 22U
#define TRANSCRIPT_MAX_SIZE (CLIENT_HELLO_SIZE + SERVER_HELLO_SIZE)
#define SESSION_MAGIC UINT32_C(0x53434e31)

typedef struct
{
    uint32_t magic;
    bool started;
    bool established;
    uint8_t psk[SECURE_CHANNEL_PSK_SIZE];
    mbedtls_ecp_group group;
    mbedtls_mpi private_key;
    mbedtls_ecp_point public_key;
    mbedtls_ecp_point peer_public_key;
    uint8_t client_hello[CLIENT_HELLO_SIZE];
    uint8_t server_hello[SERVER_HELLO_SIZE];
    uint8_t transcript[TRANSCRIPT_MAX_SIZE];
    size_t transcript_size;
    uint8_t key[32];
    uint8_t client_prefix[4];
    uint8_t server_prefix[4];
    uint64_t send_sequence;
    uint64_t receive_sequence;
} secure_channel_state_t;

/*
 * Return the private state associated with a public secure-channel handle.
 * Design Document, Chapter 2, Section 2.1 (Bluetooth communication).
 */
static secure_channel_state_t *state_of(secure_channel_t *channel)
{
    return (secure_channel_state_t *)channel->backend_state;
}

/*
 * Encode a 64-bit protocol value in network byte order.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1-4.3.
 */
static void put_u64(uint8_t *p, uint64_t v)
{
    unsigned int i;
    for (i = 0U; i < 8U; ++i)
    {
        p[7U - i] = (uint8_t)(v & UINT64_C(0xff));
        v >>= 8U;
    }
}

/*
 * Decode a 64-bit protocol value from network byte order.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1-4.3.
 */
static uint64_t get_u64(const uint8_t *p)
{
    uint64_t value = 0U;
    unsigned int i;
    for (i = 0U; i < 8U; ++i)
    {
        value = (value << 8U) | p[i];
    }
    return value;
}

/*
 * Encode a 32-bit protocol value in network byte order.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1-4.3.
 */
static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24U);
    p[1] = (uint8_t)(v >> 16U);
    p[2] = (uint8_t)(v >> 8U);
    p[3] = (uint8_t)v;
}

/*
 * Decode a 32-bit protocol value from network byte order.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1-4.3.
 */
static uint32_t get_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) |
           ((uint32_t)p[2] << 8U) | (uint32_t)p[3];
}

/*
 * Compare handshake authentication tags without data-dependent early exit.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.9.
 */
static int constant_time_equal(const uint8_t *left, const uint8_t *right, size_t size)
{
    volatile uint8_t difference = 0U;
    size_t i;
    for (i = 0U; i < size; ++i)
    {
        difference |= (uint8_t)(left[i] ^ right[i]);
    }
    return difference == 0U;
}

/*
 * Supply cryptographic random bytes from the ESP32 hardware RNG.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.9.
 */
static int random_bytes(void *context, unsigned char *output, size_t size)
{
    (void)context;
    esp_fill_random(output, size);
    return 0;
}

/*
 * Accept only defined secure-channel application message types.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.2 and 4.3.
 */
static int valid_type(uint8_t type)
{
    return type >= (uint8_t)SECURE_CHANNEL_MESSAGE_STARTUP &&
           type <= (uint8_t)SECURE_CHANNEL_MESSAGE_FAULT;
}

/*
 * Authenticate handshake transcript data with HMAC-SHA-256 and the PSK.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.9.
 */
static int hmac_sha256(const uint8_t *key, size_t key_size,
                       const uint8_t *data, size_t data_size, uint8_t out[32])
{
    const mbedtls_md_info_t *md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return (md == NULL) ? -1 :
        mbedtls_md_hmac(md, key, key_size, data, data_size, out);
}

/*
 * Append bounded handshake bytes to the transcript used for key derivation.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.9.
 */
static int append_transcript(secure_channel_state_t *s, const uint8_t *data, size_t size)
{
    if ((size > sizeof(s->transcript) - s->transcript_size) || (data == NULL))
    {
        return -1;
    }
    (void)memcpy(&s->transcript[s->transcript_size], data, size);
    s->transcript_size += size;
    return 0;
}

/*
 * Derive directional AES-GCM key material from P-256 ECDH and the PSK.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1, 4.7, and 4.9.
 */
static int derive_key(secure_channel_state_t *s)
{
    uint8_t shared[MBEDTLS_ECP_MAX_BYTES];
    uint8_t info[32];
    uint8_t material[40];
    size_t shared_size = 0U;
    mbedtls_mpi shared_mpi;
    int result = -1;

    mbedtls_mpi_init(&shared_mpi);
    if ((mbedtls_ecp_check_pubkey(&s->group, &s->peer_public_key) != 0) ||
        (mbedtls_ecdh_compute_shared(&s->group, &shared_mpi, &s->peer_public_key,
                                    &s->private_key, random_bytes, NULL) != 0))
    {
        goto cleanup;
    }

    shared_size = (s->group.pbits + 7U) / 8U;
    if (mbedtls_mpi_write_binary(&shared_mpi, shared, shared_size) != 0)
    {
        goto cleanup;
    }

    if ((mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                    s->transcript, s->transcript_size, info) != 0) ||
        (mbedtls_hkdf(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                      s->psk, sizeof(s->psk), shared, shared_size,
                      info, sizeof(info), material, sizeof(material)) != 0))
    {
        goto cleanup;
    }

    (void)memcpy(s->key, material, sizeof(s->key));
    (void)memcpy(s->client_prefix, &material[32], sizeof(s->client_prefix));
    (void)memcpy(s->server_prefix, &material[36], sizeof(s->server_prefix));
    s->send_sequence = 1U;
    s->receive_sequence = 0U;
    s->established = true;
    result = 0;

cleanup:
    mbedtls_mpi_free(&shared_mpi);
    mbedtls_platform_zeroize(shared, sizeof(shared));
    mbedtls_platform_zeroize(info, sizeof(info));
    mbedtls_platform_zeroize(material, sizeof(material));
    return result;
}

/*
 * Generate and export an ephemeral P-256 public key.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.9.
 */
static int make_keypair(secure_channel_state_t *s, uint8_t public_out[65])
{
    size_t written = 0U;
    if ((mbedtls_ecp_gen_keypair(&s->group, &s->private_key, &s->public_key,
                                 random_bytes, NULL) != 0) ||
        (mbedtls_ecp_point_write_binary(&s->group, &s->public_key,
                                        MBEDTLS_ECP_PF_UNCOMPRESSED, &written,
                                        public_out, 65U) != 0) ||
        (written != 65U))
    {
        return -1;
    }
    return 0;
}

/*
 * Initialize the ESP32 secure channel as the ECDHE server (the Pi is always
 * the client) and retain its commissioned PSK.
 * Design Document, Chapter 2, Section 2.1, and Chapter 6, Section 6.2,
 * Requirements 4.1 and 4.7-4.9.
 */
int secure_channel_init(secure_channel_t *channel, secure_channel_role_t role,
                        const uint8_t psk[SECURE_CHANNEL_PSK_SIZE])
{
    secure_channel_state_t *s;
    if ((channel == NULL) || (psk == NULL) || (role != SECURE_CHANNEL_ROLE_SERVER))
    {
        return -1;
    }

    channel->backend_state = NULL;
    s = calloc(1U, sizeof(*s));
    if (s == NULL)
    {
        return -1;
    }
    channel->backend_state = s;
    (void)memcpy(s->psk, psk, sizeof(s->psk));
    mbedtls_ecp_group_init(&s->group);
    mbedtls_mpi_init(&s->private_key);
    mbedtls_ecp_point_init(&s->public_key);
    mbedtls_ecp_point_init(&s->peer_public_key);
    s->magic = SESSION_MAGIC;
    if (mbedtls_ecp_group_load(&s->group, MBEDTLS_ECP_DP_SECP256R1) != 0)
    {
        secure_channel_clear(channel);
        return -1;
    }
    return 0;
}

/*
 * Authenticate the peer handshake and derive the fresh session key.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.7-4.9.
 */
/*
 * Compute the PSK-keyed handshake tag over label || first || second.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.9.
 */
static int handshake_mac(const secure_channel_state_t *s, const char *label,
                         const uint8_t *first, size_t first_size,
                         const uint8_t *second, size_t second_size,
                         uint8_t mac[HANDSHAKE_MAC_SIZE])
{
    uint8_t data[LABEL_SIZE + TRANSCRIPT_MAX_SIZE];
    size_t size = LABEL_SIZE + first_size + second_size;
    int result = -1;

    if (size <= sizeof(data))
    {
        (void)memcpy(data, label, LABEL_SIZE);
        (void)memcpy(&data[LABEL_SIZE], first, first_size);
        if (second_size != 0U)
        {
            (void)memcpy(&data[LABEL_SIZE + first_size], second, second_size);
        }
        result = hmac_sha256(s->psk, sizeof(s->psk), data, size, mac);
        mbedtls_platform_zeroize(data, sizeof(data));
    }
    return result;
}

/* Parse and validate the peer's uncompressed P-256 public key. */
static int read_peer_key(secure_channel_state_t *s, const uint8_t *hello)
{
    return ((mbedtls_ecp_point_read_binary(&s->group, &s->peer_public_key,
                                           &hello[PUBLIC_KEY_OFFSET],
                                           SECURE_CHANNEL_PUBLIC_KEY_SIZE) == 0) &&
            (mbedtls_ecp_check_pubkey(&s->group, &s->peer_public_key) == 0))
               ? 0
               : -1;
}

/*
 * Server: accept the ClientHello and answer with a PSK-tagged ServerHello.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.9.
 */
static int accept_client_hello(secure_channel_state_t *s, const uint8_t *input,
                               uint8_t *output, size_t output_capacity,
                               size_t *output_size)
{
    uint8_t mac[HANDSHAKE_MAC_SIZE];
    int result = -1;

    if ((output == NULL) || (output_capacity < SERVER_HELLO_SIZE))
    {
        return -1;
    }
    (void)memcpy(s->client_hello, input, CLIENT_HELLO_SIZE);
    (void)memcpy(s->server_hello, "MDP1", 4U);
    s->server_hello[4] = 2U;
    esp_fill_random(&s->server_hello[5], SECURE_CHANNEL_NONCE_SIZE);
    if ((read_peer_key(s, input) == 0) &&
        (append_transcript(s, input, CLIENT_HELLO_SIZE) == 0) &&
        (make_keypair(s, &s->server_hello[PUBLIC_KEY_OFFSET]) == 0) &&
        (handshake_mac(s, SERVER_LABEL, s->client_hello, CLIENT_HELLO_SIZE,
                       s->server_hello, SERVER_HELLO_BODY_SIZE, mac) == 0))
    {
        (void)memcpy(&s->server_hello[SERVER_HELLO_BODY_SIZE], mac, sizeof(mac));
        if (append_transcript(s, s->server_hello, SERVER_HELLO_SIZE) == 0)
        {
            (void)memcpy(output, s->server_hello, SERVER_HELLO_SIZE);
            *output_size = SERVER_HELLO_SIZE;
            s->started = true;
            result = 0;
        }
    }
    mbedtls_platform_zeroize(mac, sizeof(mac));
    return result;
}

/*
 * Server: verify the ClientFinish tag and derive the session key.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.7 and 4.9.
 */
static int accept_client_finish(secure_channel_state_t *s, const uint8_t *input)
{
    uint8_t mac[HANDSHAKE_MAC_SIZE];
    int result = -1;

    if ((handshake_mac(s, CLIENT_LABEL, s->transcript, s->transcript_size, NULL,
                       0U, mac) == 0) &&
        constant_time_equal(mac, &input[5], sizeof(mac)) &&
        (derive_key(s) == 0))
    {
        result = 0;
    }
    mbedtls_platform_zeroize(mac, sizeof(mac));
    return result;
}

/*
 * Authenticate the peer handshake and derive the fresh session key.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.7-4.9.
 */
int secure_channel_process_handshake(secure_channel_t *channel,
                                     const uint8_t *input, size_t input_size,
                                     uint8_t *output, size_t output_capacity,
                                     size_t *output_size, bool *established)
{
    secure_channel_state_t *s;
    int result = -1;

    if ((channel == NULL) || (input == NULL) ||
        (output_size == NULL) || (established == NULL))
    {
        return -1;
    }
    *output_size = 0U;
    *established = false;
    s = state_of(channel);
    if ((s == NULL) || (s->magic != SESSION_MAGIC) || (s->established) ||
        (input_size < 5U) ||
        (memcmp(input, "MDP1", 4U) != 0))
    {
        return -1;
    }

    if ((input_size == CLIENT_HELLO_SIZE) && (input[4] == 1U) && !s->started)
    {
        result = accept_client_hello(s, input, output, output_capacity, output_size);
    }
    else if ((input_size == CLIENT_FINISH_SIZE) && (input[4] == 3U) && s->started)
    {
        result = accept_client_finish(s, input);
    }

    *established = (result == 0) && s->established;
    return result;
}

/*
 * Encrypt and authenticate a typed message with AES-256-GCM.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.2 and 4.3.
 */
int secure_channel_seal(secure_channel_t *channel,
                        secure_channel_message_type_t message_type,
                        const uint8_t *plaintext, size_t plaintext_size,
                        uint8_t *output, size_t output_capacity, size_t *output_size)
{
    secure_channel_state_t *s;
    mbedtls_gcm_context gcm;
    uint8_t nonce[12];
    uint8_t empty = 0U;
    const uint8_t *input_data;
    int result = -1;
    if ((channel == NULL) || (output == NULL) || (output_size == NULL) ||
        ((plaintext == NULL) && (plaintext_size != 0U)) || !valid_type((uint8_t)message_type))
    {
        return -1;
    }
    *output_size = 0U;
    s = state_of(channel);
    if ((s == NULL) || (s->magic != SESSION_MAGIC) || !s->established ||
        (plaintext_size > SECURE_CHANNEL_MAX_MESSAGE_SIZE) ||
        (output_capacity < SECURE_CHANNEL_OVERHEAD + plaintext_size) ||
        (s->send_sequence == UINT64_MAX))
    {
        return -1;
    }
    (void)memcpy(output, "MDP1", 4U);
    output[4] = 1U;
    output[5] = (uint8_t)message_type;
    output[6] = 0U;
    output[7] = 0U;
    put_u64(&output[8], s->send_sequence);
    put_u32(&output[16], (uint32_t)plaintext_size);
    if (plaintext_size != 0U)
    {
        (void)memcpy(&output[SECURE_CHANNEL_HEADER_SIZE], plaintext, plaintext_size);
    }
    input_data = (plaintext_size == 0U) ? &empty : plaintext;
    (void)memcpy(nonce, s->server_prefix, 4U);
    put_u64(&nonce[4], s->send_sequence);
    mbedtls_gcm_init(&gcm);
    if ((mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, s->key, 256U) == 0) &&
        (mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plaintext_size,
                                   nonce, sizeof(nonce), output, SECURE_CHANNEL_HEADER_SIZE,
                                   input_data, &output[SECURE_CHANNEL_HEADER_SIZE],
                                   SECURE_CHANNEL_TAG_SIZE,
                                   &output[SECURE_CHANNEL_HEADER_SIZE + plaintext_size]) == 0))
    {
        *output_size = SECURE_CHANNEL_OVERHEAD + plaintext_size;
        ++s->send_sequence;
        result = 0;
    }
    else
    {
        s->established = false;
        if (output_capacity >= SECURE_CHANNEL_OVERHEAD + plaintext_size)
        {
            mbedtls_platform_zeroize(
                output, SECURE_CHANNEL_OVERHEAD + plaintext_size);
        }
    }
    mbedtls_gcm_free(&gcm);
    mbedtls_platform_zeroize(nonce, sizeof(nonce));
    return result;
}

/*
 * Authenticate and decrypt a message, accepting only a fresh sequence number.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.2 and 4.3.
 */
int secure_channel_open(secure_channel_t *channel, const uint8_t *input, size_t input_size,
                        secure_channel_message_type_t *message_type, uint8_t *plaintext,
                        size_t plaintext_capacity, size_t *plaintext_size)
{
    secure_channel_state_t *s;
    mbedtls_gcm_context gcm;
    uint8_t nonce[12];
    uint8_t empty = 0U;
    uint8_t *output_data;
    uint32_t payload_size;
    uint64_t sequence;
    int result = -1;
    if ((channel == NULL) || (input == NULL) || (message_type == NULL) ||
        (plaintext_size == NULL) || ((plaintext == NULL) && (plaintext_capacity != 0U)))
    {
        return -1;
    }
    *plaintext_size = 0U;
    s = state_of(channel);
    if ((s == NULL) || (s->magic != SESSION_MAGIC) || !s->established ||
        (input_size < SECURE_CHANNEL_OVERHEAD) || (memcmp(input, "MDP1", 4U) != 0) ||
        (input[4] != 1U) || !valid_type(input[5]) || (input[6] != 0U) || (input[7] != 0U))
    {
        if ((s != NULL) && (s->magic == SESSION_MAGIC))
        {
            s->established = false;
        }
        return -1;
    }
    sequence = get_u64(&input[8]);
    payload_size = get_u32(&input[16]);
    if ((sequence == 0U) || (sequence <= s->receive_sequence) ||
        (payload_size > SECURE_CHANNEL_MAX_MESSAGE_SIZE) ||
        ((size_t)payload_size + SECURE_CHANNEL_OVERHEAD != input_size) ||
        (plaintext_capacity < payload_size) || ((plaintext == NULL) && (payload_size != 0U)))
    {
        if ((plaintext != NULL) && (payload_size <= plaintext_capacity))
        {
            mbedtls_platform_zeroize(plaintext, payload_size);
        }
        s->established = false;
        return -1;
    }
    (void)memcpy(nonce, s->client_prefix, 4U);
    put_u64(&nonce[4], sequence);
    output_data = (payload_size == 0U) ? &empty : plaintext;
    mbedtls_gcm_init(&gcm);
    if ((mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, s->key, 256U) == 0) &&
        (mbedtls_gcm_auth_decrypt(&gcm, payload_size, nonce, sizeof(nonce), input,
                                  SECURE_CHANNEL_HEADER_SIZE,
                                  &input[SECURE_CHANNEL_HEADER_SIZE + payload_size],
                                  SECURE_CHANNEL_TAG_SIZE,
                                  &input[SECURE_CHANNEL_HEADER_SIZE], output_data) == 0))
    {
        s->receive_sequence = sequence;
        *message_type = (secure_channel_message_type_t)input[5];
        *plaintext_size = payload_size;
        result = 0;
    }
    else
    {
        if ((plaintext != NULL) && (payload_size <= plaintext_capacity))
        {
            mbedtls_platform_zeroize(plaintext, payload_size);
        }
        s->established = false;
    }
    mbedtls_gcm_free(&gcm);
    mbedtls_platform_zeroize(nonce, sizeof(nonce));
    return result;
}

/*
 * Erase ephemeral keys, PSK, and session keys before releasing channel state.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.7-4.9.
 */
void secure_channel_clear(secure_channel_t *channel)
{
    secure_channel_state_t *s;
    if (channel == NULL)
    {
        return;
    }
    s = state_of(channel);
    if (s != NULL)
    {
        mbedtls_ecp_group_free(&s->group);
        mbedtls_mpi_free(&s->private_key);
        mbedtls_ecp_point_free(&s->public_key);
        mbedtls_ecp_point_free(&s->peer_public_key);
        mbedtls_platform_zeroize(s, sizeof(*s));
        free(s);
    }
    channel->backend_state = NULL;
}
