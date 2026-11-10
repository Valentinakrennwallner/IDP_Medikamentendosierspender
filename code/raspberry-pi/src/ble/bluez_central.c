#define _POSIX_C_SOURCE 200809L

#include "bluez_central.h"

#include "../../../protocol/include/ble_wire.h"

#include <dbus/dbus.h>
#include <openssl/crypto.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define BLUEZ_BUS_NAME "org.bluez"
#define BLUEZ_OBJECT_MANAGER "org.freedesktop.DBus.ObjectManager"
#define BLUEZ_PROPERTIES "org.freedesktop.DBus.Properties"
#define BLUEZ_ADAPTER "org.bluez.Adapter1"
#define BLUEZ_DEVICE "org.bluez.Device1"
#define BLUEZ_SERVICE "org.bluez.GattService1"
#define BLUEZ_CHARACTERISTIC "org.bluez.GattCharacteristic1"
#define BLUEZ_MAX_GATT_PATH 256U
#define BLE_FRAGMENT_TIMEOUT_MS UINT64_C(5000)
#define BLE_HANDSHAKE_TIMEOUT_MS UINT64_C(15000)
#define BLE_CONNECT_RETRY_MS UINT64_C(5000)
#define BLE_PACKET_QUEUE_SIZE 16U
#define BLE_SEND_QUEUE_SIZE 8U
#define BLE_MAX_FRAGMENTS \
    ((MEDDISP_BLE_MAX_REASSEMBLED_MESSAGE + \
      MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD - 1U) / \
     MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD)

typedef struct
{
    uint8_t bytes[MEDDISP_BLE_FRAGMENT_HEADER_SIZE +
                 MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD];
    size_t size;
} ble_packet_t;

typedef struct ble_send_request
{
    struct ble_send_request *next;
    secure_channel_message_type_t message_type;
    uint8_t payload[SECURE_CHANNEL_MAX_MESSAGE_SIZE];
    size_t payload_size;
    pthread_cond_t completed_condition;
    bool completed;
    int result;
} ble_send_request_t;

struct ble_central
{
    char device_address[BLE_CENTRAL_ADDRESS_SIZE];
    char adapter_path[BLUEZ_MAX_GATT_PATH];
    uint8_t psk[SECURE_CHANNEL_PSK_SIZE];
    ble_central_event_fn on_event;
    ble_central_message_fn on_message;
    ble_central_alert_fn on_heartbeat_timeout;
    void *callback_context;

    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t queue_condition;
    bool thread_started;
    bool stop_requested;
    bool authenticated;
    bool worker_running;

    DBusConnection *bus;
    char device_path[BLUEZ_MAX_GATT_PATH];
    char characteristic_path[BLUEZ_MAX_GATT_PATH];
    secure_channel_t secure_channel;
    heartbeat_monitor_t heartbeat_monitor;

    ble_packet_t packet_queue[BLE_PACKET_QUEUE_SIZE];
    size_t packet_head;
    size_t packet_count;
    uint8_t reassembly[MEDDISP_BLE_MAX_REASSEMBLED_MESSAGE];
    size_t reassembly_size;
    uint16_t reassembly_id;
    uint16_t reassembly_count;
    uint16_t reassembly_next_index;
    uint64_t reassembly_started_ms;
    bool reassembly_active;
    uint16_t next_message_id;

    ble_send_request_t *send_head;
    ble_send_request_t *send_tail;
    size_t send_count;
    bool fatal_error;
};

static uint64_t ble_monotonic_ms(void)
{
    struct timespec time_value;

    if (clock_gettime(CLOCK_MONOTONIC, &time_value) != 0)
    {
        return 0U;
    }

    return ((uint64_t)time_value.tv_sec * UINT64_C(1000)) +
           ((uint64_t)time_value.tv_nsec / UINT64_C(1000000));
}

static bool ble_should_stop(ble_central_t *central)
{
    bool should_stop;

    (void)pthread_mutex_lock(&central->mutex);
    should_stop = central->stop_requested;
    (void)pthread_mutex_unlock(&central->mutex);
    return should_stop;
}

static void ble_emit_event(ble_central_t *central,
                           ble_central_event_t event,
                           const char *detail)
{
    if (central->on_event != NULL)
    {
        central->on_event(central->callback_context, event, detail);
    }
}

static int ble_set_authenticated(ble_central_t *central, bool authenticated)
{
    int result = pthread_mutex_lock(&central->mutex);

    if (result != 0)
    {
        return -1;
    }
    central->authenticated = authenticated;
    (void)pthread_cond_broadcast(&central->queue_condition);
    (void)pthread_mutex_unlock(&central->mutex);
    return 0;
}

static bool ble_address_is_valid(const char *address)
{
    size_t index;

    if ((address == NULL) ||
        (strlen(address) != (BLE_CENTRAL_ADDRESS_SIZE - 1U)))
    {
        return false;
    }
    for (index = 0U; index < BLE_CENTRAL_ADDRESS_SIZE - 1U; index++)
    {
        if (((index + 1U) % 3U) == 0U)
        {
            if (address[index] != ':')
            {
                return false;
            }
        }
        else if (!(((address[index] >= '0') && (address[index] <= '9')) ||
                   ((address[index] >= 'A') && (address[index] <= 'F')) ||
                   ((address[index] >= 'a') && (address[index] <= 'f'))))
        {
            return false;
        }
    }
    return true;
}

static bool ble_known_message_type(secure_channel_message_type_t type)
{
    return (type == SECURE_CHANNEL_MESSAGE_STARTUP) ||
           (type == SECURE_CHANNEL_MESSAGE_HEARTBEAT) ||
           (type == SECURE_CHANNEL_MESSAGE_ACKNOWLEDGEMENT) ||
           (type == SECURE_CHANNEL_MESSAGE_RESULT) ||
           (type == SECURE_CHANNEL_MESSAGE_DAY_END) ||
           (type == SECURE_CHANNEL_MESSAGE_FAULT);
}

