set(WASMC_SRC_DIR "../../src/")

idf_component_register(SRCS
    "wasmc.c"
    "${WASMC_SRC_DIR}/parse.c"
    "${WASMC_SRC_DIR}/compile.c"
    "${WASMC_SRC_DIR}/dalist.c"
    "${WASMC_SRC_DIR}/utils.c"
    "${WASMC_SRC_DIR}/libqbe.c"
    "${WASMC_SRC_DIR}/qbe-1.2/util.c"
    "${WASMC_SRC_DIR}/qbe-1.2/abi.c"
    "${WASMC_SRC_DIR}/qbe-1.2/cfg.c"
    "${WASMC_SRC_DIR}/qbe-1.2/mem.c"
    "${WASMC_SRC_DIR}/qbe-1.2/ssa.c"
    "${WASMC_SRC_DIR}/qbe-1.2/alias.c"
    "${WASMC_SRC_DIR}/qbe-1.2/load.c"
    "${WASMC_SRC_DIR}/qbe-1.2/copy.c"
    "${WASMC_SRC_DIR}/qbe-1.2/fold.c"
    "${WASMC_SRC_DIR}/qbe-1.2/simpl.c"
    "${WASMC_SRC_DIR}/qbe-1.2/live.c"
    "${WASMC_SRC_DIR}/qbe-1.2/spill.c"
    "${WASMC_SRC_DIR}/qbe-1.2/rega.c"
    "${WASMC_SRC_DIR}/qbe-1.2/emit.c"
    "${WASMC_SRC_DIR}/qbe-1.2/amd64/targ.c"
    "${WASMC_SRC_DIR}/qbe-1.2/amd64/sysv.c"
    "${WASMC_SRC_DIR}/qbe-1.2/amd64/isel.c"
    "${WASMC_SRC_DIR}/qbe-1.2/amd64/emit.c"

    INCLUDE_DIRS "${WASMC_SRC_DIR}")
