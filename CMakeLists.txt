cmake_minimum_required(VERSION 3.25)
project(wasmc LANGUAGES C)

#Debug is the default build type
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

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
        --output-file ${CMAKE_SOURCE_DIR}/coverage.info
        &&
        genhtml
        -o ${CMAKE_SOURCE_DIR}/coverage
        ${CMAKE_SOURCE_DIR}/coverage.info)
endif()

add_subdirectory(${PROJECT_SOURCE_DIR}/src/)
add_subdirectory(${PROJECT_SOURCE_DIR}/apps/)

#Add 'uninstall' target
if(NOT TARGET uninstall)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    IMMEDIATE @ONLY)

  add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(${PROJECT_SOURCE_DIR}/tests/)
endif()

