#ifndef BLE_SECURE_TRANSPORT_H
#define BLE_SECURE_TRANSPORT_H

#include <stddef.h>
#include <stdint.h>

#include "secure_channel.h"

typedef int (*ble_secure_message_callback_t)(
    secure_channel_message_type_t message_type,
    const uint8_t *plaintext,
    size_t plaintext_size,
    void *context);

/* Return nonzero to reject the authenticated application message and close. */

/*
 * Start the NimBLE peripheral using an explicitly commissioned 32-byte PSK.
 * The key is copied. Startup fails closed when the caller has no key.
 */
int ble_secure_transport_start(
    const uint8_t psk[SECURE_CHANNEL_PSK_SIZE],
    ble_secure_message_callback_t on_message,
    void *context);

/*
 * Seal and notify one application message. Call from the NimBLE host task
 * (including from on_message); the transport fragments protected messages.
 */
int ble_secure_transport_send(secure_channel_message_type_t message_type,
                              const uint8_t *plaintext,
                              size_t plaintext_size);

/* Whether the peer has completed the authenticated handshake. */
int ble_secure_transport_is_established(void);

#endif
