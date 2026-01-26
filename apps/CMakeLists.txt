add_executable(readaot readaot.c)
target_include_directories(readaot PRIVATE ${PROJECT_SOURCE_DIR}/src/)

add_executable(wasmc_exe wasmc.c)
set_property(TARGET wasmc_exe PROPERTY OUTPUT_NAME wasmc)
target_include_directories(wasmc_exe PRIVATE ${PROJECT_SOURCE_DIR}/include/wasmc/)
target_link_libraries(wasmc_exe wasmc_lib)

# How to change the install prefix:
# $mkdir build/ && cd build/
# $cmake --install-prefix ~/opt/wasmc/
# $make install
install(TARGETS wasmc_exe RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS readaot   RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
