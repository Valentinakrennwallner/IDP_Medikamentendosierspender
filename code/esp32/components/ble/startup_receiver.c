#include "startup_receiver.h"

/*
 * Accept a decoded daily schedule and current time only after the caller has
 * authenticated the BLE payload. Design Document, Chapter 2, Section 2.1
 * (Bluetooth communication and dose evaluation), Chapter 3 (ESP32 flow), and
 * Chapter 6, Section 6.1, Requirements 1.4 and 1.5.
 */
int ble_startup_receiver_accept_authenticated_payload(
    startup_received_data_t *received_data,
    const uint8_t *payload,
    size_t payload_size)
{
    size_t received_entry_count = 0U;
    int64_t received_unix_time = 0;

    if ((received_data == NULL) || (received_data->entries == NULL) ||
        (received_data->entry_capacity == 0U))
    {
        return -1;
    }

    received_data->has_valid_startup_data = false;
    received_data->entry_count = 0U;
    received_data->current_unix_time = 0;

    if (startup_message_decode(payload, payload_size, received_data->entries,
                               received_data->entry_capacity,
                               &received_entry_count, &received_unix_time) != 0)
    {
        return -1;
    }

    received_data->entry_count = received_entry_count;
    received_data->current_unix_time = received_unix_time;
    received_data->has_valid_startup_data = true;
    return 0;
}
