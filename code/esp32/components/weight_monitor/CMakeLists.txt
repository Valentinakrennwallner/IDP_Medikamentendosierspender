idf_component_register(
    SRCS "weight_monitor.c"
    INCLUDE_DIRS "."
    REQUIRES weight sensor_fault
)
