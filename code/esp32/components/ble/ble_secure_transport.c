#include "ble_secure_transport.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/platform_util.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"

#include "ble_wire.h"

#define REASSEMBLY_TIMEOUT_US INT64_C(5000000)
#define MAX_FRAGMENT_COUNT MEDDISP_BLE_MAX_REASSEMBLED_MESSAGE

static const char *TAG = "ble_secure";
static const ble_uuid128_t service_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0xf3, 0x93, 0xf3, 0xb5, 0x01, 0x00, 0x40, 0x7e);
static const ble_uuid128_t message_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0xf3, 0x93, 0xf3, 0xb5, 0x02, 0x00, 0x40, 0x7e);

static secure_channel_t secure_channel;
static uint8_t commissioned_psk[SECURE_CHANNEL_PSK_SIZE];
static uint8_t receive_message[MEDDISP_BLE_MAX_REASSEMBLED_MESSAGE];
static uint8_t protected_message[SECURE_CHANNEL_MAX_MESSAGE_SIZE + SECURE_CHANNEL_OVERHEAD];
static uint8_t plaintext[SECURE_CHANNEL_MAX_MESSAGE_SIZE];
static uint8_t fragment_buffer[MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD +
                               MEDDISP_BLE_FRAGMENT_HEADER_SIZE];
static ble_secure_message_callback_t message_callback;
static void *message_context;
static uint16_t message_value_handle;
static uint16_t active_connection = BLE_HS_CONN_HANDLE_NONE;
static uint8_t own_address_type;
static uint16_t next_message_id;
static uint16_t expected_fragment_index;
static uint16_t active_fragment_count;
static uint16_t active_message_id;
static size_t receive_message_size;
static int64_t last_fragment_time_us;
static esp_timer_handle_t reassembly_timer;
static uint16_t transmit_fragment_count;
static uint16_t transmit_fragment_index;
static uint16_t transmit_message_id;
static size_t transmit_message_size;
static size_t transmit_offset;
static bool reassembly_active;
static bool transmit_pending;
static bool transport_started;
static bool handshake_established;

static int gatt_access(uint16_t connection_handle, uint16_t attribute_handle,
                       struct ble_gatt_access_ctxt *context, void *argument);
static int start_advertising(void);
static int gap_event(struct ble_gap_event *event, void *argument);

static void reassembly_timeout(void *argument)
{
    (void)argument;
    if (active_connection != BLE_HS_CONN_HANDLE_NONE)
    {
        (void)ble_gap_terminate(active_connection, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static uint16_t read_u16_be(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8U) | bytes[1]);
}

static void write_u16_be(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8U);
    bytes[1] = (uint8_t)value;
}

static void clear_reassembly(void)
{
    reassembly_active = false;
    receive_message_size = 0U;
    expected_fragment_index = 0U;
    active_fragment_count = 0U;
    active_message_id = 0U;
    last_fragment_time_us = 0;
    if (reassembly_timer != NULL)
    {
        (void)esp_timer_stop(reassembly_timer);
    }
}

