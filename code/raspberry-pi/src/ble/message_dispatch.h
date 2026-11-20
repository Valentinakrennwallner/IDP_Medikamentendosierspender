#ifndef MESSAGE_DISPATCH_H
#define MESSAGE_DISPATCH_H

#include "../../../protocol/include/report_message.h"
#include "../../../protocol/include/secure_channel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Gateway dispatcher for authenticated RESULT (4), DAY_END (5) and FAULT (6)
 * messages from the ESP32. The callbacks only report events (status, intake
 * index, fault state); they carry no weights, times or medication names.
 * All four callbacks are required.
 */
typedef void (*message_dispatch_result_fn)(void *context,
                                           report_status_t status,
                                           uint8_t intake_index);
typedef void (*message_dispatch_unassigned_fn)(void *context,
                                               report_status_t status);
typedef void (*message_dispatch_fault_fn)(void *context,
                                          report_status_t status,
                                          report_fault_state_t fault_state);
typedef void (*message_dispatch_day_end_fn)(void *context);

typedef struct
{
    /* RESULT whose intake index is valid for today's schedule (or 0xFF with
     * an unscheduled removal). */
    message_dispatch_result_fn on_result;
    /* RESULT whose intake index is not below today's entry count, or any
     * RESULT before an entry count was set: received, but not assignable
     * to a dose of the schedule the gateway sent. */
    message_dispatch_unassigned_fn on_report_unassigned;
    message_dispatch_fault_fn on_fault;
    message_dispatch_day_end_fn on_day_end;
    void *context;
} message_dispatch_callbacks_t;

/*
 * Caller-owned dispatcher state. Keep it in static storage and pass it as
 * ble_central_config_t.callback_context. It is deliberately not reset on a
 * BLE disconnection, so reports resent after a reconnect are recognised.
 */
typedef struct
{
    message_dispatch_callbacks_t callbacks;
    size_t entry_count;
    uint8_t last_sequence;
    bool report_accepted;
    bool initialized;
} message_dispatch_t;

/* Initialize the dispatcher; all four callbacks must be non-NULL. */
int message_dispatch_init(message_dispatch_t *dispatch,
                          const message_dispatch_callbacks_t *callbacks);

/*
 * Store the entry count of today's schedule after ble_send_startup_data()
 * succeeded. Call it on the BLE worker thread (for example from the
 * connected-event callback) or before ble_central_start().
 */
int message_dispatch_set_entry_count(message_dispatch_t *dispatch,
                                     size_t entry_count);

/*
 * ble_central_message_fn adapter; context is a message_dispatch_t. Returns
 * nonzero for unknown message types and malformed payloads, which closes
 * the BLE session. Duplicate reports return zero without a callback. A
 * valid (empty) DAY_END resets the entry count to 0 before on_day_end; the
 * report sequence state is kept.
 */
int message_dispatch_on_message(void *context,
                                secure_channel_message_type_t message_type,
                                const uint8_t *payload,
                                size_t payload_size);

#endif
