/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * emit.h - RV32IM code generator.
 *
 * Values live in the registers or stack slots chosen by ra.c (linear
 * scan); instruction selection works on scratch registers, reading simple
 * operands straight from their registers.
 */
#ifndef EMIT_H_INCLUDED
#define EMIT_H_INCLUDED

#include "aotw.h"
#include "ir.h"
#include "wasmc/wasmc.h"

/*
 * Emits the code of defined function `funcidx`, whose finalized IR is `func`
 * and whose type is `ftype`, as the next function of the AOT file `writer`.
 * Uses `func`'s scratch region (released first: the builder's data is no
 * longer needed).
 *
 * Returns WASMC_OK, WASMC_ERR_UNSUPPORTED for constructs not implemented
 * yet, WASMC_ERR_OUT_OF_MEMORY if a scratch chunk could not be allocated,
 * or an error from the writer.
 */
wasmc_err_t emit_func(ir_func *func, const wdec_module_t *mod, uint32_t funcidx,
                      const wdec_functype_t *ftype, aotw_t *writer,
                      const wasmc_options *opt);

#endif /* EMIT_H_INCLUDED */
