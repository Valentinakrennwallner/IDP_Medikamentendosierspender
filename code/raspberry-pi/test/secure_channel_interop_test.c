/*
 * Host interoperability test between the two secure-channel backends:
 * the gateway OpenSSL backend (src/secure_channel/secure_channel.c, always
 * the client) and the ESP32 mbedTLS backend
 * (../esp32/components/ble/secure_channel_mbedtls.c, always the server),
 * built against the host mbedTLS 3.6 library.
 *
 * Both backends define the same public function names. CMake compiles the
 * mbedTLS backend with compile definitions that rename its public functions
 * to esp32_secure_channel_*, so both can be linked into this executable
 * without changing either backend. esp_fill_random() (ESP-IDF hardware RNG)
 * is provided below from OpenSSL RAND_bytes().
 *
 * Design Document, Chapter 2, Section 2.1 (Bluetooth communication), and
 * Chapter 6, Section 6.2, Requirements 4.1-4.3 and 4.7-4.9.
 */

#include "../../protocol/include/secure_channel.h"
#include "esp_random.h"

#include <limits.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Renamed ESP32 backend functions (same signatures as secure_channel.h). */
int esp32_secure_channel_init(secure_channel_t *channel,
                              secure_channel_role_t role,
                              const uint8_t psk[SECURE_CHANNEL_PSK_SIZE]);
int esp32_secure_channel_process_handshake(secure_channel_t *channel,
                                           const uint8_t *input,
                                           size_t input_size,
                                           uint8_t *output,
                                           size_t output_capacity,
                                           size_t *output_size,
                                           bool *established);
int esp32_secure_channel_seal(secure_channel_t *channel,
                              secure_channel_message_type_t message_type,
                              const uint8_t *plaintext,
                              size_t plaintext_size,
                              uint8_t *output,
                              size_t output_capacity,
                              size_t *output_size);
int esp32_secure_channel_open(secure_channel_t *channel,
                              const uint8_t *input,
                              size_t input_size,
                              secure_channel_message_type_t *message_type,
                              uint8_t *plaintext,
                              size_t plaintext_capacity,
                              size_t *plaintext_size);
void esp32_secure_channel_clear(secure_channel_t *channel);

static bool random_failed = false;

/*
 * Host replacement for the ESP32 hardware RNG used by the mbedTLS backend.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.9.
 */
void esp_fill_random(void *buf, size_t len)
{
    if ((len > (size_t)INT_MAX) ||
        (RAND_bytes((unsigned char *)buf, (int)len) != 1))
    {
        random_failed = true;
        (void)memset(buf, 0, len);
    }
}

/*
 * Run the three-message handshake with the Pi (OpenSSL) as client and the
 * ESP32 (mbedTLS) as server.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.7-4.9.
 */
static int perform_handshake(secure_channel_t *pi, secure_channel_t *esp)
{
    uint8_t client_hello[SECURE_CHANNEL_CLIENT_HELLO_SIZE];
    uint8_t server_hello[SECURE_CHANNEL_SERVER_HELLO_SIZE];
    uint8_t client_finish[SECURE_CHANNEL_CLIENT_FINISH_SIZE];
    size_t client_hello_size = 0U;
    size_t server_hello_size = 0U;
    size_t client_finish_size = 0U;
    size_t unused_size = 0U;
    bool pi_established = false;
    bool esp_established = false;

    if ((secure_channel_start(pi, client_hello, sizeof(client_hello),
                              &client_hello_size) != 0) ||
        (esp32_secure_channel_process_handshake(
             esp, client_hello, client_hello_size, server_hello,
             sizeof(server_hello), &server_hello_size,
             &esp_established) != 0) ||
        esp_established ||
        (secure_channel_process_handshake(
             pi, server_hello, server_hello_size, client_finish,
             sizeof(client_finish), &client_finish_size,
             &pi_established) != 0) ||
        !pi_established ||
        (esp32_secure_channel_process_handshake(
             esp, client_finish, client_finish_size, NULL, 0U, &unused_size,
             &esp_established) != 0) ||
        !esp_established)
    {
        return 1;
    }

    return 0;
}

