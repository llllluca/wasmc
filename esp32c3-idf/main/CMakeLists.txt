set(WASMC_SRC_DIR "../../src/")

idf_component_register(SRCS
    "wasmc.c"
    "${WASMC_SRC_DIR}/parse.c"
    "${WASMC_SRC_DIR}/compile.c"
    "${WASMC_SRC_DIR}/adlist.c"
    "${WASMC_SRC_DIR}/utils.c"
    "${WASMC_SRC_DIR}/ssa.c"
    "${WASMC_SRC_DIR}/libqbe.c"
    INCLUDE_DIRS "${WASMC_SRC_DIR}")
