/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * wamr_abi.h - WAMR-2.4.4 constants used by the AOT writer and the RV32
 * backend: AOT file format values and the runtime ABI (structure offsets,
 * exception ids, runtime symbols).
 *
 * Source: aotfmt/aotfmt.tex (reverse engineered from WAMR-2.4.4, git commit
 * 8c18e3f68b16c4bcaf05996b2636f6ed2b4cf629). Offsets are for 32-bit
 * runtimes built without shared memory, GC and multi-module. If WAMR
 * changes them, this is the only file to update.
 */
#ifndef WAMR_ABI_H_INCLUDED
#define WAMR_ABI_H_INCLUDED

/* ------------------------------------------------------------------------ */
/* AOT file format (aotfmt.tex sections 4-12)                               */
/* ------------------------------------------------------------------------ */

#define AOT_MAGIC_0 0x00
#define AOT_MAGIC_1 'a'
#define AOT_MAGIC_2 'o'
#define AOT_MAGIC_3 't'
#define AOT_VERSION 5u

enum {
    AOT_SEC_TARGET_INFO = 0,
    AOT_SEC_INIT_DATA = 1,
    AOT_SEC_TEXT = 2,
    AOT_SEC_FUNCTION = 3,
    AOT_SEC_EXPORT = 4,
    AOT_SEC_RELOCATION = 5,
    AOT_SEC_CUSTOM = 100,
};

/* target info for RV32, soft-float ILP32, no compressed instructions */
#define AOT_BIN_TYPE_ELF32L 0u
#define AOT_E_TYPE_REL      1u
#define AOT_E_MACHINE_RISCV 243u
#define AOT_E_VERSION       1u
#define AOT_E_FLAGS_RV32_SOFT                                                  \
    0u /* EF_RISCV_FLOAT_ABI_SOFT, no EF_RISCV_RVC                             \
        */
#define AOT_TARGET_ARCH "riscv32"

/* init expression types */
#define AOT_INIT_EXPR_NONE          0x00u
#define AOT_INIT_EXPR_I32_CONST     0x41u
#define AOT_INIT_EXPR_I64_CONST     0x42u
#define AOT_INIT_EXPR_F32_CONST     0x43u
#define AOT_INIT_EXPR_F64_CONST     0x44u
#define AOT_INIT_EXPR_GET_GLOBAL    0x23u
#define AOT_INIT_EXPR_FUNCREF_CONST 0xD2u

#define AOT_VALTYPE_FUNCREF    0x70u
#define AOT_MEMORY_HAS_MAX     0x01u
#define AOT_TABLE_HAS_MAX      0x01u
#define AOT_DEFAULT_PAGE_SIZE  65536u
#define AOT_DEFAULT_MAX_PAGES  65536u
#define AOT_TABLE_MIN_MAX_SIZE 1024u /* WASM_TABLE_MAX_SIZE */
#define AOT_NO_INDEX           0xFFFFFFFFu

/* relocation types applied by the RISC-V runtime (aotfmt.tex appendix A) */
#define R_RISCV_32           1u
#define R_RISCV_CALL         18u
#define R_RISCV_CALL_PLT     19u
#define R_RISCV_PCREL_HI20   23u
#define R_RISCV_PCREL_LO12_I 24u
#define R_RISCV_HI20         26u
#define R_RISCV_LO12_I       27u
#define R_RISCV_LO12_S       28u

/* ------------------------------------------------------------------------ */
/* Runtime ABI (aotfmt.tex section "The Interface Between the Code and the  */
/* Runtime")                                                                */
/* ------------------------------------------------------------------------ */

/* WASMExecEnv (32-bit: field i at 4*i) */
#define WAMR_EXEC_ENV_MODULE_INST           8
#define WAMR_EXEC_ENV_ARGV_BUF              12
#define WAMR_EXEC_ENV_NATIVE_STACK_BOUNDARY 16
#define WAMR_EXEC_ENV_AUX_STACK_BOUNDARY    24
#define WAMR_EXEC_ENV_AUX_STACK_BOTTOM      28
#define WAMR_ARGV_BUF_CELLS                 64

/* AOTModuleInstance (same layout on 32/64-bit runtimes) */
#define WAMR_INST_FUNC_PTRS         40
#define WAMR_INST_FUNC_TYPE_INDEXES 48
#define WAMR_INST_CUR_EXCEPTION     104
#define WAMR_INST_GLOBAL_TABLE_DATA 344
#define WAMR_MEMORY_INSTANCE_SIZE   120

/* AOTMemoryInstance, offsets from the module instance (memory 0) */
#define WAMR_INST_MEM_CUR_PAGE_COUNT    (WAMR_INST_GLOBAL_TABLE_DATA + 16)
#define WAMR_INST_MEM_DATA              (WAMR_INST_GLOBAL_TABLE_DATA + 32)
#define WAMR_INST_MEM_BOUND_CHECK_1BYTE (WAMR_INST_GLOBAL_TABLE_DATA + 80)

/* AOTTableInstance */
#define WAMR_TABLE_CUR_SIZE  16
#define WAMR_TABLE_ELEMS     24
#define WAMR_TABLE_ELEM_NULL 0xFFFFFFFFu

