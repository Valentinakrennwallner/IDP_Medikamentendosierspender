#include "telegram.h"

#include <curl/curl.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TELEGRAM_BOT_TOKEN_MAX_LENGTH 128U
#define TELEGRAM_CHAT_ID_MAX_LENGTH 128U
#define TELEGRAM_MESSAGE_MAX_LENGTH 4096U
#define TELEGRAM_RESPONSE_CAPACITY 4096U
#define TELEGRAM_REQUEST_TIMEOUT_SECONDS 20L

typedef struct
{
    time_t verification_time;
    char response[TELEGRAM_RESPONSE_CAPACITY];
    size_t response_length;
    bool response_overflow;
} telegram_request_context_t;

static pthread_once_t curl_initialization_once = PTHREAD_ONCE_INIT;
static CURLcode curl_initialization_result = CURLE_FAILED_INIT;

/*
 * Initialize libcurl once for concurrent gateway notification threads.
 * Design Document, Chapter 2, Section 2.2 (Raspberry Pi libraries).
 */
static void telegram_initialize_curl(void)
{
    curl_initialization_result = curl_global_init(CURL_GLOBAL_DEFAULT);
}

/*
 * Restrict bot tokens to the documented token alphabet so they cannot alter
 * the HTTPS request path.
 */
static bool telegram_token_is_valid(const char *bot_token)
{
    size_t index;
    size_t length;
    bool separator_found = false;

    if (bot_token == NULL)
    {
        return false;
    }

    length = strlen(bot_token);
    if ((length == 0U) || (length > TELEGRAM_BOT_TOKEN_MAX_LENGTH))
    {
        return false;
    }

    for (index = 0U; index < length; index++)
    {
        char character = bot_token[index];
        bool is_digit = (character >= '0') && (character <= '9');
        bool is_upper = (character >= 'A') && (character <= 'Z');
        bool is_lower = (character >= 'a') && (character <= 'z');

        if (character == ':')
        {
            if (separator_found || (index == 0U) || ((index + 1U) >= length))
            {
                return false;
            }
            separator_found = true;
        }
        else if (separator_found)
        {
            if (!is_digit && !is_upper && !is_lower &&
                (character != '_') && (character != '-'))
            {
                return false;
            }
        }
        else if (!is_digit)
        {
            return false;
        }
    }

    return separator_found;
}

/*
 * Apply the caller's trusted epoch to OpenSSL certificate validity checks.
 * Design Document, Chapter 2, Section 2.1 (Telegram connection) and Chapter
 * 6, Section 6.2, Requirement 4.10.
 */
static CURLcode telegram_set_certificate_time(CURL *curl_handle,
                                               void *ssl_context,
                                               void *user_data)
{
    SSL_CTX *context = (SSL_CTX *)ssl_context;
    telegram_request_context_t *request_context =
        (telegram_request_context_t *)user_data;
    X509_VERIFY_PARAM *verify_parameters;
    (void)curl_handle;

    if ((context == NULL) || (request_context == NULL))
    {
        return CURLE_SSL_CERTPROBLEM;
    }

    verify_parameters = SSL_CTX_get0_param(context);
    if (verify_parameters == NULL)
    {
        return CURLE_SSL_CERTPROBLEM;
    }

    X509_VERIFY_PARAM_set_time(verify_parameters, request_context->verification_time);
    return CURLE_OK;
}

/*
 * Bound the response body in memory and reject responses that exceed the cap.
 */
static size_t telegram_receive_response(char *data,
                                        size_t size,
                                        size_t count,
                                        void *user_data)
{
    telegram_request_context_t *request_context =
        (telegram_request_context_t *)user_data;
    size_t data_size;
    size_t available;

    if ((request_context == NULL) ||
        ((size != 0U) && (count > (SIZE_MAX / size))))
    {
        return 0U;
    }

    data_size = size * count;
    available = sizeof(request_context->response) -
                request_context->response_length - 1U;
    if (data_size > available)
    {
        request_context->response_overflow = true;
        return 0U;
    }

    if (data_size > 0U)
    {
        (void)memcpy(&request_context->response[request_context->response_length],
                     data, data_size);
    }
    request_context->response_length += data_size;
    request_context->response[request_context->response_length] = '\0';
    return data_size;
}

/*
 * Accept a successful Telegram Bot API JSON response without logging its body.
 */
static bool telegram_response_was_accepted(const char *response)
{
    const char *value = response;

    if (value == NULL)
    {
        return false;
    }

    while ((*value == ' ') || (*value == '\t') ||
           (*value == '\r') || (*value == '\n'))
    {
        value++;
    }

    if (*value != '{')
    {
        return false;
    }
    value++;

    while ((*value == ' ') || (*value == '\t') ||
           (*value == '\r') || (*value == '\n'))
    {
        value++;
    }

    if (strncmp(value, "\"ok\"", 4U) != 0)
    {
        return false;
    }
    value += 4;

    while ((*value == ' ') || (*value == '\t') ||
           (*value == '\r') || (*value == '\n'))
    {
        value++;
    }

    if (*value != ':')
    {
        return false;
    }
    value++;

    while ((*value == ' ') || (*value == '\t') ||
           (*value == '\r') || (*value == '\n'))
    {
        value++;
    }

    if (strncmp(value, "true", 4U) != 0)
    {
        return false;
    }
    value += 4;

    while ((*value == ' ') || (*value == '\t') ||
           (*value == '\r') || (*value == '\n'))
    {
        value++;
    }

    return (*value == ',') || (*value == '}');
}

