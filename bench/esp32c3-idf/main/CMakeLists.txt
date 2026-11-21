idf_build_get_property(req BENCH_REQ)
idf_build_get_property(bin BENCH_BIN)
idf_component_register(SRCS "main.c"
                       EMBED_FILES "${bin}"
                       PRIV_REQUIRES ${req} esp_timer pthread freertos heap)
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_build_get_property(defs BENCH_DEFS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ${defs})
endif()
