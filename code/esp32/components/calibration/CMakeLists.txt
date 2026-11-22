idf_component_register(
    SRCS "calibration.c" "calibration_uart.c"
    INCLUDE_DIRS "."
    REQUIRES weight persistence esp_driver_uart freertos
)
