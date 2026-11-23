idf_component_register(
    SRCS "calibration.c" "calibration_uart.c"
    INCLUDE_DIRS "."
    REQUIRES weight persistence serial_line
)