/*
 * Pi -> ESP32 startup data and ESP32 -> Pi acknowledgement, then a replayed
 * startup frame must be rejected by the ESP32.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.2 and 4.3.
 */
static int test_round_trip_and_replay(void)
{
    static const uint8_t psk[SECURE_CHANNEL_PSK_SIZE] = {
        0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U,
        0x99U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xF0U, 0x01U,
        0x12U, 0x23U, 0x34U, 0x45U, 0x56U, 0x67U, 0x78U, 0x89U,
        0x9AU, 0xABU, 0xBCU, 0xCDU, 0xDEU, 0xEFU, 0xF1U, 0x02U};
    static const uint8_t message[] = "interop startup data";
    secure_channel_t pi;
    secure_channel_t esp;
    secure_channel_message_type_t received_type;
    uint8_t encrypted[SECURE_CHANNEL_OVERHEAD + sizeof(message)];
    uint8_t response[SECURE_CHANNEL_OVERHEAD + sizeof(message)];
    uint8_t decrypted[sizeof(message)];
    size_t encrypted_size = 0U;
    size_t response_size = 0U;
    size_t decrypted_size = 0U;
    int result = 1;

    if ((secure_channel_init(&pi, SECURE_CHANNEL_ROLE_CLIENT, psk) == 0) &&
        (esp32_secure_channel_init(&esp, SECURE_CHANNEL_ROLE_SERVER, psk) ==
         0) &&
        (perform_handshake(&pi, &esp) == 0) &&
        (secure_channel_seal(&pi, SECURE_CHANNEL_MESSAGE_STARTUP, message,
                             sizeof(message), encrypted, sizeof(encrypted),
                             &encrypted_size) == 0) &&
        (esp32_secure_channel_open(&esp, encrypted, encrypted_size,
                                   &received_type, decrypted,
                                   sizeof(decrypted), &decrypted_size) == 0) &&
        (received_type == SECURE_CHANNEL_MESSAGE_STARTUP) &&
        (decrypted_size == sizeof(message)) &&
        (memcmp(message, decrypted, sizeof(message)) == 0) &&
        (esp32_secure_channel_seal(&esp,
                                   SECURE_CHANNEL_MESSAGE_ACKNOWLEDGEMENT,
                                   message, sizeof(message), response,
                                   sizeof(response), &response_size) == 0) &&
        (secure_channel_open(&pi, response, response_size, &received_type,
                             decrypted, sizeof(decrypted),
                             &decrypted_size) == 0) &&
        (received_type == SECURE_CHANNEL_MESSAGE_ACKNOWLEDGEMENT) &&
        (decrypted_size == sizeof(message)) &&
        (memcmp(message, decrypted, sizeof(message)) == 0) &&
        (esp32_secure_channel_open(&esp, encrypted, encrypted_size,
                                   &received_type, decrypted,
                                   sizeof(decrypted), &decrypted_size) != 0))
    {
        result = 0;
    }

    secure_channel_clear(&pi);
    esp32_secure_channel_clear(&esp);
    return result;
}

/*
 * Empty heartbeat ESP32 -> Pi and empty acknowledgement Pi -> ESP32.
 * Design Document, Chapter 6, Section 6.1, Requirement 3.4.
 */
