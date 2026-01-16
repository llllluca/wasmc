cmake_minimum_required(VERSION 3.25)
project(wasmc LANGUAGES C)

set(CMAKE_EXPORT_COMPILE_COMMANDS YES)
add_compile_options(-ggdb -std=gnu17 -Wall -Wextra)

add_subdirectory(${PROJECT_SOURCE_DIR}/src/)
add_subdirectory(${PROJECT_SOURCE_DIR}/apps/)

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

