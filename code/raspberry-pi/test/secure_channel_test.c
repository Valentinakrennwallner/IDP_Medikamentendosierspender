#include "../../protocol/include/secure_channel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int perform_handshake(secure_channel_t *client,
                             secure_channel_t *server)
{
    uint8_t client_hello[SECURE_CHANNEL_CLIENT_HELLO_SIZE];
    uint8_t server_hello[SECURE_CHANNEL_SERVER_HELLO_SIZE];
    uint8_t client_finish[SECURE_CHANNEL_CLIENT_FINISH_SIZE];
    size_t client_hello_size = 0U;
    size_t server_hello_size = 0U;
    size_t client_finish_size = 0U;
    bool client_established = false;
    bool server_established = false;

    if ((secure_channel_start(client, client_hello, sizeof(client_hello),
                              &client_hello_size) != 0) ||
        (secure_channel_process_handshake(
             server, client_hello, client_hello_size, server_hello,
             sizeof(server_hello), &server_hello_size, &server_established) !=
         0) ||
        server_established ||
        (secure_channel_process_handshake(
             client, server_hello, server_hello_size, client_finish,
             sizeof(client_finish), &client_finish_size,
             &client_established) != 0) ||
        !client_established ||
        (secure_channel_process_handshake(
             server, client_finish, client_finish_size, NULL, 0U,
             &server_hello_size, &server_established) != 0) ||
        !server_established)
    {
        return 1;
    }

    return 0;
}

/* Verify handshake, authenticated payload exchange, and replay rejection. */
static int test_authenticated_round_trip(void)
{
    static const uint8_t psk[SECURE_CHANNEL_PSK_SIZE] = {
        0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U, 0x88U,
        0x99U, 0xAAU, 0xBBU, 0xCCU, 0xDDU, 0xEEU, 0xF0U, 0x01U,
        0x12U, 0x23U, 0x34U, 0x45U, 0x56U, 0x67U, 0x78U, 0x89U,
        0x9AU, 0xABU, 0xBCU, 0xCDU, 0xDEU, 0xEFU, 0xF1U, 0x02U};
    static const uint8_t message[] = "authenticated startup data";
    secure_channel_t client;
    secure_channel_t server;
    secure_channel_message_type_t received_type;
    uint8_t encrypted[SECURE_CHANNEL_OVERHEAD + sizeof(message)];
    uint8_t response[SECURE_CHANNEL_OVERHEAD + sizeof(message)];
    uint8_t decrypted[sizeof(message)];
    size_t encrypted_size = 0U;
    size_t response_size = 0U;
    size_t decrypted_size = 0U;

    if ((secure_channel_init(&client, SECURE_CHANNEL_ROLE_CLIENT, psk) != 0) ||
        (secure_channel_init(&server, SECURE_CHANNEL_ROLE_SERVER, psk) != 0) ||
        (perform_handshake(&client, &server) != 0) ||
        (secure_channel_seal(&client, SECURE_CHANNEL_MESSAGE_STARTUP, message,
                             sizeof(message), encrypted, sizeof(encrypted),
                             &encrypted_size) != 0) ||
        (secure_channel_open(&server, encrypted, encrypted_size,
                             &received_type, decrypted, sizeof(decrypted),
                             &decrypted_size) != 0) ||
        (received_type != SECURE_CHANNEL_MESSAGE_STARTUP) ||
        (decrypted_size != sizeof(message)) ||
        (memcmp(message, decrypted, sizeof(message)) != 0) ||
        (secure_channel_seal(&server,
                             SECURE_CHANNEL_MESSAGE_ACKNOWLEDGEMENT, message,
                             sizeof(message), response, sizeof(response),
                             &response_size) != 0) ||
        (secure_channel_open(&client, response, response_size, &received_type,
                             decrypted, sizeof(decrypted),
                             &decrypted_size) != 0) ||
        (received_type != SECURE_CHANNEL_MESSAGE_ACKNOWLEDGEMENT) ||
        (decrypted_size != sizeof(message)) ||
        (memcmp(message, decrypted, sizeof(message)) != 0) ||
        (secure_channel_open(&server, encrypted, encrypted_size,
                             &received_type, decrypted, sizeof(decrypted),
                             &decrypted_size) == 0))
    {
        secure_channel_clear(&client);
        secure_channel_clear(&server);
        return 1;
    }

    secure_channel_clear(&client);
    secure_channel_clear(&server);
    return 0;
}