static DBusMessage *ble_call_method(ble_central_t *central,
                                    const char *path,
                                    const char *interface,
                                    const char *method)
{
    DBusMessage *request;
    DBusMessage *reply;
    DBusError error;

    request = dbus_message_new_method_call(BLUEZ_BUS_NAME, path, interface,
                                           method);
    if (request == NULL)
    {
        return NULL;
    }
    dbus_error_init(&error);
    reply = dbus_connection_send_with_reply_and_block(
        central->bus, request, 10000, &error);
    dbus_message_unref(request);
    if (dbus_error_is_set(&error))
    {
        dbus_error_free(&error);
    }
    return reply;
}

static int ble_call_no_arguments(ble_central_t *central,
                                 const char *path,
                                 const char *interface,
                                 const char *method)
{
    DBusMessage *reply = ble_call_method(central, path, interface, method);
    int result = -1;

    if (reply != NULL)
    {
        result = (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN)
                     ? 0
                     : -1;
        dbus_message_unref(reply);
    }
    return result;
}

static bool ble_dict_get(DBusMessageIter *dictionary,
                         const char *name,
                         int expected_type,
                         DBusMessageIter *value)
{
    DBusMessageIter entries;

    if (dbus_message_iter_get_arg_type(dictionary) != DBUS_TYPE_ARRAY)
    {
        return false;
    }
    dbus_message_iter_recurse(dictionary, &entries);
    while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY)
    {
        DBusMessageIter entry;
        DBusMessageIter variant;
        const char *key = NULL;

        dbus_message_iter_recurse(&entries, &entry);
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
        {
            dbus_message_iter_get_basic(&entry, &key);
        }
        if ((key != NULL) && (strcmp(key, name) == 0))
        {
            if (!dbus_message_iter_next(&entry) ||
                (dbus_message_iter_get_arg_type(&entry) != DBUS_TYPE_VARIANT))
            {
                return false;
            }
            dbus_message_iter_recurse(&entry, &variant);
            if (dbus_message_iter_get_arg_type(&variant) != expected_type)
            {
                return false;
            }
            *value = variant;
            return true;
        }
        dbus_message_iter_next(&entries);
    }
    return false;
}

static bool ble_dict_contains(DBusMessageIter *dictionary, const char *name)
{
    DBusMessageIter entries;

    if (dbus_message_iter_get_arg_type(dictionary) != DBUS_TYPE_ARRAY)
    {
        return false;
    }
    dbus_message_iter_recurse(dictionary, &entries);
    while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY)
    {
        DBusMessageIter entry;
        const char *key = NULL;

        dbus_message_iter_recurse(&entries, &entry);
        if (dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_STRING)
        {
            dbus_message_iter_get_basic(&entry, &key);
        }
        if ((key != NULL) && (strcmp(key, name) == 0))
        {
            return true;
        }
        dbus_message_iter_next(&entries);
    }
    return false;
}

static bool ble_find_interface(DBusMessageIter *interfaces,
                               const char *name,
                               DBusMessageIter *properties)
{
    DBusMessageIter items;

    if (dbus_message_iter_get_arg_type(interfaces) != DBUS_TYPE_ARRAY)
    {
        return false;
    }
    dbus_message_iter_recurse(interfaces, &items);
    while (dbus_message_iter_get_arg_type(&items) == DBUS_TYPE_DICT_ENTRY)
    {
        DBusMessageIter item;
        DBusMessageIter property_array;
        const char *interface_name = NULL;

        dbus_message_iter_recurse(&items, &item);
        if (dbus_message_iter_get_arg_type(&item) == DBUS_TYPE_STRING)
        {
            dbus_message_iter_get_basic(&item, &interface_name);
        }
        if ((interface_name != NULL) && (strcmp(interface_name, name) == 0) &&
            dbus_message_iter_next(&item) &&
            (dbus_message_iter_get_arg_type(&item) == DBUS_TYPE_ARRAY))
        {
            dbus_message_iter_recurse(&item, &property_array);
            *properties = property_array;
            return true;
        }
        dbus_message_iter_next(&items);
    }
    return false;
}

static DBusMessage *ble_get_managed_objects(ble_central_t *central)
{
    return ble_call_method(central, "/",
                           "org.freedesktop.DBus.ObjectManager",
                           "GetManagedObjects");
}

static bool ble_find_device(DBusMessage *objects,
                            const char *wanted_address,
                            char *device_path,
                            size_t path_capacity,
                            bool *services_resolved)
{
    DBusMessageIter root;
    DBusMessageIter entries;

    if (!dbus_message_iter_init(objects, &root) ||
        (dbus_message_iter_get_arg_type(&root) != DBUS_TYPE_ARRAY))
    {
        return false;
    }
    dbus_message_iter_recurse(&root, &entries);
    while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY)
    {
        DBusMessageIter entry;
        DBusMessageIter object_path_iter;
        DBusMessageIter interfaces;
        DBusMessageIter properties;
        DBusMessageIter value;
        const char *path = NULL;
        const char *address = NULL;
        dbus_bool_t resolved = FALSE;

        dbus_message_iter_recurse(&entries, &entry);
        dbus_message_iter_get_basic(&entry, &path);
        dbus_message_iter_next(&entry);
        object_path_iter = entry;
        interfaces = object_path_iter;
        if (ble_find_interface(&interfaces, BLUEZ_DEVICE, &properties) &&
            ble_dict_get(&properties, "Address", DBUS_TYPE_STRING, &value))
        {
            dbus_message_iter_get_basic(&value, &address);
            if (ble_dict_get(&properties, "ServicesResolved",
                             DBUS_TYPE_BOOLEAN, &value))
            {
                dbus_message_iter_get_basic(&value, &resolved);
            }
            if ((address != NULL) &&
                (strcasecmp(address, wanted_address) == 0) &&
                (path != NULL) && (strlen(path) < path_capacity))
            {
                (void)snprintf(device_path, path_capacity, "%s", path);
                *services_resolved = (resolved != FALSE);
                return true;
            }
        }
        dbus_message_iter_next(&entries);
    }
    return false;
}

