# Minimal wasmc benchmark: one module per firmware, one mode per build.
#   idf.py -B build-<mode> -DBENCH_MODE=wasmc|aot|interp \
#          -DBENCH_MODULE=/path/x.wasm build flash
cmake_minimum_required(VERSION 3.16)

set(BENCH_MODE "interp" CACHE STRING "wasmc, aot or interp")
set(BENCH_MODULE "" CACHE FILEPATH "the .wasm to benchmark")
set(BENCH_ENTRY "__main_void" CACHE STRING "exported function to call")
set(BENCH_REPEAT "3" CACHE STRING "measures per boot")
option(BENCH_EXECUTE_FUNC "aot/interp: call the entry with wasm_application_execute_func() instead of wasm_runtime_call_wasm()" OFF)
set(WAMRC "wamrc" CACHE STRING "wamrc, for BENCH_MODE=aot")
set(WAMRC_FLAGS "--target=riscv32;--target-abi=ilp32;--cpu=generic-rv32;--cpu-features=+m,+c"
    CACHE STRING "wamrc options")

if(NOT EXISTS "${BENCH_MODULE}")
    message(FATAL_ERROR "set -DBENCH_MODULE=/path/to/module.wasm")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${BENCH_MODULE}")

# The firmware embeds module.bin: the .wasm, or for aot the AOT file that
# wamrc compiles from it here on the host.
set(BIN "${CMAKE_BINARY_DIR}/module.bin")
if(BENCH_MODE STREQUAL "aot")
    execute_process(COMMAND ${WAMRC} ${WAMRC_FLAGS} -o "${BIN}" "${BENCH_MODULE}"
                    RESULT_VARIABLE rc OUTPUT_QUIET)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "wamrc failed on ${BENCH_MODULE}")
    endif()
    set(REQ wamr)
elseif(BENCH_MODE STREQUAL "interp")
    configure_file("${BENCH_MODULE}" "${BIN}" COPYONLY)
    set(REQ wamr)
elseif(BENCH_MODE STREQUAL "wasmc")
    # wasmc compiles the .wasm on the device, then WAMR runs its AOT file
    configure_file("${BENCH_MODULE}" "${BIN}" COPYONLY)
    set(REQ wasmc wamr)
else()
    message(FATAL_ERROR "BENCH_MODE must be wasmc, aot or interp")
endif()
get_filename_component(BENCH_NAME "${BENCH_MODULE}" NAME_WE)

if(DEFINED ENV{WAMR_PATH})
    set(WAMR_PATH "$ENV{WAMR_PATH}")
else()
    get_filename_component(WAMR_PATH
        "${CMAKE_CURRENT_LIST_DIR}/../../wasm-micro-runtime" ABSOLUTE)
endif()
list(APPEND EXTRA_COMPONENT_DIRS "${WAMR_PATH}/build-scripts/esp-idf")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
# main/CMakeLists.txt reads these (it cannot see the -D variables)
idf_build_set_property(BENCH_REQ "${REQ}")
idf_build_set_property(BENCH_BIN "${BIN}")
string(TOUPPER "${BENCH_MODE}" MODE_UC)
idf_build_set_property(BENCH_DEFS "MODE_${MODE_UC}=1;BENCH_MODE=\"${BENCH_MODE}\";BENCH_NAME=\"${BENCH_NAME}\";BENCH_ENTRY=\"${BENCH_ENTRY}\";BENCH_REPEAT=${BENCH_REPEAT};BENCH_EXECUTE_FUNC=$<BOOL:${BENCH_EXECUTE_FUNC}>")
project(wasmc_mini_bench)
