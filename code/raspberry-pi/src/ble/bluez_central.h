#ifndef BLUEZ_CENTRAL_H
#define BLUEZ_CENTRAL_H

#include "heartbeat_monitor.h"
#include "../../../protocol/include/secure_channel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BLE_CENTRAL_ADDRESS_SIZE 18U

typedef struct ble_central ble_central_t;

typedef enum
{
    BLE_CENTRAL_EVENT_CONNECTED = 1,
    BLE_CENTRAL_EVENT_DISCONNECTED = 2,
    BLE_CENTRAL_EVENT_ERROR = 3
} ble_central_event_t;

typedef void (*ble_central_event_fn)(void *context,
                                     ble_central_event_t event,
                                     const char *detail);
typedef int (*ble_central_message_fn)(
    void *context,
    secure_channel_message_type_t message_type,
    const uint8_t *payload,
    size_t payload_size);
/* Message callback returns nonzero to reject the authenticated message. */
typedef int (*ble_central_alert_fn)(void *context);

typedef struct
{
    /* Canonical configured ESP32 address (XX:XX:XX:XX:XX:XX); no scan
     * result other than this exact address is ever connected. */
    char device_address[BLE_CENTRAL_ADDRESS_SIZE];
    /* Optional BlueZ adapter object path; defaults to /org/bluez/hci0. */
    const char *adapter_path;
    /* Must be a nonzero, provisioned 32-byte commissioning key. */
    uint8_t psk[SECURE_CHANNEL_PSK_SIZE];
    ble_central_event_fn on_event;
    ble_central_message_fn on_message;
    /* Required; invoked once on heartbeat timeout. */
    ble_central_alert_fn on_heartbeat_timeout;
    void *callback_context;
} ble_central_config_t;

/*
 * Create copies of the address and PSK. Keep callbacks/context valid until
 * destroy. start() launches the BlueZ event thread; application traffic is
 * accepted only after the authenticated handshake. Callbacks run on that
 * thread and must not block waiting for another BLE event.
 */
ble_central_t *ble_central_create(const ble_central_config_t *config);
int ble_central_start(ble_central_t *central);
void ble_central_stop(ble_central_t *central);
void ble_central_destroy(ble_central_t *central);

/*
 * Seal and write one application message. Calls from other threads wait
 * until the complete fragmented GATT write succeeds. Calls made from a
 * callback execute directly. The payload is copied before returning.
 */
int ble_central_send(ble_central_t *central,
                     secure_channel_message_type_t message_type,
                     const uint8_t *payload,
                     size_t payload_size);

/* Adapter for ble_send_startup_data(); sends a protected STARTUP message. */
int ble_central_secure_send_payload(void *central,
                                    const uint8_t *payload,
                                    size_t payload_size);

#endif