static bool ble_find_gatt_path(DBusMessage *objects,
                               const char *interface_name,
                               const char *uuid_property,
                               const char *wanted_uuid,
                               const char *parent_property,
                               const char *wanted_parent,
                               char *found_path,
                               size_t path_capacity)
{
    DBusMessageIter root;
    DBusMessageIter entries;

    if (!dbus_message_iter_init(objects, &root) ||
        (dbus_message_iter_get_arg_type(&root) != DBUS_TYPE_ARRAY))
    {
        return false;
    }
    dbus_message_iter_recurse(&root, &entries);
    while (dbus_message_iter_get_arg_type(&entries) == DBUS_TYPE_DICT_ENTRY)
    {
        DBusMessageIter entry;
        DBusMessageIter interfaces;
        DBusMessageIter properties;
        DBusMessageIter value;
        const char *path = NULL;
        const char *uuid = NULL;
        const char *parent = NULL;

        dbus_message_iter_recurse(&entries, &entry);
        dbus_message_iter_get_basic(&entry, &path);
        dbus_message_iter_next(&entry);
        interfaces = entry;
        if (ble_find_interface(&interfaces, interface_name, &properties) &&
            ble_dict_get(&properties, uuid_property, DBUS_TYPE_STRING, &value))
        {
            dbus_message_iter_get_basic(&value, &uuid);
            if ((uuid != NULL) &&
                (strcasecmp(uuid, wanted_uuid) == 0) &&
                ble_dict_get(&properties, parent_property, DBUS_TYPE_OBJECT_PATH,
                             &value))
            {
                dbus_message_iter_get_basic(&value, &parent);
                if ((parent != NULL) && (wanted_parent != NULL) &&
                    (strcmp(parent, wanted_parent) == 0) && (path != NULL) &&
                    (strlen(path) < path_capacity))
                {
                    (void)snprintf(found_path, path_capacity, "%s", path);
                    return true;
                }
            }
        }
        dbus_message_iter_next(&entries);
    }
    return false;
}

static int ble_start_discovery(ble_central_t *central)
{
    uint64_t next_tick = ble_monotonic_ms();

    if (ble_call_no_arguments(central, central->adapter_path, BLUEZ_ADAPTER,
                              "StartDiscovery") != 0)
    {
        return -1;
    }
    while (!ble_should_stop(central))
    {
        DBusMessage *objects = ble_get_managed_objects(central);
        bool resolved = false;
        char device_path[BLUEZ_MAX_GATT_PATH];

        if (objects == NULL)
        {
            (void)ble_call_no_arguments(central, central->adapter_path,
                                        BLUEZ_ADAPTER, "StopDiscovery");
            return -1;
        }
        if (ble_find_device(objects, central->device_address, device_path,
                            sizeof(device_path), &resolved))
        {
            (void)snprintf(central->device_path,
                           sizeof(central->device_path), "%s", device_path);
            dbus_message_unref(objects);
            (void)ble_call_no_arguments(central, central->adapter_path,
                                        BLUEZ_ADAPTER, "StopDiscovery");
            if (ble_call_no_arguments(central, device_path, BLUEZ_DEVICE,
                                      "Connect") != 0)
            {
                return -1;
            }
            for (unsigned int attempt = 0U; attempt < 150U; attempt++)
            {
                objects = ble_get_managed_objects(central);
                if (objects == NULL)
                {
                    return -1;
                }
                if (ble_find_device(objects, central->device_address,
                                    device_path, sizeof(device_path),
                                    &resolved) &&
                    resolved)
                {
                    bool found_service = ble_find_gatt_path(
                        objects, BLUEZ_SERVICE, "UUID",
                        MEDDISP_BLE_SERVICE_UUID, "Device", device_path,
                        central->characteristic_path,
                        sizeof(central->characteristic_path));
                    char service_path[BLUEZ_MAX_GATT_PATH];
                    if (found_service)
                    {
                        (void)snprintf(service_path, sizeof(service_path), "%s",
                                       central->characteristic_path);
                        if (ble_find_gatt_path(
                                objects, BLUEZ_CHARACTERISTIC, "UUID",
                                MEDDISP_BLE_MESSAGE_UUID, "Service",
                                service_path, central->characteristic_path,
                                sizeof(central->characteristic_path)))
                        {
                            dbus_message_unref(objects);
                            return 0;
                        }
                    }
                }
                dbus_message_unref(objects);
                (void)dbus_connection_read_write_dispatch(central->bus, 100);
                if (ble_monotonic_ms() >= next_tick)
                {
                    ble_tick_heartbeat_monitor(central);
                    next_tick = ble_monotonic_ms() + UINT64_C(1000);
                }
                if (ble_should_stop(central))
                {
                    break;
                }
            }
            return -1;
        }
        dbus_message_unref(objects);
        (void)dbus_connection_read_write_dispatch(central->bus, 250);
        if (ble_monotonic_ms() >= next_tick)
        {
            ble_tick_heartbeat_monitor(central);
            next_tick = ble_monotonic_ms() + UINT64_C(1000);
        }
    }
    (void)ble_call_no_arguments(central, central->adapter_path, BLUEZ_ADAPTER,
                                "StopDiscovery");
    return -1;
}

static int ble_start_notify(ble_central_t *central)
{
    return ble_call_no_arguments(central, central->characteristic_path,
                                 BLUEZ_CHARACTERISTIC, "StartNotify");
}

