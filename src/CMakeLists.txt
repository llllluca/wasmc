add_library(
    wasmc_lib
    STATIC
    wasm.c
    compile.c
    libqbe.c
    rega.c
    aot.c
    rv32/emit.c)
set_property(TARGET wasmc_lib PROPERTY OUTPUT_NAME wasmc)
target_include_directories(
    wasmc_lib
    INTERFACE ${PROJECT_SOURCE_DIR}/include/wasmc/
    PRIVATE ${PROJECT_SOURCE_DIR}/src/)

