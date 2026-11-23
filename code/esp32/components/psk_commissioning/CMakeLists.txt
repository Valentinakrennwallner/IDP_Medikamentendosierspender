idf_component_register(
    SRCS "psk_commissioning.c" "psk_commissioning_uart.c"
    INCLUDE_DIRS "."
    REQUIRES persistence serial_line mbedtls
)
