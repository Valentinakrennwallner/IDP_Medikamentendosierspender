idf_component_register(
    SRCS "wall_clock.c" "wall_clock_esp32.c"
    INCLUDE_DIRS "."
    REQUIRES dosing esp_timer freertos log
)
