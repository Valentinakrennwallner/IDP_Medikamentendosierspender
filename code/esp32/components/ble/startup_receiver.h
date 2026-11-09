#ifndef STARTUP_RECEIVER_H
#define STARTUP_RECEIVER_H

#include "startup_message.h"

typedef struct
{
    startup_schedule_entry_t *entries;
    size_t entry_capacity;
    size_t entry_count;
    int64_t current_unix_time;
    bool has_valid_startup_data;
} startup_received_data_t;

/*
 * Call only with plaintext that the secure channel has authenticated.
 * The schedule storage is caller-owned and must remain valid while in use.
 */
int ble_startup_receiver_accept_authenticated_payload(
    startup_received_data_t *received_data,
    const uint8_t *payload,
    size_t payload_size);

#endif
