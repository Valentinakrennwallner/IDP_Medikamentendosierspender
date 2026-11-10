#include "../src/ble/startup_transfer.h"
#include "../../protocol/include/secure_channel.h"
#include "../../protocol/include/startup_message.h"
#include "../../esp32/components/ble/startup_receiver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int sync_result;
    int clock_result;
    int alert_result;
    int transport_result;
    int send_count;
    int64_t unix_time;
    int64_t received_unix_time;
    startup_schedule_entry_t received_entries[4];
    size_t received_entry_count;
} fake_gateway_t;

/* Simulate the NTP synchronization request used by the transfer helper. */
static int fake_request_sync(void *context)
{
    fake_gateway_t *gateway = context;
    return gateway->sync_result;
}

/* Return the fake synchronized clock value to the time-sync service. */
static int fake_read_synchronized_time(void *context, int64_t *unix_time)
{
    fake_gateway_t *gateway = context;

    if (gateway->clock_result != 0)
    {
        return gateway->clock_result;
    }

    *unix_time = gateway->unix_time;
    return 0;
}

/* Simulate delivery of the time-unavailable alert. */
static int fake_send_unavailable_alert(void *context)
{
    fake_gateway_t *gateway = context;
    return gateway->alert_result;
}

/* Decode the bytes supplied by the secure-send boundary for assertions. */
static int fake_secure_send(void *context,
                            const uint8_t *payload,
                            size_t payload_size)
{
    fake_gateway_t *gateway = context;

    gateway->send_count++;
    if (startup_message_decode(payload, payload_size, gateway->received_entries,
                               sizeof(gateway->received_entries) /
                                   sizeof(gateway->received_entries[0]),
                               &gateway->received_entry_count,
                               &gateway->received_unix_time) != 0)
    {
        return -1;
    }

    return gateway->transport_result;
}

/* Verify successful encoding of the daily schedule and current Unix time. */
static int test_schedule_and_current_time_are_sent(void)
{
    startup_schedule_entry_t entries[] = {
        {7U * 3600U, 250U},
        {12U * 3600U, 500U}};
    startup_daily_schedule_t schedule = {entries, 2U};
    fake_gateway_t gateway = {0, 0, 0, 0, 0, 1700000400, 0, {{0U, 0U}}, 0U};
    time_sync_ops_t ops = {
        fake_request_sync,
        fake_read_synchronized_time,
        fake_send_unavailable_alert,
        &gateway};
    time_sync_service_t time_sync;
    uint32_t next_delay = 0U;

    if ((time_sync_init(&time_sync, &ops) != 0) ||
        (time_sync_step(&time_sync, &next_delay) != 0) ||
        (ble_send_startup_data(&time_sync, &schedule, fake_secure_send, &gateway) != 0))
    {
        return 1;
    }

    if ((gateway.send_count != 1) || (gateway.received_entry_count != 2U) ||
        (gateway.received_entries[0].intake_time_seconds != (7U * 3600U)) ||
        (gateway.received_entries[0].target_weight_milligrams != 250U) ||
        (gateway.received_entries[1].intake_time_seconds != (12U * 3600U)) ||
        (gateway.received_entries[1].target_weight_milligrams != 500U) ||
        (gateway.received_unix_time != gateway.unix_time))
    {
        return 1;
    }

    return 0;
}

/* Verify invalid schedules and unsynchronized time are never sent. */
static int test_no_send_without_valid_sync_or_schedule(void)
{
    startup_schedule_entry_t invalid_entries[] = {
        {8U * 3600U, 250U},
        {7U * 3600U, 500U}};
    startup_daily_schedule_t invalid_schedule = {invalid_entries, 2U};
    startup_schedule_entry_t valid_entries[] = {{8U * 3600U, 250U}};
    startup_daily_schedule_t valid_schedule = {valid_entries, 1U};
    fake_gateway_t gateway = {1, 0, 0, 0, 0, 1700000500, 0, {{0U, 0U}}, 0U};
    time_sync_ops_t ops = {
        fake_request_sync,
        fake_read_synchronized_time,
        fake_send_unavailable_alert,
        &gateway};
    time_sync_service_t time_sync;

    if (time_sync_init(&time_sync, &ops) != 0)
    {
        return 1;
    }

    if ((ble_send_startup_data(&time_sync, &valid_schedule,
                               fake_secure_send, &gateway) == 0) ||
        (ble_send_startup_data(&time_sync, &invalid_schedule,
                               fake_secure_send, &gateway) == 0) ||
        (gateway.send_count != 0))
    {
        return 1;
    }

    return 0;
}

/* Verify startup transfer rejects a missing secure-transport callback. */
static int test_transport_failure_is_propagated(void)
{
    startup_schedule_entry_t entries[] = {{9U * 3600U, 200U}};
    startup_daily_schedule_t schedule = {entries, 1U};
    fake_gateway_t gateway = {0, 0, 0, 0, 0, 1700000600, 0, {{0U, 0U}}, 0U};
    time_sync_ops_t ops = {
        fake_request_sync,
        fake_read_synchronized_time,
        fake_send_unavailable_alert,
        &gateway};
    time_sync_service_t time_sync;
    uint32_t next_delay = 0U;

    if ((time_sync_init(&time_sync, &ops) != 0) ||
        (time_sync_step(&time_sync, &next_delay) != 0))
    {
        return 1;
    }

    if (ble_send_startup_data(&time_sync, &schedule, NULL, &gateway) == 0)
    {
        return 1;
    }

    return 0;
}