static int ble_write_fragment(ble_central_t *central,
                              const uint8_t *fragment,
                              size_t fragment_size)
{
    DBusMessage *request;
    DBusMessage *reply;
    DBusMessageIter arguments;
    DBusMessageIter value_array;
    DBusMessageIter options;
    DBusMessageIter option_entry;
    DBusError error;
    const char *method = "WriteValue";
    const char *dict_signature = "{sv}";
    int result = -1;

    request = dbus_message_new_method_call(
        BLUEZ_BUS_NAME, central->characteristic_path, BLUEZ_CHARACTERISTIC,
        method);
    if (request == NULL)
    {
        return -1;
    }
    dbus_message_iter_init_append(request, &arguments);
    if (!dbus_message_iter_open_container(&arguments, DBUS_TYPE_ARRAY,
                                           DBUS_TYPE_BYTE_AS_STRING,
                                           &value_array))
    {
        goto cleanup;
    }
    for (size_t index = 0U; index < fragment_size; index++)
    {
        unsigned char byte = fragment[index];
        if (!dbus_message_iter_append_basic(&value_array, DBUS_TYPE_BYTE,
                                            &byte))
        {
            goto cleanup;
        }
    }
    if (!dbus_message_iter_close_container(&arguments, &value_array) ||
        !dbus_message_iter_open_container(&arguments, DBUS_TYPE_ARRAY,
                                           dict_signature, &options) ||
        !dbus_message_iter_open_container(&options, DBUS_TYPE_DICT_ENTRY, NULL,
                                           &option_entry))
    {
        goto cleanup;
    }
    {
        const char *type_key = "type";
        const char *write_type = "request";
        DBusMessageIter variant;
        if (!dbus_message_iter_append_basic(&option_entry, DBUS_TYPE_STRING,
                                            &type_key) ||
            !dbus_message_iter_open_container(&option_entry, DBUS_TYPE_VARIANT,
                                               DBUS_TYPE_STRING_AS_STRING,
                                               &variant) ||
            !dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING,
                                            &write_type) ||
            !dbus_message_iter_close_container(&option_entry, &variant) ||
            !dbus_message_iter_close_container(&options, &option_entry) ||
            !dbus_message_iter_close_container(&arguments, &options))
        {
            goto cleanup;
        }
    }
    dbus_error_init(&error);
    reply = dbus_connection_send_with_reply_and_block(central->bus, request,
                                                       10000, &error);
    if (reply != NULL)
    {
        result = (dbus_message_get_type(reply) == DBUS_MESSAGE_TYPE_METHOD_RETURN)
                     ? 0
                     : -1;
        dbus_message_unref(reply);
    }
    if (dbus_error_is_set(&error))
    {
        dbus_error_free(&error);
    }

cleanup:
    dbus_message_unref(request);
    return result;
}

static int ble_send_wire_message(ble_central_t *central,
                                 const uint8_t *message,
                                 size_t message_size)
{
    size_t fragment_count;
    size_t offset = 0U;
    uint16_t message_id = central->next_message_id++;

    if ((message == NULL) || (message_size == 0U) ||
        (message_size > MEDDISP_BLE_MAX_REASSEMBLED_MESSAGE))
    {
        return -1;
    }
    fragment_count =
        (message_size + MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD - 1U) /
        MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD;
    if ((fragment_count == 0U) || (fragment_count > UINT16_MAX))
    {
        return -1;
    }
    for (size_t fragment_index = 0U; fragment_index < fragment_count;
         fragment_index++)
    {
        uint8_t fragment[MEDDISP_BLE_FRAGMENT_HEADER_SIZE +
                         MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD];
        size_t remaining = message_size - offset;
        size_t payload_size =
            (remaining > MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD)
                ? MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD
                : remaining;

        fragment[0] = (uint8_t)(message_id >> 8U);
        fragment[1] = (uint8_t)message_id;
        fragment[2] = (uint8_t)(fragment_index >> 8U);
        fragment[3] = (uint8_t)fragment_index;
        fragment[4] = (uint8_t)(fragment_count >> 8U);
        fragment[5] = (uint8_t)fragment_count;
        fragment[6] = (uint8_t)(payload_size >> 8U);
        fragment[7] = (uint8_t)payload_size;
        (void)memcpy(&fragment[MEDDISP_BLE_FRAGMENT_HEADER_SIZE],
                     &message[offset], payload_size);
        if (ble_write_fragment(central, fragment,
                               MEDDISP_BLE_FRAGMENT_HEADER_SIZE +
                                   payload_size) != 0)
        {
            return -1;
        }
        offset += payload_size;
    }
    return 0;
}

static bool ble_queue_packet(ble_central_t *central,
                             const uint8_t *bytes,
                             size_t size)
{
    size_t slot;

    if ((size < MEDDISP_BLE_FRAGMENT_HEADER_SIZE) ||
        (size > sizeof(central->packet_queue[0].bytes)) ||
        (central->packet_count >= BLE_PACKET_QUEUE_SIZE))
    {
        return false;
    }
    slot = (central->packet_head + central->packet_count) %
           BLE_PACKET_QUEUE_SIZE;
    (void)memcpy(central->packet_queue[slot].bytes, bytes, size);
    central->packet_queue[slot].size = size;
    central->packet_count++;
    return true;
}

