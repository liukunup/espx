idf_component_register(
    SRCS "cli_service.c"
    INCLUDE_DIRS "."
    REQUIRES esp_console linenoise esp_system driver uart
)