/* exception ids (WASMExceptionID) */
enum {
    WAMR_EXCE_UNREACHABLE = 0,
    WAMR_EXCE_OUT_OF_MEMORY = 1,
    WAMR_EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS = 2,
    WAMR_EXCE_INTEGER_OVERFLOW = 3,
    WAMR_EXCE_INTEGER_DIVIDE_BY_ZERO = 4,
    WAMR_EXCE_INVALID_CONVERSION_TO_INTEGER = 5,
    WAMR_EXCE_INVALID_FUNCTION_TYPE_INDEX = 6,
    WAMR_EXCE_INVALID_FUNCTION_INDEX = 7,
    WAMR_EXCE_UNDEFINED_ELEMENT = 8,
    WAMR_EXCE_UNINITIALIZED_ELEMENT = 9,
    WAMR_EXCE_CALL_UNLINKED_IMPORT_FUNC = 10,
    WAMR_EXCE_NATIVE_STACK_OVERFLOW = 11,
    WAMR_EXCE_AUX_STACK_OVERFLOW = 13,
    WAMR_EXCE_AUX_STACK_UNDERFLOW = 14,
};

/* AOTFloatCond, first argument of aot_intrinsic_f32_cmp/f64_cmp */
enum {
    WAMR_FLOAT_EQ = 0,
    WAMR_FLOAT_NE = 1,
    WAMR_FLOAT_LT = 2,
    WAMR_FLOAT_GT = 3,
    WAMR_FLOAT_LE = 4,
    WAMR_FLOAT_GE = 5,
    WAMR_FLOAT_UNO = 6,
};

/*
 * Runtime symbols the generated code may call (resolved through relocations,
 * aotfmt.tex Table "Common runtime symbols"). X(id, "name").
 */
#define WAMR_SYMBOLS(X)                                                        \
    X(SET_EXCEPTION_WITH_ID, "aot_set_exception_with_id")                      \
    X(INVOKE_NATIVE, "aot_invoke_native")                                      \
    X(CALL_INDIRECT, "aot_call_indirect")                                      \
    X(ENLARGE_MEMORY, "aot_enlarge_memory")                                    \
    X(F32_ADD, "aot_intrinsic_fadd_f32")                                       \
    X(F64_ADD, "aot_intrinsic_fadd_f64")                                       \
    X(F32_SUB, "aot_intrinsic_fsub_f32")                                       \
    X(F64_SUB, "aot_intrinsic_fsub_f64")                                       \
    X(F32_MUL, "aot_intrinsic_fmul_f32")                                       \
    X(F64_MUL, "aot_intrinsic_fmul_f64")                                       \
    X(F32_DIV, "aot_intrinsic_fdiv_f32")                                       \
    X(F64_DIV, "aot_intrinsic_fdiv_f64")                                       \
    X(F32_MIN, "aot_intrinsic_fmin_f32")                                       \
    X(F64_MIN, "aot_intrinsic_fmin_f64")                                       \
    X(F32_MAX, "aot_intrinsic_fmax_f32")                                       \
    X(F64_MAX, "aot_intrinsic_fmax_f64")                                       \
    X(F32_SQRT, "aot_intrinsic_sqrt_f32")                                      \
    X(F64_SQRT, "aot_intrinsic_sqrt_f64")                                      \
    X(F32_CEIL, "aot_intrinsic_ceil_f32")                                      \
    X(F64_CEIL, "aot_intrinsic_ceil_f64")                                      \
    X(F32_FLOOR, "aot_intrinsic_floor_f32")                                    \
    X(F64_FLOOR, "aot_intrinsic_floor_f64")                                    \
    X(F32_TRUNC, "aot_intrinsic_trunc_f32")                                    \
    X(F64_TRUNC, "aot_intrinsic_trunc_f64")                                    \
    X(F32_RINT, "aot_intrinsic_rint_f32")                                      \
    X(F64_RINT, "aot_intrinsic_rint_f64")                                      \
    X(F32_CMP, "aot_intrinsic_f32_cmp")                                        \
    X(F64_CMP, "aot_intrinsic_f64_cmp")                                        \
    X(I32_TO_F32, "aot_intrinsic_i32_to_f32")                                  \
    X(U32_TO_F32, "aot_intrinsic_u32_to_f32")                                  \
    X(I32_TO_F64, "aot_intrinsic_i32_to_f64")                                  \
    X(U32_TO_F64, "aot_intrinsic_u32_to_f64")                                  \
    X(I64_TO_F32, "aot_intrinsic_i64_to_f32")                                  \
    X(U64_TO_F32, "aot_intrinsic_u64_to_f32")                                  \
    X(I64_TO_F64, "aot_intrinsic_i64_to_f64")                                  \
    X(U64_TO_F64, "aot_intrinsic_u64_to_f64")                                  \
    X(F32_TO_I32, "aot_intrinsic_f32_to_i32")                                  \
    X(F32_TO_U32, "aot_intrinsic_f32_to_u32")                                  \
    X(F32_TO_I64, "aot_intrinsic_f32_to_i64")                                  \
    X(F32_TO_U64, "aot_intrinsic_f32_to_u64")                                  \
    X(F64_TO_I32, "aot_intrinsic_f64_to_i32")                                  \
    X(F64_TO_U32, "aot_intrinsic_f64_to_u32")                                  \
    X(F64_TO_I64, "aot_intrinsic_f64_to_i64")                                  \
    X(F64_TO_U64, "aot_intrinsic_f64_to_u64")                                  \
    X(F64_TO_F32, "aot_intrinsic_f64_to_f32")                                  \
    X(F32_TO_F64, "aot_intrinsic_f32_to_f64")                                  \
    X(I64_DIV_S, "aot_intrinsic_i64_div_s")                                    \
    X(I64_DIV_U, "aot_intrinsic_i64_div_u")                                    \
    X(I64_REM_S, "aot_intrinsic_i64_rem_s")                                    \
    X(I64_REM_U, "aot_intrinsic_i64_rem_u")

#define WAMR_SYM_ENUM(id, name) WAMR_SYM_##id,
enum { WAMR_SYMBOLS(WAMR_SYM_ENUM) WAMR_SYM__COUNT };
#undef WAMR_SYM_ENUM

#endif /* WAMR_ABI_H_INCLUDED */