/*
 * Release libcurl-owned request data without exposing the token or message.
 */
static void telegram_release_request(CURL *curl_handle,
                                    struct curl_slist *headers,
                                    char *url,
                                    char *encoded_chat_id,
                                    char *encoded_message,
                                    char *post_fields)
{
    if (curl_handle != NULL)
    {
        curl_easy_cleanup(curl_handle);
    }
    if (headers != NULL)
    {
        curl_slist_free_all(headers);
    }
    free(url);
    curl_free(encoded_chat_id);
    curl_free(encoded_message);
    free(post_fields);
}

/*
 * Send a Telegram text message over HTTPS, verifying the server certificate
 * at the supplied synchronized or saved epoch. Only the reminder/warning text,
 * chat identifier, and token are sent. Design Document, Chapter 2, Section
 * 2.1 (Telegram connection) and Chapter 6, Sections 6.1–6.2, Requirements
 * 1.2, 1.10–1.11, 2.6, 2.10, 2.13, 3.5, 4.5, and 4.10.
 */
telegram_result_t telegram_send_message(const char *bot_token,
                                        const char *chat_id,
                                        const char *message,
                                        int64_t certificate_time_unix)
{
    CURL *curl_handle = NULL;
    struct curl_slist *headers = NULL;
    char *encoded_chat_id = NULL;
    char *encoded_message = NULL;
    char *url = NULL;
    char *post_fields = NULL;
    const curl_version_info_data *version_info;
    telegram_request_context_t request_context = {0};
    long response_code = 0L;
    time_t verification_time;
    size_t url_capacity;
    size_t post_capacity;
    size_t token_length;
    size_t chat_length;
    size_t message_length;
    int format_result;
    CURLcode curl_result;
    CURLcode option_result;
    telegram_result_t result = TELEGRAM_RESULT_REQUEST_FAILED;

    if (!telegram_token_is_valid(bot_token) || (chat_id == NULL) ||
        (message == NULL) || (chat_id[0] == '\0') || (message[0] == '\0') ||
        (strlen(chat_id) > TELEGRAM_CHAT_ID_MAX_LENGTH) ||
        (strlen(message) > TELEGRAM_MESSAGE_MAX_LENGTH) ||
        (certificate_time_unix <= 0))
    {
        return TELEGRAM_RESULT_INVALID_ARGUMENT;
    }

    verification_time = (time_t)certificate_time_unix;
    if (((int64_t)verification_time != certificate_time_unix) ||
        (verification_time <= (time_t)0))
    {
        return TELEGRAM_RESULT_INVALID_ARGUMENT;
    }

    if (pthread_once(&curl_initialization_once, telegram_initialize_curl) != 0)
    {
        return TELEGRAM_RESULT_INITIALIZATION_FAILED;
    }
    if (curl_initialization_result != CURLE_OK)
    {
        return TELEGRAM_RESULT_INITIALIZATION_FAILED;
    }

    version_info = curl_version_info(CURLVERSION_NOW);
    if ((version_info == NULL) || (version_info->ssl_version == NULL) ||
        (strncmp(version_info->ssl_version, "OpenSSL/", 8U) != 0))
    {
        return TELEGRAM_RESULT_TLS_BACKEND_UNSUPPORTED;
    }

    curl_handle = curl_easy_init();
    if (curl_handle == NULL)
    {
        return TELEGRAM_RESULT_INITIALIZATION_FAILED;
    }

    token_length = strlen(bot_token);
    chat_length = strlen(chat_id);
    message_length = strlen(message);
    if ((token_length > (SIZE_MAX - 64U)) ||
        (chat_length > ((SIZE_MAX - 32U) / 3U)) ||
        (message_length > ((SIZE_MAX - 32U) / 3U)))
    {
        result = TELEGRAM_RESULT_INVALID_ARGUMENT;
        goto cleanup;
    }

    url_capacity = token_length + 64U;
    post_capacity = (chat_length * 3U) + (message_length * 3U) + 32U;
    url = malloc(url_capacity);
    post_fields = malloc(post_capacity);
    if ((url == NULL) || (post_fields == NULL))
    {
        result = TELEGRAM_RESULT_INITIALIZATION_FAILED;
        goto cleanup;
    }

    format_result = snprintf(url, url_capacity,
                             "https://api.telegram.org/bot%s/sendMessage",
                             bot_token);
    if ((format_result < 0) || ((size_t)format_result >= url_capacity))
    {
        goto cleanup;
    }

    encoded_chat_id = curl_easy_escape(curl_handle, chat_id, (int)chat_length);
    encoded_message = curl_easy_escape(curl_handle, message, (int)message_length);
    if ((encoded_chat_id == NULL) || (encoded_message == NULL))
    {
        result = TELEGRAM_RESULT_INITIALIZATION_FAILED;
        goto cleanup;
    }

    format_result = snprintf(post_fields, post_capacity, "chat_id=%s&text=%s",
                             encoded_chat_id, encoded_message);
    if ((format_result < 0) || ((size_t)format_result >= post_capacity))
    {
        goto cleanup;
    }

    request_context.verification_time = verification_time;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    if (headers == NULL)
    {
        result = TELEGRAM_RESULT_INITIALIZATION_FAILED;
        goto cleanup;
    }

#define TELEGRAM_SETOPT(option, value)                                      \
    do                                                                      \
    {                                                                       \
        option_result = curl_easy_setopt(curl_handle, (option), (value));   \
        if (option_result != CURLE_OK)                                      \
        {                                                                   \
            goto cleanup;                                                   \
        }                                                                   \
    } while (0)

    TELEGRAM_SETOPT(CURLOPT_URL, url);
    TELEGRAM_SETOPT(CURLOPT_POST, 1L);
    TELEGRAM_SETOPT(CURLOPT_POSTFIELDS, post_fields);
    TELEGRAM_SETOPT(CURLOPT_HTTPHEADER, headers);
    TELEGRAM_SETOPT(CURLOPT_SSL_VERIFYPEER, 1L);
    TELEGRAM_SETOPT(CURLOPT_SSL_VERIFYHOST, 2L);
    TELEGRAM_SETOPT(CURLOPT_FOLLOWLOCATION, 0L);
    TELEGRAM_SETOPT(CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    TELEGRAM_SETOPT(CURLOPT_CONNECTTIMEOUT, TELEGRAM_REQUEST_TIMEOUT_SECONDS);
    TELEGRAM_SETOPT(CURLOPT_TIMEOUT, TELEGRAM_REQUEST_TIMEOUT_SECONDS);
    TELEGRAM_SETOPT(CURLOPT_NOSIGNAL, 1L);
    TELEGRAM_SETOPT(CURLOPT_SSL_CTX_FUNCTION, telegram_set_certificate_time);
    TELEGRAM_SETOPT(CURLOPT_SSL_CTX_DATA, &request_context);
    TELEGRAM_SETOPT(CURLOPT_WRITEFUNCTION, telegram_receive_response);
    TELEGRAM_SETOPT(CURLOPT_WRITEDATA, &request_context);
    TELEGRAM_SETOPT(CURLOPT_USERAGENT, "medication-dispenser-gateway/1.0");

#undef TELEGRAM_SETOPT

    curl_result = curl_easy_perform(curl_handle);
    if ((curl_result != CURLE_OK) || request_context.response_overflow)
    {
        goto cleanup;
    }

    curl_result = curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &response_code);
    if (curl_result != CURLE_OK)
    {
        goto cleanup;
    }

    if ((response_code < 200L) || (response_code >= 300L) ||
        !telegram_response_was_accepted(request_context.response))
    {
        result = TELEGRAM_RESULT_API_REJECTED;
        goto cleanup;
    }

    result = TELEGRAM_RESULT_OK;

cleanup:
    telegram_release_request(curl_handle, headers, url, encoded_chat_id,
                             encoded_message, post_fields);
    return result;
}

