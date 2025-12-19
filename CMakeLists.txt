cmake_minimum_required(VERSION 3.25)
project(wasmc LANGUAGES C)

add_compile_options(-O3 -Wall -Wextra -Wpedantic)

add_executable(
    wasmc
    src/main.c
    src/wasm.c
    src/compile.c
    src/libqbe.c
    src/utils.c
    src/adlist.c
    src/rega.c
    src/aot.c
    src/rv32/emit.c
)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

