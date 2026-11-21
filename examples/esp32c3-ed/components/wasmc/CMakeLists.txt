# wasmc as an ESP-IDF component, built from the repository's sources: the
# public headers (include/wasmc/) are visible to the components that
# require it, the private ones (src/) only to wasmc itself.
get_filename_component(WASMC_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.."
                       ABSOLUTE)
file(GLOB WASMC_SRCS "${WASMC_ROOT}/src/*.c")

idf_component_register(SRCS ${WASMC_SRCS}
                       INCLUDE_DIRS "${WASMC_ROOT}/include"
                       PRIV_INCLUDE_DIRS "${WASMC_ROOT}/src")
