#include "startup_transfer.h"

#include "../../../protocol/include/secure_channel.h"

#include <stddef.h>

/*
 * Encode the validated daily schedule with current synchronized time and hand
 * it to the authenticated BLE transport. A schedule that does not fit one
 * protected message is rejected. Design Document, Chapter 2, Section 2.1
 * (Bluetooth communication and intake schedule), Chapter 3 (Raspberry Pi
 * flow), and Chapter 6, Section 6.1, Requirements 1.4 and 1.5.
 */
int ble_send_startup_data(const time_sync_service_t *time_sync,
                          const startup_daily_schedule_t *schedule,
                          secure_send_payload_fn secure_send,
                          void *secure_send_context)
{
    uint8_t payload[SECURE_CHANNEL_MAX_MESSAGE_SIZE];
    int64_t current_unix_time = 0;
    size_t payload_size = 0U;
    int result = -1;

    if ((time_sync != NULL) && (secure_send != NULL) &&
        (time_sync_get_current_time(time_sync, &current_unix_time) == 0) &&
        (startup_message_encode(schedule, current_unix_time, payload,
                                sizeof(payload), &payload_size) == 0))
    {
        result = secure_send(secure_send_context, payload, payload_size);
    }

    return result;
}
