idf_component_register(
    SRCS "startup_receiver.c" "heartbeat_client.c"
    INCLUDE_DIRS "."
    REQUIRES startup_protocol
)