static int test_empty_messages(void)
{
    static const uint8_t psk[SECURE_CHANNEL_PSK_SIZE] = {
        0x21U, 0x32U, 0x43U, 0x54U, 0x65U, 0x76U, 0x87U, 0x98U,
        0xA9U, 0xBAU, 0xCBU, 0xDCU, 0xEDU, 0xFEU, 0x0FU, 0x10U,
        0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U, 0x99U,
        0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xFFU, 0x01U, 0x12U};
    secure_channel_t pi;
    secure_channel_t esp;
    secure_channel_message_type_t received_type;
    uint8_t frame[SECURE_CHANNEL_OVERHEAD];
    uint8_t plaintext[1];
    size_t frame_size = 0U;
    size_t plaintext_size = 1U;
    int result = 1;

    if ((secure_channel_init(&pi, SECURE_CHANNEL_ROLE_CLIENT, psk) == 0) &&
        (esp32_secure_channel_init(&esp, SECURE_CHANNEL_ROLE_SERVER, psk) ==
         0) &&
        (perform_handshake(&pi, &esp) == 0) &&
        (esp32_secure_channel_seal(&esp, SECURE_CHANNEL_MESSAGE_HEARTBEAT,
                                   NULL, 0U, frame, sizeof(frame),
                                   &frame_size) == 0) &&
        (frame_size == SECURE_CHANNEL_OVERHEAD) &&
        (secure_channel_open(&pi, frame, frame_size, &received_type,
                             plaintext, sizeof(plaintext),
                             &plaintext_size) == 0) &&
        (received_type == SECURE_CHANNEL_MESSAGE_HEARTBEAT) &&
        (plaintext_size == 0U) &&
        (secure_channel_seal(&pi, SECURE_CHANNEL_MESSAGE_ACKNOWLEDGEMENT,
                             NULL, 0U, frame, sizeof(frame),
                             &frame_size) == 0) &&
        (esp32_secure_channel_open(&esp, frame, frame_size, &received_type,
                                   plaintext, sizeof(plaintext),
                                   &plaintext_size) == 0) &&
        (received_type == SECURE_CHANNEL_MESSAGE_ACKNOWLEDGEMENT) &&
        (plaintext_size == 0U))
    {
        result = 0;
    }

    secure_channel_clear(&pi);
    esp32_secure_channel_clear(&esp);
    return result;
}

/*
 * A PSK mismatch must make the Pi reject the ESP32 ServerHello.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.9.
 */
static int test_rejects_wrong_psk(void)
{
    static const uint8_t pi_psk[SECURE_CHANNEL_PSK_SIZE] = {
        0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU,
        0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U,
        0x18U, 0x19U, 0x1AU, 0x1BU, 0x1CU, 0x1DU, 0x1EU, 0x1FU};
    static const uint8_t esp_psk[SECURE_CHANNEL_PSK_SIZE] = {
        0x20U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU,
        0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U,
        0x18U, 0x19U, 0x1AU, 0x1BU, 0x1CU, 0x1DU, 0x1EU, 0x1FU};
    secure_channel_t pi;
    secure_channel_t esp;
    uint8_t client_hello[SECURE_CHANNEL_CLIENT_HELLO_SIZE];
    uint8_t server_hello[SECURE_CHANNEL_SERVER_HELLO_SIZE];
    uint8_t client_finish[SECURE_CHANNEL_CLIENT_FINISH_SIZE];
    size_t client_hello_size = 0U;
    size_t server_hello_size = 0U;
    size_t client_finish_size = 0U;
    bool established = false;
    int result = 1;

    if ((secure_channel_init(&pi, SECURE_CHANNEL_ROLE_CLIENT, pi_psk) == 0) &&
        (esp32_secure_channel_init(&esp, SECURE_CHANNEL_ROLE_SERVER,
                                   esp_psk) == 0) &&
        (secure_channel_start(&pi, client_hello, sizeof(client_hello),
                              &client_hello_size) == 0) &&
        (esp32_secure_channel_process_handshake(
             &esp, client_hello, client_hello_size, server_hello,
             sizeof(server_hello), &server_hello_size, &established) == 0) &&
        (secure_channel_process_handshake(
             &pi, server_hello, server_hello_size, client_finish,
             sizeof(client_finish), &client_finish_size, &established) != 0) &&
        !established)
    {
        result = 0;
    }

    secure_channel_clear(&pi);
    esp32_secure_channel_clear(&esp);
    return result;
}

/*
 * A modified authentication tag must be rejected in both directions.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.2 and 4.3.
 */
