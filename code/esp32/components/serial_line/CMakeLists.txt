idf_component_register(
    SRCS "serial_line.c" "serial_line_uart.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_uart freertos
)
