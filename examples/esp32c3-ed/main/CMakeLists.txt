idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES wasmc wamr pthread esp_timer vfs
                                     esp_driver_uart joltwallet__littlefs)
