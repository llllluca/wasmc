cmake_minimum_required(VERSION 3.25)
project(wasmc LANGUAGES C)

add_compile_options(-std=c99 -ggdb -Wall -Wextra -Wpedantic)

add_executable(
    wasmc
    "src/main.c"
    "src/libqbe.c"
    "src/parse.c"
    "src/compile.c"
    "src/adlist.c"
    "src/utils.c"
    "src/ssa.c"
    "src/rega.c"
    "src/rv32/emit_aot.c"
    "src/rv32/target.c")


