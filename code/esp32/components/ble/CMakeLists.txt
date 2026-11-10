idf_component_register(
    SRCS "startup_receiver.c" "heartbeat_client.c"
         "ble_secure_transport.c" "secure_channel_mbedtls.c"
    INCLUDE_DIRS "."
    REQUIRES startup_protocol bt mbedtls esp_timer
)
