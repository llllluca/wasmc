#include "rv32i.h"
#include "../all.h"

/* emit_aot.c */
extern void rv32_init(AOTModule *aotm);
extern void rv32_emit_target_info(AOTModule *aotm);
extern void rv32_emit_init_data(AOTModule *aotm, const AOTInitData *init_data);
extern void rv32_init_text(AOTModule *aotm);
extern void rv32_emit_fn_text(AOTModule *aotm, Fn *fn, uint32_t type_index);
extern void rv32_finalize_text(AOTModule *aotm);
extern void rv32_emit_function(AOTModule *aotm);
extern void rv32_emit_export(AOTModule *aotm, uint32_t num_exports, WASMExport *exports);
extern void rv32_emit_relocation(AOTModule *aotm);
extern void rv32_finalize(AOTModule *aotm, uint8_t **buf, uint32_t *buf_len);

const rv32_reg rv32_gp_reg[RV32_GP_NUM_REG] = {
    T0, T1, T2, T3, T4,
    S1, S2, S3, S4, S5, S6, S7, S8, S9, S10, S11, 
};

const rv32_reg rv32_arg_reg[RV32_ARG_NUM_REG] = {
    A0, A1, A2, A3, A4, A5, A6, A7,
};

const rv32_reg rv32_reserved_reg[RV32_RESERVED_NUM_REG] = {
    T5, T6
};

Target rv32 = {
    .name = "riscv32",
    .init = rv32_init,
    .emit_target_info = rv32_emit_target_info,
    .emit_init_data = rv32_emit_init_data,
    .init_text = rv32_init_text,
    .emit_fn_text = rv32_emit_fn_text,
    .finalize_text = rv32_finalize_text,
    .emit_function = rv32_emit_function,
    .emit_export = rv32_emit_export,
    .emit_relocation = rv32_emit_relocation,
    .finalize = rv32_finalize,
};


