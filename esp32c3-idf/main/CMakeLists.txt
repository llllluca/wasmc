set(WASMC_SRC_DIR "../../src/")

idf_component_register(SRCS
    "wamr.c"
    "${WASMC_SRC_DIR}/wasm.c"
    "${WASMC_SRC_DIR}/compile.c"
    "${WASMC_SRC_DIR}/adlist.c"
    "${WASMC_SRC_DIR}/utils.c"
    "${WASMC_SRC_DIR}/libqbe.c"
    "${WASMC_SRC_DIR}/rega.c"
    "${WASMC_SRC_DIR}/aot.c"
    "${WASMC_SRC_DIR}/rv32/emit.c"
    INCLUDE_DIRS "${WASMC_SRC_DIR}")