static void close_connection(void)
{
    uint16_t connection = active_connection;
    active_connection = BLE_HS_CONN_HANDLE_NONE;
    handshake_established = false;
    transmit_pending = false;
    clear_reassembly();
    secure_channel_clear(&secure_channel);
    (void)memset(&secure_channel, 0, sizeof(secure_channel));
    mbedtls_platform_zeroize(receive_message,
                             sizeof(receive_message));
    mbedtls_platform_zeroize(protected_message,
                             sizeof(protected_message));
    mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
    mbedtls_platform_zeroize(fragment_buffer,
                             sizeof(fragment_buffer));
    mbedtls_platform_zeroize(receive_message, sizeof(receive_message));
    mbedtls_platform_zeroize(protected_message, sizeof(protected_message));
    mbedtls_platform_zeroize(plaintext, sizeof(plaintext));
    mbedtls_platform_zeroize(fragment_buffer, sizeof(fragment_buffer));
    if (connection != BLE_HS_CONN_HANDLE_NONE)
    {
        (void)ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static int notify_raw(const uint8_t *data, size_t size)
{
    struct os_mbuf *om;
    int result;
    if ((active_connection == BLE_HS_CONN_HANDLE_NONE) ||
        (message_value_handle == 0U) || (data == NULL) ||
        (ble_att_mtu(active_connection) <= 3U) ||
        (size > (size_t)(ble_att_mtu(active_connection) - 3U)))
    {
        return -1;
    }
    om = ble_hs_mbuf_from_flat(data, (uint16_t)size);
    if (om == NULL)
    {
        return -1;
    }
    result = ble_gatts_notify_custom(active_connection, message_value_handle, om);
    return (result == 0) ? 0 : -1;
}

static int send_next_notification(void)
{
    size_t chunk = transmit_message_size - transmit_offset;
    if (!transmit_pending)
    {
        return 0;
    }
    if (chunk > MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD)
    {
        chunk = MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD;
    }
    write_u16_be(&fragment_buffer[0], transmit_message_id);
    write_u16_be(&fragment_buffer[2], transmit_fragment_index);
    write_u16_be(&fragment_buffer[4], transmit_fragment_count);
    write_u16_be(&fragment_buffer[6], (uint16_t)chunk);
    (void)memcpy(&fragment_buffer[MEDDISP_BLE_FRAGMENT_HEADER_SIZE],
                 &protected_message[transmit_offset], chunk);
    if (notify_raw(fragment_buffer, chunk + MEDDISP_BLE_FRAGMENT_HEADER_SIZE) != 0)
    {
        return -1;
    }
    transmit_offset += chunk;
    ++transmit_fragment_index;
    if (transmit_fragment_index == transmit_fragment_count)
    {
        transmit_pending = false;
        mbedtls_platform_zeroize(protected_message, transmit_message_size);
        transmit_message_size = 0U;
        transmit_offset = 0U;
    }
    return 0;
}

static int notify_protected(const uint8_t *data, size_t size)
{
    if ((data == NULL) || (size == 0U) || (size > sizeof(protected_message)) ||
        transmit_pending ||
        (ble_att_mtu(active_connection) <
         (MEDDISP_BLE_FRAGMENT_HEADER_SIZE + MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD + 3U)))
    {
        return -1;
    }
    transmit_fragment_count = (uint16_t)((size + MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD - 1U) /
                                         MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD);
    transmit_fragment_index = 0U;
    transmit_message_id = next_message_id++;
    transmit_message_size = size;
    transmit_offset = 0U;
    transmit_pending = true;
    return send_next_notification();
}

static int process_complete_message(const uint8_t *data, size_t size)
{
    uint8_t handshake_reply[SERVER_HELLO_SIZE];
    size_t handshake_reply_size = 0U;
    bool established = false;

    if (!handshake_established)
    {
        if (secure_channel_process_handshake(&secure_channel, data, size,
                                             handshake_reply, sizeof(handshake_reply),
                                             &handshake_reply_size, &established) != 0)
        {
            return -1;
        }
        if ((handshake_reply_size != 0U) &&
            (notify_raw(handshake_reply, handshake_reply_size) != 0))
        {
            return -1;
        }
        if (established)
        {
            handshake_established = true;
            ESP_LOGI(TAG, "BLE secure-channel handshake established");
        }
        return 0;
    }

    {
        secure_channel_message_type_t message_type;
        size_t plaintext_size = 0U;
        if (secure_channel_open(&secure_channel, data, size, &message_type, plaintext,
                                sizeof(plaintext), &plaintext_size) != 0)
        {
            return -1;
        }
        if (message_callback != NULL)
        {
            int callback_result = message_callback(
                message_type, plaintext, plaintext_size, message_context);
            mbedtls_platform_zeroize(plaintext, plaintext_size);
            if (callback_result != 0)
            {
                return -1;
            }
        }
        else
        {
            mbedtls_platform_zeroize(plaintext, plaintext_size);
            return -1;
        }
    }
    return 0;
}

static int receive_fragment(const uint8_t *value, size_t value_size)
{
    uint16_t message_id;
    uint16_t fragment_index;
    uint16_t fragment_count;
    uint16_t data_size;
    int64_t now = esp_timer_get_time();

    if (value_size < MEDDISP_BLE_FRAGMENT_HEADER_SIZE)
    {
        return -1;
    }
    message_id = read_u16_be(&value[0]);
    fragment_index = read_u16_be(&value[2]);
    fragment_count = read_u16_be(&value[4]);
    data_size = read_u16_be(&value[6]);
    if ((data_size == 0U) || (data_size > MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD) ||
        (value_size != MEDDISP_BLE_FRAGMENT_HEADER_SIZE + data_size) ||
        (fragment_count == 0U) || (fragment_count > MAX_FRAGMENT_COUNT) ||
        (fragment_index >= fragment_count))
    {
        return -1;
    }

    if (reassembly_active && ((now - last_fragment_time_us) > REASSEMBLY_TIMEOUT_US))
    {
        return -1;
    }
    if (!reassembly_active)
    {
        if (fragment_index != 0U)
        {
            return -1;
        }
        reassembly_active = true;
        active_message_id = message_id;
        active_fragment_count = fragment_count;
        expected_fragment_index = 0U;
        receive_message_size = 0U;
    }
    if ((message_id != active_message_id) ||
        (fragment_count != active_fragment_count) ||
        (fragment_index != expected_fragment_index) ||
        (receive_message_size + data_size > sizeof(receive_message)))
    {
        return -1;
    }
    (void)memcpy(&receive_message[receive_message_size],
                 &value[MEDDISP_BLE_FRAGMENT_HEADER_SIZE], data_size);
    receive_message_size += data_size;
    ++expected_fragment_index;
    last_fragment_time_us = now;
    (void)esp_timer_stop(reassembly_timer);
    if (expected_fragment_index != active_fragment_count &&
        (esp_timer_start_once(reassembly_timer, (uint64_t)REASSEMBLY_TIMEOUT_US) != ESP_OK))
    {
        return -1;
    }
    if (expected_fragment_index == active_fragment_count)
    {
        size_t completed_size = receive_message_size;
        int result;
        clear_reassembly();
        result = process_complete_message(receive_message, completed_size);
        mbedtls_platform_zeroize(receive_message, completed_size);
        return result;
    }
    return 0;
}

static int gatt_access(uint16_t connection_handle, uint16_t attribute_handle,
                       struct ble_gatt_access_ctxt *context, void *argument)
{
    uint8_t value[MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD +
                  MEDDISP_BLE_FRAGMENT_HEADER_SIZE];
    uint16_t value_size;
    (void)attribute_handle;
    (void)argument;

    if ((context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) ||
        (connection_handle != active_connection))
    {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (OS_MBUF_PKTLEN(context->om) > sizeof(value))
    {
        close_connection();
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    value_size = (uint16_t)OS_MBUF_PKTLEN(context->om);
    if ((value_size == 0U) || (os_mbuf_copydata(context->om, 0, value_size, value) != 0))
    {
        close_connection();
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    if (!handshake_established && (value_size >= 5U) &&
        (memcmp(value, "MDP1", 4U) == 0))
    {
        if (process_complete_message(value, value_size) != 0)
        {
            close_connection();
            return BLE_ATT_ERR_UNLIKELY;
        }
        return 0;
    }
    if (receive_fragment(value, value_size) != 0)
    {
        close_connection();
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    return 0;
}

static const struct ble_gatt_chr_def characteristics[] = {
    {
        .uuid = &message_uuid.u,
        .access_cb = gatt_access,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP |
                 BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &message_value_handle,
    },
    {0}
};

static const struct ble_gatt_svc_def services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &service_uuid.u,
        .characteristics = characteristics,
    },
    {0}
};

static int start_advertising(void)
{
    struct ble_hs_adv_fields fields;
    struct ble_gap_adv_params parameters;
    const char *name = "MedDisp";
    int result;

    (void)memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128 = (ble_uuid128_t *)&service_uuid;
    fields.num_uuids128 = 1U;
    fields.uuids128_is_complete = 1U;
    fields.name = (const uint8_t *)name;
    fields.name_len = (uint8_t)strlen(name);
    fields.name_is_complete = 1U;
    result = ble_gap_adv_set_fields(&fields);
    if (result != 0)
    {
        return result;
    }
    (void)memset(&parameters, 0, sizeof(parameters));
    parameters.conn_mode = BLE_GAP_CONN_MODE_UND;
    parameters.disc_mode = BLE_GAP_DISC_MODE_GEN;
    result = ble_gap_adv_start(own_address_type, NULL, BLE_HS_FOREVER,
                               &parameters, gap_event, NULL);
    return result;
}

static int gap_event(struct ble_gap_event *event, void *argument)
{
    (void)argument;
    switch (event->type)
    {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status != 0)
            {
                (void)start_advertising();
                return 0;
            }
            if (active_connection != BLE_HS_CONN_HANDLE_NONE)
            {
                (void)ble_gap_terminate(event->connect.conn_handle,
                                        BLE_ERR_REM_USER_CONN_TERM);
                return 0;
            }
            active_connection = event->connect.conn_handle;
            clear_reassembly();
            handshake_established = false;
            if (secure_channel_init(&secure_channel, SECURE_CHANNEL_ROLE_SERVER,
                                    commissioned_psk) != 0)
            {
                close_connection();
            }
            break;
        case BLE_GAP_EVENT_NOTIFY_TX:
            if ((event->notify_tx.conn_handle == active_connection) &&
                (event->notify_tx.attr_handle == message_value_handle))
            {
                if (event->notify_tx.status != 0)
                {
                    close_connection();
                }
                else if (transmit_pending && (send_next_notification() != 0))
                {
                    close_connection();
                }
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            if (event->disconnect.conn.conn_handle == active_connection)
            {
                active_connection = BLE_HS_CONN_HANDLE_NONE;
                handshake_established = false;
                clear_reassembly();
                secure_channel_clear(&secure_channel);
                (void)memset(&secure_channel, 0, sizeof(secure_channel));
            }
            (void)start_advertising();
            break;
        case BLE_GAP_EVENT_SUBSCRIBE:
            if ((event->subscribe.conn_handle == active_connection) &&
                (event->subscribe.cur_notify == 0U))
            {
                close_connection();
            }
            break;
        case BLE_GAP_EVENT_MTU:
            if ((event->mtu.conn_handle == active_connection) &&
                (event->mtu.value < MEDDISP_BLE_FRAGMENT_HEADER_SIZE +
                                    MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD + 3U))
            {
                close_connection();
            }
            break;
        default:
            break;
    }
    return 0;
}

static void host_sync(void)
{
    int result = ble_hs_id_infer_auto(0, &own_address_type);
    if (result == 0)
    {
        result = start_advertising();
    }
    if (result != 0)
    {
        ESP_LOGE(TAG, "Unable to start BLE advertising: %d", result);
    }
}

static void host_task(void *argument)
{
    (void)argument;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

int ble_secure_transport_start(const uint8_t psk[SECURE_CHANNEL_PSK_SIZE],
                               ble_secure_message_callback_t on_message,
                               void *context)
{
    int result;
    const esp_timer_create_args_t timer_args = {
        .callback = reassembly_timeout,
        .name = "ble_reassembly",
    };

    if ((psk == NULL) || (on_message == NULL) || transport_started)
    {
        return -1;
    }
    (void)memcpy(commissioned_psk, psk, sizeof(commissioned_psk));
    message_callback = on_message;
    message_context = context;
    (void)ble_att_set_preferred_mtu(MEDDISP_BLE_FRAGMENT_HEADER_SIZE +
                                    MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD + 3U);
    result = esp_timer_create(&timer_args, &reassembly_timer);
    if (result != ESP_OK)
    {
        mbedtls_platform_zeroize(commissioned_psk, sizeof(commissioned_psk));
        return -1;
    }
    result = nimble_port_init();
    if (result != 0)
    {
        (void)esp_timer_delete(reassembly_timer);
        reassembly_timer = NULL;
        mbedtls_platform_zeroize(commissioned_psk, sizeof(commissioned_psk));
        return -1;
    }
    result = ble_gatts_count_cfg(services);
    if (result == 0)
    {
        result = ble_gatts_add_svcs(services);
    }
    if (result != 0)
    {
        if (reassembly_timer != NULL)
        {
            (void)esp_timer_delete(reassembly_timer);
        }
        reassembly_timer = NULL;
        nimble_port_deinit();
        mbedtls_platform_zeroize(commissioned_psk, sizeof(commissioned_psk));
        return -1;
    }
    ble_hs_cfg.sync_cb = host_sync;
    transport_started = true;
    nimble_port_freertos_init(host_task);
    return 0;
}

int ble_secure_transport_send(secure_channel_message_type_t message_type,
                             const uint8_t *data, size_t size)
{
    size_t protected_size = 0U;
    if (!handshake_established || (active_connection == BLE_HS_CONN_HANDLE_NONE) ||
        transmit_pending ||
        (size > SECURE_CHANNEL_MAX_MESSAGE_SIZE) ||
        (secure_channel_seal(&secure_channel, message_type, data, size,
                             protected_message, sizeof(protected_message),
                             &protected_size) != 0) ||
        (notify_protected(protected_message, protected_size) != 0))
    {
        close_connection();
        return -1;
    }
    return 0;
}

int ble_secure_transport_is_established(void)
{
    return handshake_established ? 1 : 0;
}