/*
 * Use synchronized time or the persisted last-valid epoch for certificate
 * validation before sending. Design Document, Chapter 2, Section 2.1
 * (Telegram connection) and Chapter 6, Section 6.2, Requirement 4.10.
 */
telegram_result_t telegram_send_message_with_time(
    const char *bot_token,
    const char *chat_id,
    const char *message,
    const time_sync_service_t *time_sync)
{
    int64_t verification_time = 0;

    if (time_sync == NULL)
    {
        return TELEGRAM_RESULT_INVALID_ARGUMENT;
    }

    if (time_sync_get_certificate_verification_time(time_sync,
                                                   &verification_time) != 0)
    {
        return TELEGRAM_RESULT_TRUSTED_TIME_UNAVAILABLE;
    }

    return telegram_send_message(bot_token, chat_id, message, verification_time);
}

/*
 * Send the minimal dispenser-unreachable alert when the heartbeat monitor
 * times out. Design Document, Chapter 6, Section 6.1, Requirement 3.5.
 */
int telegram_heartbeat_timeout_alert(void *context)
{
    const telegram_heartbeat_alert_context_t *alert_context = context;

    if (alert_context == NULL)
    {
        return -1;
    }

    if (telegram_send_message_with_time(
            alert_context->bot_token, alert_context->chat_id,
            "Dispenser not reachable", alert_context->time_sync) !=
        TELEGRAM_RESULT_OK)
    {
        return -1;
    }

    return 0;
}
