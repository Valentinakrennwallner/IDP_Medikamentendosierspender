idf_component_register(
    SRCS "persistence.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash weight mbedtls
)
