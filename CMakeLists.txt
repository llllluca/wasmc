cmake_minimum_required(VERSION 3.25)
project(wasmc LANGUAGES C)

add_compile_options(-ggdb -std=gnu17 -Wall -Wextra)

add_executable(
    wasmc
    src/main.c
    src/wasm.c
    src/compile.c
    src/libqbe.c
    src/rega.c
    src/aot.c
    src/rv32/emit.c
)

add_executable(readaot src/readaot.c)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

