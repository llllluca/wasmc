/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * measure.h - Pre-pass that sizes the IR of a function before it is built.
 *
 * The IR of a function (ir.h) has fixed capacities of blocks, edges and
 * instruction slots (the "low" region of the slot array), which must be known
 * before ir_init(); only its phi region and its scratch grow as needed. One
 * pass over the function body, before compile.c builds its IR, computes them:
 *
 *   - blocks, edges, insn slots: exact upper bounds, mirroring what compile.c
 *     emits for each operator, dead code included;
 *   - phi slots: the first capacity of the phi region, an estimate, because
 *     how many phis the SSA construction creates depends on the data flow.
 *
 * The pass needs only a few bytes of memory and allocates no IR.
 *
 * Whenever compile.c changes what it emits for an operator, this file must
 * change with it: a count too low makes the IR run out of blocks, edges or
 * slots, and the function fail to compile.
 */
#ifndef MEASURE_H_INCLUDED
#define MEASURE_H_INCLUDED

#include "ir.h"
#include "wasm_decode.h"
#include <stdint.h>

/**
 * struct wmeasure_t - Measurements of one function.
 * @body_size: bytes of the function body.
 * @nlocals: parameters + declared locals.
 * @nwritten: distinct locals written by local.set/local.tee.
 * @blocks: upper bound of the IR blocks.
 * @edges: upper bound of the CFG edges.
 * @insn_slots: upper bound of the instruction slots (the low region).
 * @merges: blocks that may have 2+ predecessors: the only places phis go.
 * @phi_slots: first capacity of the phi region, in slots: an estimate,
 *     capped by an upper bound.
 */
typedef struct {
    uint32_t body_size;
    uint32_t nlocals;
    uint32_t nwritten;

    uint32_t blocks;
    uint32_t edges;
    uint32_t insn_slots;
    uint32_t merges;

    uint32_t phi_slots;
} wmeasure_t;

/**
 * wmeasure_func() - Measure one defined function.
 * @mod: the module; it must have passed wdec_validate().
 * @func: the defined function of @mod to measure. To measure every function
 *     in linear time, take them from wdec_func_iter_next().
 * @ftype: @func's type.
 * @caps: the validator's stack high-water marks (wdec_validator_max_depth()).
 * @tmp: temporary memory, any alignment, not used after the call returns:
 *     a bitmap of the locals, a byte per open control frame and the
 *     simulated constant cache.
 * @tmp_len: size of @tmp; it needs nlocals / 8 + @caps.ctrl + 2 +
 *     %WMEASURE_CONST_TMP bytes.
 * @out: filled with the measurements.
 *
 * Return: %WASMC_OK, %WASMC_ERR_TOO_MANY_LOCALS, a decoder error, or
 * %WASMC_ERR_INVALID_ARG if @tmp is too small.
 */
/* bytes of @tmp for the simulated constant cache: an entry per
 * ir_func.const_cache entry (kind, bits) */
enum {
    WMEASURE_CONST_KEY = 9,
    WMEASURE_CONST_TMP = IR_CONST_CACHE * WMEASURE_CONST_KEY,
};

wasmc_err_t wmeasure_func(const wdec_module_t *mod, const wdec_func_t *func,
                          const wdec_functype_t *ftype, wdec_stack_depth caps,
                          uint8_t *tmp, uint32_t tmp_len, wmeasure_t *out);

#endif /* MEASURE_H_INCLUDED */
