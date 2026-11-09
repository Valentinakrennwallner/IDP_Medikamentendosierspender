#ifndef STARTUP_TRANSFER_H
#define STARTUP_TRANSFER_H

#include "../schedule/schedule.h"
#include "../time_sync/time_sync.h"
#include <stddef.h>
#include <stdint.h>

typedef int (*secure_send_payload_fn)(void *context,
                                      const uint8_t *payload,
                                      size_t payload_size);

/*
 * Send the validated current-day schedule and current synchronized time over
 * the already-authenticated BLE session. The callback must protect the
 * versioned startup message using the secure-channel protocol and handle BLE
 * fragmentation as required. It must consume the payload before returning and
 * return zero only after successful delivery.
 */
int ble_send_startup_data(const time_sync_service_t *time_sync,
                          const daily_schedule_t *schedule,
                          secure_send_payload_fn secure_send,
                          void *secure_send_context);

#endif
