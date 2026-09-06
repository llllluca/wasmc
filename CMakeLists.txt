cmake_minimum_required(VERSION 3.25)

if(ESP_PLATFORM)
    idf_component_register(
        SRC_DIRS
            ${COMPONENT_DIR}/src
            ${COMPONENT_DIR}/src/rv32
        INCLUDE_DIRS
            ${COMPONENT_DIR}/include/
            ${COMPONENT_DIR}/src
    )
  return()
endif()

project(wasmc LANGUAGES C)

#Automatic creation of compile_commands.json used by language servers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-std=gnu17 -Wall -Wextra)

#Add 'Coverage' custom build type
set(CMAKE_C_FLAGS_COVERAGE "-O0 -ggdb -coverage")
if(CMAKE_BUILD_TYPE STREQUAL "Coverage")
    add_custom_target(coverage
    COMMAND lcov
        --directory ${CMAKE_CURRENT_BINARY_DIR}
        --capture
        --output-file coverage.info
        &&
        genhtml -o coverage coverage.info
    )
endif()

# Build libwasmc.a
add_library(
    libwasmc STATIC
    src/wasm.c
    src/compile.c
    src/ir.c
    src/rega.c
    src/aot.c
    src/rv32/emit.c
)
set_property(TARGET libwasmc PROPERTY OUTPUT_NAME wasmc)
target_include_directories(
    libwasmc
    PUBLIC include/wasmc
    PRIVATE src
)

#Build readaot
add_executable(readaot apps/readaot.c)
target_include_directories(readaot PRIVATE src)

#Build wasmc
add_executable(wasmc apps/wasmc.c)
target_include_directories(wasmc PRIVATE include/wasmc)
target_link_libraries(wasmc libwasmc)

# Build tests
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

