#include "startup_transfer.h"

#include "../../../protocol/include/startup_message.h"

#include <stdlib.h>

/*
 * Encode the validated daily schedule with current synchronized time and hand
 * it to the authenticated BLE transport. Design Document, Chapter 2, Section
 * 2.1 (Bluetooth communication and intake schedule), Chapter 3 (Raspberry Pi
 * flow), and Chapter 6, Section 6.1, Requirements 1.4 and 1.5.
 */
int ble_send_startup_data(const time_sync_service_t *time_sync,
                          const daily_schedule_t *schedule,
                          secure_send_payload_fn secure_send,
                          void *secure_send_context)
{
    int64_t current_unix_time = 0;
    size_t payload_size = 0U;
    size_t encoded_size = 0U;
    uint8_t *payload;
    int result;

    if ((time_sync == NULL) || !schedule_is_valid_daily(schedule) ||
        (secure_send == NULL) ||
        (time_sync_get_current_time(time_sync, &current_unix_time) != 0) ||
        !startup_message_size(schedule, &payload_size))
    {
        return -1;
    }

    payload = malloc(payload_size);
    if (payload == NULL)
    {
        return -1;
    }

    if (startup_message_encode(schedule, current_unix_time, payload,
                               payload_size, &encoded_size) != 0)
    {
        free(payload);
        return -1;
    }

    result = secure_send(secure_send_context, payload, encoded_size);
    free(payload);
    return result;
}
