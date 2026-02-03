if(ESP_PLATFORM)
    idf_component_register(
        SRC_DIRS ${COMPONENT_DIR}/src ${COMPONENT_DIR}/src/rv32
        INCLUDE_DIRS ${COMPONENT_DIR}/include/wasmc/ ${COMPONENT_DIR}/src
    )
    return()
endif()

add_library(wasmc_lib STATIC wasm.c compile.c ir.c rega.c aot.c rv32/emit.c)
set_property(TARGET wasmc_lib PROPERTY OUTPUT_NAME wasmc)
target_include_directories(
    wasmc_lib
    PUBLIC ${PROJECT_SOURCE_DIR}/include/wasmc/
    PRIVATE ${PROJECT_SOURCE_DIR}/src/)

