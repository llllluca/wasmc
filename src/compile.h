/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * compile.h - WASM 1.0 function body -> ir_func compiler.
 *
 * One defined function is compiled at a time into an ir_func, building SSA
 * form on the fly (Braun et al., "Simple and Efficient Construction of
 * Static Single Assignment Form", CC 2013) while walking the body once with
 * wdec's raw, stack-less wdec_instr_iter_t.
 *
 * The compiler never allocates: every array it needs (operand and control
 * stacks, per-block definition lists, ...) lives in the scratch of the
 * output ir_func (ir_scratch_alloc()), in chunks from its allocator, and is
 * released with it (ir_free(), or emit_func(), which reuses the scratch).
 */
#ifndef WCOMPILE_H_INCLUDED
#define WCOMPILE_H_INCLUDED

#include "ir.h"
#include "wasm_decode.h"

#include <stdbool.h>

/**
 * wcompile_func() - Compile one defined WASM function into IR.
 * @mod: the module. It must already have passed wdec_decode() AND
 *     wdec_validate() IN FULL (the whole module, not just this function).
 * @func: the defined function of @mod to compile. To compile every function,
 *     take them from wdec_func_iter_next(): wdec_get_func() walks the code
 *     section from the start on each call.
 * @ftype: @func's type, the entry @func->typeidx of @mod's type section.
 * @out: the IR to build, just ir_init()'d with limits for this function
 *     (see wmeasure_func()).
 * @caps: the operand-stack / control-stack depths to support. Use the
 *     high-water marks of the validator (wdec_validator_max_depth()): if
 *     validation succeeded, the real depths of every function are provably
 *     within them. The stacks themselves live in @out's scratch region.
 *
 * wcompile_func() does not re-run WASM's type checking: it trusts that @mod
 * is well-typed. On an unvalidated or invalid module its behaviour is
 * undefined (it may build nonsensical IR), although every wdec_* accessor
 * it calls stays bounds-safe, so it does not read past wdec's own buffers.
 *
 * On success @out holds a finalized ir_func (ir_finalize() already called),
 * ready for passes and emission. On failure @out is left partially built,
 * for the caller to ir_free().
 *
 * Return: %WASMC_OK on success, otherwise the first error hit:
 * a decoder error from a wdec_* accessor (e.g. %WASMC_ERR_FUNC_INDEX);
 * a %WASMC_ERR_IR_* from @out (e.g. out of capacity or scratch);
 * %WASMC_ERR_OPD_STACK_OVERFLOW or %WASMC_ERR_CTRL_STACK_OVERFLOW if @caps
 * is too small; %WASMC_ERR_INVALID_ARG if the function has more than
 * UINT16_MAX locals, a call_indirect type index exceeds UINT16_MAX, or an
 * instruction is not recognized (only possible on an invalid module).
 */
wasmc_err_t wcompile_func(const wdec_module_t *mod, const wdec_func_t *func,
                          const wdec_functype_t *ftype, ir_func *out,
                          wdec_stack_depth caps);

#endif /* WCOMPILE_H_INCLUDED */
