idf_component_register(
    SRCS "report_buffer.c" "report_buffer_esp32.c"
    INCLUDE_DIRS "."
    REQUIRES startup_protocol freertos log
)
