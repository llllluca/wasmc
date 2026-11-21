# wasmc on the ESP32-C3: compile WebAssembly to AOT on the device, then run
# it with the WAMR AOT runtime.
cmake_minimum_required(VERSION 3.16)

# WAMR's ESP-IDF component (wasm-micro-runtime/build-scripts/esp-idf/wamr):
# $WAMR_PATH, or the clone at the root of this repository.
if(DEFINED ENV{WAMR_PATH})
    set(WAMR_PATH "$ENV{WAMR_PATH}")
else()
    get_filename_component(WAMR_PATH
        "${CMAKE_CURRENT_LIST_DIR}/../../wasm-micro-runtime" ABSOLUTE)
endif()
list(APPEND EXTRA_COMPONENT_DIRS "${WAMR_PATH}/build-scripts/esp-idf")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# build only the components main needs
idf_build_set_property(MINIMAL_BUILD ON)
project(wasmc_demo)
