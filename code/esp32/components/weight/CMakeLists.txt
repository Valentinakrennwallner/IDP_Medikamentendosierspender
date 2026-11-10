idf_component_register(
    SRCS "weight.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer esp_rom freertos
)