/* Verify that the PSK authenticates the handshake transcript. */
static int test_rejects_wrong_psk(void)
{
    static const uint8_t client_psk[SECURE_CHANNEL_PSK_SIZE] = {
        0x00U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU,
        0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U,
        0x18U, 0x19U, 0x1AU, 0x1BU, 0x1CU, 0x1DU, 0x1EU, 0x1FU};
    static const uint8_t server_psk[SECURE_CHANNEL_PSK_SIZE] = {
        0x20U, 0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U,
        0x08U, 0x09U, 0x0AU, 0x0BU, 0x0CU, 0x0DU, 0x0EU, 0x0FU,
        0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U,
        0x18U, 0x19U, 0x1AU, 0x1BU, 0x1CU, 0x1DU, 0x1EU, 0x1FU};
    secure_channel_t client;
    secure_channel_t server;
    uint8_t client_hello[SECURE_CHANNEL_CLIENT_HELLO_SIZE];
    uint8_t server_hello[SECURE_CHANNEL_SERVER_HELLO_SIZE];
    uint8_t client_finish[SECURE_CHANNEL_CLIENT_FINISH_SIZE];
    size_t client_hello_size = 0U;
    size_t server_hello_size = 0U;
    size_t client_finish_size = 0U;
    bool established = false;
    int result = 1;

    if ((secure_channel_init(&client, SECURE_CHANNEL_ROLE_CLIENT,
                             client_psk) == 0) &&
        (secure_channel_init(&server, SECURE_CHANNEL_ROLE_SERVER,
                             server_psk) == 0) &&
        (secure_channel_start(&client, client_hello, sizeof(client_hello),
                              &client_hello_size) == 0) &&
        (secure_channel_process_handshake(
             &server, client_hello, client_hello_size, server_hello,
             sizeof(server_hello), &server_hello_size, &established) == 0) &&
        (secure_channel_process_handshake(
             &client, server_hello, server_hello_size, client_finish,
             sizeof(client_finish), &client_finish_size, &established) != 0))
    {
        result = 0;
    }

    secure_channel_clear(&client);
    secure_channel_clear(&server);
    return result;
}

/* Ensure authentication-tag tampering is rejected before plaintext is used. */
static int test_rejects_tampered_message(void)
{
    uint8_t psk[SECURE_CHANNEL_PSK_SIZE] = {0U};
    static const uint8_t message[] = "must remain confidential";
    secure_channel_t client;
    secure_channel_t server;
    secure_channel_message_type_t received_type;
    uint8_t client_hello[SECURE_CHANNEL_CLIENT_HELLO_SIZE];
    uint8_t server_hello[SECURE_CHANNEL_SERVER_HELLO_SIZE];
    uint8_t client_finish[SECURE_CHANNEL_CLIENT_FINISH_SIZE];
    uint8_t encrypted[SECURE_CHANNEL_OVERHEAD + sizeof(message)];
    uint8_t decrypted[sizeof(message)];
    size_t client_hello_size = 0U;
    size_t server_hello_size = 0U;
    size_t client_finish_size = 0U;
    size_t encrypted_size = 0U;
    size_t decrypted_size = 0U;
    bool established = false;
    int result = 1;

    psk[SECURE_CHANNEL_PSK_SIZE - 1U] = 0xA5U;
    if ((secure_channel_init(&client, SECURE_CHANNEL_ROLE_CLIENT, psk) == 0) &&
        (secure_channel_init(&server, SECURE_CHANNEL_ROLE_SERVER, psk) == 0) &&
        (secure_channel_start(&client, client_hello, sizeof(client_hello),
                              &client_hello_size) == 0) &&
        (secure_channel_process_handshake(
             &server, client_hello, client_hello_size, server_hello,
             sizeof(server_hello), &server_hello_size, &established) == 0) &&
        (secure_channel_process_handshake(
             &client, server_hello, server_hello_size, client_finish,
             sizeof(client_finish), &client_finish_size, &established) == 0) &&
        (secure_channel_process_handshake(
             &server, client_finish, client_finish_size, NULL, 0U,
             &server_hello_size, &established) == 0) &&
        (secure_channel_seal(&client, SECURE_CHANNEL_MESSAGE_STARTUP, message,
                             sizeof(message), encrypted, sizeof(encrypted),
                             &encrypted_size) == 0))
    {
        encrypted[encrypted_size - 1U] ^= 0x01U;
        if (secure_channel_open(&server, encrypted, encrypted_size,
                                &received_type, decrypted, sizeof(decrypted),
                                &decrypted_size) != 0)
        {
            result = 0;
        }
    }

    secure_channel_clear(&client);
    secure_channel_clear(&server);
    return result;
}

/* Execute the OpenSSL secure-channel regression tests. */
int main(void)
{
    if ((test_authenticated_round_trip() != 0) ||
        (test_rejects_wrong_psk() != 0) ||
        (test_rejects_tampered_message() != 0))
    {
        (void)fputs("secure channel tests failed\n", stderr);
        return EXIT_FAILURE;
    }

    (void)puts("secure channel tests passed");
    return EXIT_SUCCESS;
}