static DBusHandlerResult ble_dbus_filter(DBusConnection *connection,
                                         DBusMessage *message,
                                         void *user_data)
{
    ble_central_t *central = user_data;
    DBusMessageIter arguments;
    DBusMessageIter changed;
    DBusMessageIter value;
    DBusMessageIter bytes;
    const char *interface_name = NULL;
    const char *path = dbus_message_get_path(message);
    const unsigned char *data = NULL;
    int data_size = 0;

    (void)connection;
    if (!dbus_message_is_signal(message, BLUEZ_PROPERTIES, "PropertiesChanged") ||
        (path == NULL) ||
        (strcmp(path, central->characteristic_path) != 0) ||
        !dbus_message_iter_init(message, &arguments) ||
        (dbus_message_iter_get_arg_type(&arguments) != DBUS_TYPE_STRING))
    {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    dbus_message_iter_get_basic(&arguments, &interface_name);
    if ((interface_name == NULL) ||
        (strcmp(interface_name, BLUEZ_CHARACTERISTIC) != 0))
    {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    if (!dbus_message_iter_next(&arguments))
    {
        central->fatal_error = true;
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    if (!ble_dict_contains(&arguments, "Value"))
    {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    if (!ble_dict_get(&arguments, "Value", DBUS_TYPE_ARRAY, &changed))
    {
        central->fatal_error = true;
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    dbus_message_iter_recurse(&changed, &bytes);
    if (dbus_message_iter_get_element_type(&changed) != DBUS_TYPE_BYTE)
    {
        central->fatal_error = true;
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    dbus_message_iter_get_fixed_array(&bytes, &data, &data_size);
    if ((data == NULL) || (data_size <= 0) ||
        !ble_queue_packet(central, data, (size_t)data_size))
    {
        central->fatal_error = true;
    }
    return DBUS_HANDLER_RESULT_HANDLED;
}

static int ble_secure_send(ble_central_t *central,
                           secure_channel_message_type_t message_type,
                           const uint8_t *payload,
                           size_t payload_size)
{
    uint8_t frame[SECURE_CHANNEL_MAX_MESSAGE_SIZE + SECURE_CHANNEL_OVERHEAD];
    size_t frame_size = 0U;

    if (secure_channel_seal(&central->secure_channel, message_type, payload,
                            payload_size, frame, sizeof(frame),
                            &frame_size) != 0)
    {
        return -1;
    }
    return ble_send_wire_message(central, frame, frame_size);
}

static int ble_heartbeat_acknowledge(void *context)
{
    return ble_secure_send(context, SECURE_CHANNEL_MESSAGE_ACKNOWLEDGEMENT,
                           NULL, 0U);
}

static int ble_heartbeat_alert(void *context)
{
    ble_central_t *central = context;

    return (central->on_heartbeat_timeout == NULL)
               ? -1
               : central->on_heartbeat_timeout(central->callback_context);
}

static int ble_reconnect_requested(void *context)
{
    ble_central_t *central = context;

    if (central->authenticated)
    {
        central->fatal_error = true;
    }

    return 0;
}

static void ble_tick_heartbeat_monitor(ble_central_t *central)
{
    if (heartbeat_monitor_tick(
            &central->heartbeat_monitor, ble_monotonic_ms(),
            ble_heartbeat_alert, ble_reconnect_requested, central) != 0)
    {
        ble_emit_event(central, BLE_CENTRAL_EVENT_ERROR,
                       "heartbeat monitor callback failed");
    }
}

static int ble_process_wire_message(ble_central_t *central,
                                    const uint8_t *message,
                                    size_t message_size)
{
    uint8_t handshake_output[SECURE_CHANNEL_SERVER_HELLO_SIZE];
    size_t handshake_output_size = 0U;
    bool established = false;

    if (!central->authenticated)
    {
        if (secure_channel_process_handshake(
                &central->secure_channel, message, message_size,
                handshake_output, sizeof(handshake_output),
                &handshake_output_size, &established) != 0)
        {
            return -1;
        }
        if ((handshake_output_size != 0U) &&
            (ble_send_wire_message(central, handshake_output,
                                   handshake_output_size) != 0))
        {
            return -1;
        }
        if (established)
        {
            if ((ble_set_authenticated(central, true) != 0) ||
                (heartbeat_monitor_on_connected(
                     &central->heartbeat_monitor, ble_monotonic_ms()) != 0))
            {
                return -1;
            }
            ble_emit_event(central, BLE_CENTRAL_EVENT_CONNECTED,
                           "authenticated BLE connection established");
        }
        return 0;
    }

    {
        uint8_t plaintext[SECURE_CHANNEL_MAX_MESSAGE_SIZE];
        size_t plaintext_size = 0U;
        secure_channel_message_type_t message_type;

        if (secure_channel_open(&central->secure_channel, message,
                                message_size, &message_type, plaintext,
                                sizeof(plaintext), &plaintext_size) != 0)
        {
            OPENSSL_cleanse(plaintext, sizeof(plaintext));
            return -1;
        }
        if (message_type == SECURE_CHANNEL_MESSAGE_HEARTBEAT)
        {
            if ((plaintext_size != 0U) ||
                (heartbeat_monitor_on_heartbeat(
                     &central->heartbeat_monitor, ble_monotonic_ms(),
                     ble_heartbeat_acknowledge, central) != 0))
            {
                OPENSSL_cleanse(plaintext, sizeof(plaintext));
                return -1;
            }
            OPENSSL_cleanse(plaintext, sizeof(plaintext));
            return 0;
        }
        else if (message_type == SECURE_CHANNEL_MESSAGE_DAY_END)
        {
            if ((plaintext_size != 0U) ||
                (heartbeat_monitor_on_day_end(&central->heartbeat_monitor) !=
                 0))
            {
                OPENSSL_cleanse(plaintext, sizeof(plaintext));
                return -1;
            }
        }

        if ((message_type == SECURE_CHANNEL_MESSAGE_STARTUP) ||
            (message_type == SECURE_CHANNEL_MESSAGE_ACKNOWLEDGEMENT) ||
            (central->on_message == NULL) ||
            (central->on_message(central->callback_context, message_type,
                                 plaintext, plaintext_size) != 0))
        {
            OPENSSL_cleanse(plaintext, sizeof(plaintext));
            return -1;
        }
        OPENSSL_cleanse(plaintext, sizeof(plaintext));
    }
    return 0;
}

static int ble_process_fragment(ble_central_t *central,
                                const uint8_t *fragment,
                                size_t fragment_size)
{
    uint16_t message_id;
    uint16_t fragment_index;
    uint16_t fragment_count;
    uint16_t data_size;
    uint64_t now = ble_monotonic_ms();

    if (fragment_size < MEDDISP_BLE_FRAGMENT_HEADER_SIZE)
    {
        return -1;
    }
    message_id = (uint16_t)(((uint16_t)fragment[0] << 8U) | fragment[1]);
    fragment_index = (uint16_t)(((uint16_t)fragment[2] << 8U) | fragment[3]);
    fragment_count = (uint16_t)(((uint16_t)fragment[4] << 8U) | fragment[5]);
    data_size = (uint16_t)(((uint16_t)fragment[6] << 8U) | fragment[7]);

    if ((data_size == 0U) ||
        (data_size > MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD) ||
        (fragment_size !=
         (MEDDISP_BLE_FRAGMENT_HEADER_SIZE + (size_t)data_size)) ||
        (fragment_count == 0U) || (fragment_count > BLE_MAX_FRAGMENTS) ||
        (fragment_index >= fragment_count))
    {
        return -1;
    }
    if (!central->reassembly_active)
    {
        if (fragment_index != 0U)
        {
            return -1;
        }
        central->reassembly_active = true;
        central->reassembly_id = message_id;
        central->reassembly_count = fragment_count;
        central->reassembly_next_index = 0U;
        central->reassembly_size = 0U;
        central->reassembly_started_ms = now;
    }
    if ((now < central->reassembly_started_ms) ||
        ((now - central->reassembly_started_ms) >
         BLE_FRAGMENT_TIMEOUT_MS) ||
        (message_id != central->reassembly_id) ||
        (fragment_count != central->reassembly_count) ||
        (fragment_index != central->reassembly_next_index) ||
        ((fragment_index + 1U < fragment_count) &&
         (data_size != MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD)) ||
        (central->reassembly_size + data_size > sizeof(central->reassembly)))
    {
        return -1;
    }
    (void)memcpy(&central->reassembly[central->reassembly_size],
                 &fragment[MEDDISP_BLE_FRAGMENT_HEADER_SIZE], data_size);
    central->reassembly_size += data_size;
    central->reassembly_started_ms = now;
    central->reassembly_next_index++;
    if (central->reassembly_next_index == central->reassembly_count)
    {
        size_t expected_count =
            (central->reassembly_size + MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD - 1U) /
            MEDDISP_BLE_MAX_FRAGMENT_PAYLOAD;
        int result = -1;

        if (expected_count == central->reassembly_count)
        {
            result = ble_process_wire_message(central, central->reassembly,
                                              central->reassembly_size);
        }
        central->reassembly_active = false;
        OPENSSL_cleanse(central->reassembly, central->reassembly_size);
        central->reassembly_size = 0U;
        return result;
    }
    return 0;
}

static int ble_process_queued_packets(ble_central_t *central)
{
    while (central->packet_count != 0U)
    {
        ble_packet_t *packet = &central->packet_queue[central->packet_head];
        int result;

        if (!central->authenticated && (packet->size >= 5U) &&
            (memcmp(packet->bytes, "MDP1", 4U) == 0))
        {
            result = ble_process_wire_message(central, packet->bytes,
                                              packet->size);
        }
        else
        {
            result = ble_process_fragment(central, packet->bytes, packet->size);
        }

        central->packet_head =
            (central->packet_head + 1U) % BLE_PACKET_QUEUE_SIZE;
        central->packet_count--;
        OPENSSL_cleanse(packet->bytes, sizeof(packet->bytes));
        if (result != 0)
        {
            return -1;
        }
    }
    return 0;
}

static int ble_process_send_request(ble_central_t *central,
                                    ble_send_request_t *request)
{
    return ble_secure_send(central, request->message_type, request->payload,
                           request->payload_size);
}

static ble_send_request_t *ble_pop_send_request(ble_central_t *central)
{
    ble_send_request_t *request = NULL;

    (void)pthread_mutex_lock(&central->mutex);
    if (central->send_head != NULL)
    {
        request = central->send_head;
        central->send_head = request->next;
        if (central->send_head == NULL)
        {
            central->send_tail = NULL;
        }
        central->send_count--;
    }
    (void)pthread_mutex_unlock(&central->mutex);
    return request;
}

static void ble_complete_send_request(ble_central_t *central,
                                      ble_send_request_t *request,
                                      int result)
{
    (void)pthread_mutex_lock(&central->mutex);
    request->result = result;
    request->completed = true;
    (void)pthread_cond_signal(&request->completed_condition);
    (void)pthread_mutex_unlock(&central->mutex);
}

static int ble_drain_send_queue(ble_central_t *central)
{
    ble_send_request_t *request;

    while ((request = ble_pop_send_request(central)) != NULL)
    {
        int result = ble_process_send_request(central, request);

        ble_complete_send_request(central, request, result);
        if (result != 0)
        {
            return -1;
        }
    }
    return 0;
}

static int ble_add_signal_match(ble_central_t *central)
{
    DBusError error;
    const char *rule =
        "type='signal',sender='org.bluez',"
        "interface='org.freedesktop.DBus.Properties',"
        "member='PropertiesChanged'";

    dbus_error_init(&error);
    dbus_bus_add_match(central->bus, rule, &error);
    if (dbus_error_is_set(&error))
    {
        dbus_error_free(&error);
        return -1;
    }
    if (!dbus_connection_add_filter(central->bus, ble_dbus_filter, central,
                                    NULL))
    {
        return -1;
    }
    return 0;
}

static void ble_fail_queued_requests(ble_central_t *central)
{
    ble_send_request_t *request;

    while ((request = ble_pop_send_request(central)) != NULL)
    {
        ble_complete_send_request(central, request, -1);
    }
}

static int ble_run_connected_session(ble_central_t *central)
{
    uint8_t first_message[SECURE_CHANNEL_CLIENT_HELLO_SIZE];
    size_t first_message_size = 0U;
    uint64_t next_heartbeat_tick = ble_monotonic_ms();
    uint64_t handshake_started_ms;
    bool was_authenticated = false;
    int result = -1;

    if (ble_start_notify(central) != 0 ||
        secure_channel_init(&central->secure_channel,
                            SECURE_CHANNEL_ROLE_CLIENT, central->psk) != 0 ||
        secure_channel_start(&central->secure_channel, first_message,
                             sizeof(first_message),
                             &first_message_size) != 0 ||
        ble_send_wire_message(central, first_message,
                              first_message_size) != 0)
    {
        goto cleanup;
    }
    handshake_started_ms = ble_monotonic_ms();

    while (!ble_should_stop(central) && !central->fatal_error)
    {
        uint64_t now;

        if (!dbus_connection_read_write_dispatch(central->bus, 100) ||
            (ble_process_queued_packets(central) != 0) ||
            (ble_drain_send_queue(central) != 0))
        {
            goto cleanup;
        }
        now = ble_monotonic_ms();
        if (!central->authenticated &&
            ((now < handshake_started_ms) ||
             ((now - handshake_started_ms) > BLE_HANDSHAKE_TIMEOUT_MS)))
        {
            goto cleanup;
        }
        if (central->reassembly_active &&
            ((now < central->reassembly_started_ms) ||
             ((now - central->reassembly_started_ms) >
              BLE_FRAGMENT_TIMEOUT_MS)))
        {
            goto cleanup;
        }
        if (now >= next_heartbeat_tick)
        {
            if (heartbeat_monitor_tick(
                    &central->heartbeat_monitor, now, ble_heartbeat_alert,
                    ble_reconnect_requested, central) != 0)
            {
                ble_emit_event(central, BLE_CENTRAL_EVENT_ERROR,
                               "heartbeat monitor callback failed");
                goto cleanup;
            }
            next_heartbeat_tick = now + UINT64_C(1000);
        }
    }
    result = central->fatal_error ? -1 : 0;

cleanup:
    (void)pthread_mutex_lock(&central->mutex);
    was_authenticated = central->authenticated;
    (void)pthread_mutex_unlock(&central->mutex);
    (void)ble_set_authenticated(central, false);
    (void)heartbeat_monitor_on_disconnected(&central->heartbeat_monitor);
    secure_channel_clear(&central->secure_channel);
    if (was_authenticated)
    {
        ble_emit_event(central, BLE_CENTRAL_EVENT_DISCONNECTED,
                       "BLE session disconnected");
    }
    if ((result != 0) && !ble_should_stop(central))
    {
        ble_emit_event(central, BLE_CENTRAL_EVENT_ERROR,
                       "secure BLE session failed");
    }
    ble_fail_queued_requests(central);
    (void)ble_call_no_arguments(central, central->characteristic_path,
                                BLUEZ_CHARACTERISTIC, "StopNotify");
    if (central->device_path[0] != '\0')
    {
        (void)ble_call_no_arguments(central, central->device_path,
                                    BLUEZ_DEVICE, "Disconnect");
        central->device_path[0] = '\0';
    }
    return result;
}

static void *ble_worker(void *argument)
{
    ble_central_t *central = argument;
    DBusError error;

    if (heartbeat_monitor_start(&central->heartbeat_monitor,
                                ble_monotonic_ms()) != 0)
    {
        ble_emit_event(central, BLE_CENTRAL_EVENT_ERROR,
                       "unable to start heartbeat monitor");
        goto finish;
    }

    dbus_error_init(&error);
    central->bus = dbus_bus_get(DBUS_BUS_SYSTEM, &error);
    if ((central->bus == NULL) || dbus_error_is_set(&error))
    {
        if (dbus_error_is_set(&error))
        {
            dbus_error_free(&error);
        }
        ble_emit_event(central, BLE_CENTRAL_EVENT_ERROR,
                       "unable to connect to the system D-Bus");
        goto finish;
    }
    dbus_connection_set_exit_on_disconnect(central->bus, FALSE);
    if (ble_add_signal_match(central) != 0)
    {
        ble_emit_event(central, BLE_CENTRAL_EVENT_ERROR,
                       "unable to subscribe to BlueZ notifications");
        goto finish;
    }

    while (!ble_should_stop(central))
    {
        central->fatal_error = false;
        central->packet_head = 0U;
        central->packet_count = 0U;
        central->reassembly_active = false;
        central->characteristic_path[0] = '\0';
        if (ble_start_discovery(central) == 0)
        {
            (void)ble_run_connected_session(central);
        }
        else
        {
            (void)heartbeat_monitor_on_disconnected(
                &central->heartbeat_monitor);
            if (!ble_should_stop(central))
            {
                ble_emit_event(central, BLE_CENTRAL_EVENT_ERROR,
                               "BlueZ discovery or GATT setup failed");
            }
            if (central->device_path[0] != '\0')
            {
                (void)ble_call_no_arguments(central, central->device_path,
                                            BLUEZ_DEVICE, "Disconnect");
                central->device_path[0] = '\0';
            }
        }
        if (ble_should_stop(central))
        {
            break;
        }
        for (unsigned int elapsed = 0U;
             (elapsed < BLE_CONNECT_RETRY_MS) && !ble_should_stop(central);
             elapsed += 100U)
        {
            (void)dbus_connection_read_write_dispatch(central->bus, 100);
            if ((elapsed % 1000U) == 0U &&
                (heartbeat_monitor_tick(
                     &central->heartbeat_monitor, ble_monotonic_ms(),
                     ble_heartbeat_alert, ble_reconnect_requested,
                     central) != 0))
            {
                ble_emit_event(central, BLE_CENTRAL_EVENT_ERROR,
                               "heartbeat monitor callback failed");
            }
        }
    }

finish:
    (void)ble_set_authenticated(central, false);
    ble_fail_queued_requests(central);
    if (central->bus != NULL)
    {
        dbus_connection_remove_filter(central->bus, ble_dbus_filter, central);
        dbus_connection_unref(central->bus);
        central->bus = NULL;
    }
    secure_channel_clear(&central->secure_channel);
    (void)pthread_mutex_lock(&central->mutex);
    central->worker_running = false;
    (void)pthread_cond_broadcast(&central->queue_condition);
    (void)pthread_mutex_unlock(&central->mutex);
    return NULL;
}

ble_central_t *ble_central_create(const ble_central_config_t *config)
{
    ble_central_t *central;
    const char *adapter_path;
    size_t index;
    uint8_t psk_bits = 0U;

    if ((config == NULL) || !ble_address_is_valid(config->device_address) ||
        (config->on_heartbeat_timeout == NULL) ||
        (config->on_message == NULL))
    {
        return NULL;
    }
    for (index = 0U; index < SECURE_CHANNEL_PSK_SIZE; index++)
    {
        psk_bits |= config->psk[index];
    }
    if (psk_bits == 0U)
    {
        return NULL;
    }
    adapter_path = (config->adapter_path == NULL) ? "/org/bluez/hci0"
                                                   : config->adapter_path;
    if ((adapter_path[0] != '/') ||
        (strlen(adapter_path) >= BLUEZ_MAX_GATT_PATH))
    {
        return NULL;
    }
    central = calloc(1U, sizeof(*central));
    if (central == NULL)
    {
        return NULL;
    }
    if (pthread_mutex_init(&central->mutex, NULL) != 0)
    {
        free(central);
        return NULL;
    }
    if (pthread_cond_init(&central->queue_condition, NULL) != 0)
    {
        (void)pthread_mutex_destroy(&central->mutex);
        free(central);
        return NULL;
    }
    (void)snprintf(central->device_address, sizeof(central->device_address),
                   "%s", config->device_address);
    for (index = 0U; index < BLE_CENTRAL_ADDRESS_SIZE - 1U; index++)
    {
        if ((central->device_address[index] >= 'a') &&
            (central->device_address[index] <= 'z'))
        {
            central->device_address[index] =
                (char)(central->device_address[index] - 'a' + 'A');
        }
    }
    (void)snprintf(central->adapter_path, sizeof(central->adapter_path), "%s",
                   adapter_path);
    (void)memcpy(central->psk, config->psk, sizeof(central->psk));
    central->on_event = config->on_event;
    central->on_message = config->on_message;
    central->on_heartbeat_timeout = config->on_heartbeat_timeout;
    central->callback_context = config->callback_context;
    central->next_message_id = 1U;
    heartbeat_monitor_init(&central->heartbeat_monitor);
    return central;
}

int ble_central_start(ble_central_t *central)
{
    if (central == NULL)
    {
        return -1;
    }
    (void)pthread_mutex_lock(&central->mutex);
    if (central->thread_started || central->worker_running)
    {
        (void)pthread_mutex_unlock(&central->mutex);
        return -1;
    }
    central->stop_requested = false;
    central->worker_running = true;
    if (pthread_create(&central->thread, NULL, ble_worker, central) != 0)
    {
        central->worker_running = false;
        (void)pthread_mutex_unlock(&central->mutex);
        return -1;
    }
    central->thread_started = true;
    (void)pthread_mutex_unlock(&central->mutex);
    return 0;
}

void ble_central_stop(ble_central_t *central)
{
    bool join_thread;

    if (central == NULL)
    {
        return;
    }
    (void)pthread_mutex_lock(&central->mutex);
    central->stop_requested = true;
    join_thread = central->thread_started;
    (void)pthread_cond_broadcast(&central->queue_condition);
    (void)pthread_mutex_unlock(&central->mutex);
    if (join_thread && !pthread_equal(pthread_self(), central->thread))
    {
        (void)pthread_join(central->thread, NULL);
        (void)pthread_mutex_lock(&central->mutex);
        central->thread_started = false;
        (void)pthread_mutex_unlock(&central->mutex);
    }
}

void ble_central_destroy(ble_central_t *central)
{
    if (central == NULL)
    {
        return;
    }
    ble_central_stop(central);
    OPENSSL_cleanse(central->psk, sizeof(central->psk));
    (void)pthread_cond_destroy(&central->queue_condition);
    (void)pthread_mutex_destroy(&central->mutex);
    free(central);
}

int ble_central_send(ble_central_t *central,
                     secure_channel_message_type_t message_type,
                     const uint8_t *payload,
                     size_t payload_size)
{
    ble_send_request_t request;

    if ((central == NULL) || !ble_known_message_type(message_type) ||
        ((message_type != SECURE_CHANNEL_MESSAGE_STARTUP) &&
         (message_type != SECURE_CHANNEL_MESSAGE_ACKNOWLEDGEMENT)) ||
        (payload_size > SECURE_CHANNEL_MAX_MESSAGE_SIZE) ||
        ((payload_size != 0U) && (payload == NULL)))
    {
        return -1;
    }
    if (central->thread_started &&
        pthread_equal(pthread_self(), central->thread))
    {
        return central->authenticated
                   ? ble_secure_send(central, message_type, payload,
                                     payload_size)
                   : -1;
    }

    (void)memset(&request, 0, sizeof(request));
    request.message_type = message_type;
    request.payload_size = payload_size;
    if (payload_size != 0U)
    {
        (void)memcpy(request.payload, payload, payload_size);
    }
    if (pthread_cond_init(&request.completed_condition, NULL) != 0)
    {
        return -1;
    }
    (void)pthread_mutex_lock(&central->mutex);
    if (!central->worker_running || !central->authenticated ||
        (central->send_count >= BLE_SEND_QUEUE_SIZE))
    {
        (void)pthread_mutex_unlock(&central->mutex);
        (void)pthread_cond_destroy(&request.completed_condition);
        return -1;
    }
    if (central->send_tail == NULL)
    {
        central->send_head = &request;
    }
    else
    {
        central->send_tail->next = &request;
    }
    central->send_tail = &request;
    central->send_count++;
    (void)pthread_cond_signal(&central->queue_condition);
    while (!request.completed && central->worker_running)
    {
        (void)pthread_cond_wait(&request.completed_condition,
                                &central->mutex);
    }
    if (!request.completed)
    {
        request.result = -1;
    }
    (void)pthread_mutex_unlock(&central->mutex);
    (void)pthread_cond_destroy(&request.completed_condition);
    OPENSSL_cleanse(request.payload, sizeof(request.payload));
    return request.result;
}

int ble_central_secure_send_payload(void *central,
                                    const uint8_t *payload,
                                    size_t payload_size)
{
    return ble_central_send(central, SECURE_CHANNEL_MESSAGE_STARTUP, payload,
                            payload_size);
}