/* Verify errors from the secure transport reach the caller. */
static int test_transport_error_is_propagated(void)
{
    startup_schedule_entry_t entries[] = {{9U * 3600U, 200U}};
    startup_daily_schedule_t schedule = {entries, 1U};
    fake_gateway_t gateway = {0, 0, 0, 1, 0, 1700000700, 0, {{0U, 0U}}, 0U};
    time_sync_ops_t ops = {
        fake_request_sync,
        fake_read_synchronized_time,
        fake_send_unavailable_alert,
        &gateway};
    time_sync_service_t time_sync;
    uint32_t next_delay = 0U;

    if ((time_sync_init(&time_sync, &ops) != 0) ||
        (time_sync_step(&time_sync, &next_delay) != 0))
    {
        return 1;
    }

    if ((ble_send_startup_data(&time_sync, &schedule, fake_secure_send, &gateway) == 0) ||
        (gateway.send_count != 1))
    {
        return 1;
    }

    return 0;
}

/* Verify the ESP32 receiver accepts valid data and clears state on bad input. */
static int test_esp32_receiver_accepts_and_rejects_payloads(void)
{
    startup_schedule_entry_t entries[] = {
        {7U * 3600U, 250U},
        {12U * 3600U, 500U}};
    startup_daily_schedule_t schedule = {entries, 2U};
    uint8_t payload[STARTUP_MESSAGE_HEADER_SIZE +
                    (2U * STARTUP_MESSAGE_ENTRY_SIZE)];
    size_t payload_size = 0U;
    startup_schedule_entry_t received_entries[2];
    static const uint8_t expected_payload[] = {
        0x01U, 0x01U, 0x00U, 0x00U, 0x00U, 0x00U, 0x65U, 0x53U,
        0xF4U, 0x20U, 0x00U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x62U, 0x70U, 0x00U, 0x00U, 0x00U, 0xFAU,
        0x00U, 0x00U, 0xA8U, 0xC0U, 0x00U, 0x00U, 0x01U, 0xF4U};
    startup_received_data_t received_data = {
        received_entries,
        sizeof(received_entries) / sizeof(received_entries[0]),
        0U,
        0,
        false};

    if (startup_message_encode(&schedule, INT64_C(1700000800), payload,
                               sizeof(payload), &payload_size) != 0)
    {
        return 1;
    }

    if ((payload_size != sizeof(expected_payload)) ||
        (memcmp(payload, expected_payload, sizeof(expected_payload)) != 0))
    {
        return 1;
    }

    if ((ble_startup_receiver_accept_authenticated_payload(
             &received_data, payload, payload_size) != 0) ||
        !received_data.has_valid_startup_data ||
        (received_data.entry_count != 2U) ||
        (received_data.current_unix_time != INT64_C(1700000800)) ||
        (received_entries[1].intake_time_seconds != (12U * 3600U)) ||
        (received_entries[1].target_weight_milligrams != 500U))
    {
        return 1;
    }

    payload[0] = 0U;
    if ((ble_startup_receiver_accept_authenticated_payload(
             &received_data, payload, payload_size) == 0) ||
        received_data.has_valid_startup_data ||
        (received_data.entry_count != 0U))
    {
        return 1;
    }

    return 0;
}

/* Record the payload size without decoding it. */
static int record_payload_size(void *context,
                               const uint8_t *payload,
                               size_t payload_size)
{
    (void)payload;
    *(size_t *)context = payload_size;
    return 0;
}

/* Verify only schedules that fit one protected message are transferred. */
static int test_schedule_size_is_bounded_by_secure_message(void)
{
    enum
    {
        max_entries = (SECURE_CHANNEL_MAX_MESSAGE_SIZE -
                       STARTUP_MESSAGE_HEADER_SIZE) /
                      STARTUP_MESSAGE_ENTRY_SIZE
    };
    startup_schedule_entry_t entries[max_entries + 1U];
    startup_daily_schedule_t schedule = {entries, max_entries};
    fake_gateway_t gateway = {0, 0, 0, 0, 0, 1700000900, 0, {{0U, 0U}}, 0U};
    time_sync_ops_t ops = {
        fake_request_sync,
        fake_read_synchronized_time,
        fake_send_unavailable_alert,
        &gateway};
    time_sync_service_t time_sync;
    uint32_t next_delay = 0U;
    size_t sent_size = 0U;
    size_t index;

    for (index = 0U; index < (max_entries + 1U); index++)
    {
        entries[index].intake_time_seconds = (uint32_t)((index + 1U) * 60U);
        entries[index].target_weight_milligrams = 100U;
    }
    if ((time_sync_init(&time_sync, &ops) != 0) ||
        (time_sync_step(&time_sync, &next_delay) != 0))
    {
        return 1;
    }

    if ((ble_send_startup_data(&time_sync, &schedule, record_payload_size,
                               &sent_size) != 0) ||
        (sent_size > SECURE_CHANNEL_MAX_MESSAGE_SIZE))
    {
        return 1;
    }
    sent_size = 0U;
    schedule.entry_count = max_entries + 1U;
    if ((ble_send_startup_data(&time_sync, &schedule, record_payload_size,
                               &sent_size) == 0) ||
        (sent_size != 0U))
    {
        return 1;
    }

    return 0;
}

/* Run the startup-transfer protocol and receiver tests. */
int main(void)
{
    if ((test_schedule_and_current_time_are_sent() != 0) ||
        (test_no_send_without_valid_sync_or_schedule() != 0) ||
        (test_schedule_size_is_bounded_by_secure_message() != 0) ||
        (test_transport_failure_is_propagated() != 0) ||
        (test_transport_error_is_propagated() != 0) ||
        (test_esp32_receiver_accepts_and_rejects_payloads() != 0))
    {
        (void)fputs("startup transfer tests failed\n", stderr);
        return EXIT_FAILURE;
    }

    (void)puts("startup transfer tests passed");
    return EXIT_SUCCESS;
}