static int test_rejects_tampered_messages(void)
{
    uint8_t psk[SECURE_CHANNEL_PSK_SIZE] = {0U};
    static const uint8_t message[] = "must remain authentic";
    secure_channel_t pi;
    secure_channel_t esp;
    secure_channel_message_type_t received_type;
    uint8_t to_esp[SECURE_CHANNEL_OVERHEAD + sizeof(message)];
    uint8_t to_pi[SECURE_CHANNEL_OVERHEAD + sizeof(message)];
    uint8_t decrypted[sizeof(message)];
    size_t to_esp_size = 0U;
    size_t to_pi_size = 0U;
    size_t decrypted_size = 0U;
    int result = 1;

    psk[SECURE_CHANNEL_PSK_SIZE - 1U] = 0xA5U;
    if ((secure_channel_init(&pi, SECURE_CHANNEL_ROLE_CLIENT, psk) == 0) &&
        (esp32_secure_channel_init(&esp, SECURE_CHANNEL_ROLE_SERVER, psk) ==
         0) &&
        (perform_handshake(&pi, &esp) == 0) &&
        (secure_channel_seal(&pi, SECURE_CHANNEL_MESSAGE_STARTUP, message,
                             sizeof(message), to_esp, sizeof(to_esp),
                             &to_esp_size) == 0) &&
        (esp32_secure_channel_seal(&esp, SECURE_CHANNEL_MESSAGE_RESULT,
                                   message, sizeof(message), to_pi,
                                   sizeof(to_pi), &to_pi_size) == 0))
    {
        to_esp[to_esp_size - 1U] ^= 0x01U;
        to_pi[to_pi_size - 1U] ^= 0x01U;
        if ((esp32_secure_channel_open(&esp, to_esp, to_esp_size,
                                       &received_type, decrypted,
                                       sizeof(decrypted),
                                       &decrypted_size) != 0) &&
            (secure_channel_open(&pi, to_pi, to_pi_size, &received_type,
                                 decrypted, sizeof(decrypted),
                                 &decrypted_size) != 0))
        {
            result = 0;
        }
    }

    secure_channel_clear(&pi);
    esp32_secure_channel_clear(&esp);
    return result;
}

/*
 * Both backends at the production pool size (1): a second session is
 * refused while the first is active, and a full session works again after
 * clear() released the slot.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1 and 4.9.
 */
static int test_production_pool_size(void)
{
    static const uint8_t psk[SECURE_CHANNEL_PSK_SIZE] = {0xC3U};
    secure_channel_t pi;
    secure_channel_t esp;
    secure_channel_t extra;
    int result = 1;

    if ((secure_channel_init(&pi, SECURE_CHANNEL_ROLE_CLIENT, psk) == 0) &&
        (esp32_secure_channel_init(&esp, SECURE_CHANNEL_ROLE_SERVER, psk) ==
         0) &&
        (secure_channel_init(&extra, SECURE_CHANNEL_ROLE_CLIENT, psk) != 0) &&
        (esp32_secure_channel_init(&extra, SECURE_CHANNEL_ROLE_SERVER, psk) !=
         0))
    {
        secure_channel_clear(&pi);
        esp32_secure_channel_clear(&esp);
        if ((secure_channel_init(&pi, SECURE_CHANNEL_ROLE_CLIENT, psk) == 0) &&
            (esp32_secure_channel_init(&esp, SECURE_CHANNEL_ROLE_SERVER,
                                       psk) == 0) &&
            (perform_handshake(&pi, &esp) == 0))
        {
            result = 0;
        }
    }

    secure_channel_clear(&pi);
    esp32_secure_channel_clear(&esp);
    return result;
}

/*
 * Execute the OpenSSL <-> mbedTLS secure-channel interoperability tests.
 * Design Document, Chapter 6, Section 6.2, Requirements 4.1-4.3 and 4.7-4.9.
 */
int main(void)
{
    if ((test_round_trip_and_replay() != 0) ||
        (test_empty_messages() != 0) ||
        (test_rejects_wrong_psk() != 0) ||
        (test_rejects_tampered_messages() != 0) ||
        (test_production_pool_size() != 0) ||
        random_failed)
    {
        (void)fputs("secure channel interop tests failed\n", stderr);
        return EXIT_FAILURE;
    }

    (void)puts("secure channel interop tests passed");
    return EXIT_SUCCESS;
}
