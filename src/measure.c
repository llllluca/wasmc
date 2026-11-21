/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * measure.c - Pre-pass that sizes the IR of a function (see measure.h).
 *
 * Every count below mirrors what compile.c emits for the same operator: a
 * block, edge or slot compile.c may create and this file does not count is
 * a bug, which makes the function fail to compile (WASMC_ERR_IR_*).
 */
#include "measure.h"
#include "bitmap.h"
#include "ir.h"
#include "wasm_decode.h"
#include "wasmc/error.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* One byte per open control frame: its kind (FR_KIND bits) and flags. */
#define FR_BLOCK    0u
#define FR_LOOP     1u
#define FR_IF       2u
#define FR_KIND     3u
#define FR_ELSE     4u /* IF: `else` seen                                  */
#define FR_TARGETED 8u /* BLOCK: some br/br_if/br_table targets its join   */

/*
 * The IR builder shares a constant equal to one already emitted in the
 * function (ir.h, ir_func.const_cache): a direct-mapped cache of recent
 * constants. This pass simulates it, with the same entries
 * (ir_const_cache_idx()), on every constant it reads, the unreachable ones
 * included, to count a cached one as free. The count stays an upper bound:
 * the IR sees a subsequence of these constants (it skips unreachable code),
 * in the same order, and in a direct-mapped cache each entry's constants
 * are a sequence in which a constant costs a slot exactly when it differs
 * from the one before it. Taking x out of a, x, b replaces the costs of x
 * and b, [a != x] + [x != b], with [a != b], never more (a != b implies a
 * != x or x != b): a subsequence never costs more. (A set-associative cache
 * with moves or replacement choices would lose this.) Entries hold the
 * kind (0: empty) and the bits.
 */

/* slots taken by `n` operand references, packed 4 per slot */
static uint32_t pack_slots(uint32_t n)
{
    return (n + 3U) >> 2;
}

/* true for the loads and stores, which have a memarg */
static int is_mem_op(wdec_opcode_t opcode)
{
    return opcode >= WDEC_OP_I32_LOAD && opcode <= WDEC_OP_I64_STORE32;
}

/* which bit of a set of value types `type` is */
static unsigned type_bit(wdec_valtype_t type)
{
    enum { SEEN_I32 = 1, SEEN_I64 = 2, SEEN_F32 = 4, SEEN_F64 = 8 };
    switch (type) {
    case WDEC_TYPE_I32:
        return SEEN_I32;
    case WDEC_TYPE_I64:
        return SEEN_I64;
    case WDEC_TYPE_F32:
        return SEEN_F32;
    default:
        return SEEN_F64;
    }
}

/**
 * struct measure_state - State of the pass over a body.
 * @mod: the module, for the types of the calls.
 * @out: the measurements being gathered.
 * @written: bitmap of the locals written.
 * @frames: one FR_* byte per open frame.
 * @consts: the simulated constant cache, %WMEASURE_CONST_KEY bytes per
 *     entry.
 * @depth: open frames, the function body itself excluded.
 * @ctrl_cap: capacity of @frames.
 */
typedef struct {
    const wdec_module_t *mod;
    wmeasure_t *out;
    uint8_t *written;
    uint8_t *frames;
    uint8_t *consts;
    uint32_t depth;
    uint32_t ctrl_cap;
} measure_state;

/**
 * const_slots() - Count a constant the IR builder emits.
 * @state: the pass state.
 * @wide: CONST64 (i64, f64), else CONST32.
 * @type: its IR_T_* type.
 * @bits: its bits.
 *
 * Return: 0 if the IR is sure to share an equal constant (see the top of
 *   this file), else its slots: 2 for CONST64, 1 for CONST32.
 */
/* cppcheck-suppress constParameterPointer ; it updates state's cache */
static uint32_t const_slots(measure_state *state, int wide, unsigned type,
                            uint64_t bits)
{
    uint8_t *entry = state->consts + ((size_t)ir_const_cache_idx(type, bits) *
                                      WMEASURE_CONST_KEY);
    const uint8_t kind = (uint8_t)(1U + (type << 1) + (wide ? 1U : 0U));
    uint64_t stored;
    memcpy(&stored, entry + 1, sizeof stored);
    if (entry[0] == kind && stored == bits) {
        return 0;
    }
    entry[0] = kind;
    memcpy(entry + 1, &bits, sizeof bits);
    return wide ? 2U : 1U;
}

/* IR_T_* of a value type */
static unsigned ir_type_of(wdec_valtype_t type)
{
    switch (type) {
    case WDEC_TYPE_I32:
        return IR_T_I32;
    case WDEC_TYPE_I64:
        return IR_T_I64;
    case WDEC_TYPE_F32:
        return IR_T_F32;
    default:
        return IR_T_F64;
    }
}

/* the bits of a little-endian float immediate */
static uint64_t read_le(const uint8_t *ptr, unsigned size)
{
    uint64_t val = 0;
    for (unsigned i = size; i-- > 0;) {
        val = (val << CHAR_BIT) | ptr[i];
    }
    return val;
}

/**
 * measure_entry() - Count the function entry.
 * @state: the pass state.
 * @func: the function.
 * @ftype: its type.
 *
 * The entry is one block holding a PARAM per parameter and one zero
 * constant per value type of the declared locals (their initial value), in
 * the order of the locals, as compile.c's emit_entry() emits them.
 *
 * Return: %WASMC_OK or a decoder error from the locals declarations.
 */
static wasmc_err_t measure_entry(measure_state *state, const wdec_func_t *func,
                                 const wdec_functype_t *ftype)
{
    wmeasure_t *out = state->out;
    unsigned seen_types = 0;
    wdec_locals_iter_t locals_it;
    uint32_t cnt;
    wdec_valtype_t valtype;
    out->blocks = 1;
    out->edges = 0;
    out->insn_slots = ftype->param_count;
    WDEC_FOR_EACH_LOCALS(locals_it, cnt, valtype, func) {
        unsigned bit = type_bit(valtype);
        if (cnt && !(seen_types & bit)) {
            seen_types |= bit;
            out->insn_slots += const_slots(
                state, valtype == WDEC_TYPE_I64 || valtype == WDEC_TYPE_F64,
                ir_type_of(valtype), 0);
        }
    }
    return wdec_locals_iter_err(&locals_it);
}

/**
 * mark_target() - Mark the target frame of a branch.
 * @state: the pass state.
 * @label: the branch's label: how many frames up its target is.
 *
 * A branch to a block makes its join a merge (see measure_end()).
 *
 * Return: false if the branch leaves the function (it is a return).
 */
static bool mark_target(measure_state *state, uint32_t label)
{
    if (label >= state->depth) {
        return false;
    }
    state->frames[state->depth - 1 - label] |= FR_TARGETED;
    return true;
}

/**
 * measure_block() - Count a block, loop or if, and open its frame.
 * @state: the pass state.
 * @opcode: %WDEC_OP_BLOCK, %WDEC_OP_LOOP or %WDEC_OP_IF.
 *
 * A block opens its join block, a merge if some branch targets it (known
 * only at its end). A loop opens its header, entered by a
 * branch, and always a merge (entry + back edges). An if opens the then,
 * else and join blocks, entered by a conditional branch.
 *
 * Return: %WASMC_OK, or %WASMC_ERR_CTRL_STACK_OVERFLOW if the frames do not
 * fit (only for a module that did not pass validation).
 */
static wasmc_err_t measure_block(measure_state *state, wdec_opcode_t opcode)
{
    wmeasure_t *out = state->out;
    if (state->depth >= state->ctrl_cap) {
        return WASMC_ERR_CTRL_STACK_OVERFLOW;
    }
    switch (opcode) {
    case WDEC_OP_BLOCK:
        state->frames[state->depth++] = FR_BLOCK;
        out->blocks += 1; /* join */
        break;
    case WDEC_OP_LOOP:
        state->frames[state->depth++] = FR_LOOP;
        out->blocks += 1;     /* header */
        out->insn_slots += 1; /* br into the header */
        out->edges += 1;
        out->merges++; /* entry + back edges */
        break;
    default: /* if */
        state->frames[state->depth++] = FR_IF;
        out->blocks += 3;     /* then, else, join */
        out->insn_slots += 1; /* br_if */
        out->edges += 2;
        out->merges++; /* join: then + else */
        break;
    }
    return WASMC_OK;
}

/**
 * measure_else() - Count an else: the then arm branches to the join.
 * @state: the pass state.
 *
 * Return: %WASMC_OK, or %WASMC_ERR_UNBALANCED_ELSE if no frame is open.
 */
static wasmc_err_t measure_else(measure_state *state)
{
    if (state->depth == 0) {
        return WASMC_ERR_UNBALANCED_ELSE;
    }
    state->frames[state->depth - 1] |= FR_ELSE;
    state->out->insn_slots += 1; /* br to the join */
    state->out->edges += 1;
    return WASMC_OK;
}

/**
 * measure_end() - Count an end, and close its frame.
 * @state: the pass state.
 *
 * The end of the body is the implicit return. The end of an if branches its
 * arm to the join, both arms if it had no else (the else arm is empty). The
 * end of a block falls through to its join, a merge if a branch targets it.
 * The end of a loop adds nothing.
 */
static void measure_end(measure_state *state)
{
    wmeasure_t *out = state->out;
    if (state->depth == 0) { /* the function's own end: implicit return */
        out->insn_slots += 1;
        return;
    }
    uint8_t frame = state->frames[state->depth - 1];
    switch (frame & FR_KIND) {
    case FR_LOOP:
        break; /* a loop's end needs no merge block */
    case FR_IF:
        if (frame & FR_ELSE) {
            out->insn_slots += 1;
            out->edges += 1;
        } else { /* live arm + empty else arm */
            out->insn_slots += 2;
            out->edges += 2;
        }
        break;
    default: /* BLOCK */
        out->insn_slots += 1;
        out->edges += 1;
        if (frame & FR_TARGETED) {
            out->merges++;
        }
        break;
    }
    state->depth--;
}

/* br: one branch, one edge */
static void measure_br(measure_state *state, uint32_t label)
{
    state->out->insn_slots += 1;
    state->out->edges += 1;
    mark_target(state, label);
}

/* br_if: a conditional branch and the continuation block; a br_if that
 * returns branches to a trampoline block holding the return */
static void measure_br_if(measure_state *state, uint32_t label)
{
    wmeasure_t *out = state->out;
    if (mark_target(state, label)) {
        out->blocks += 1; /* continuation */
        out->insn_slots += 1;
    } else {
        out->blocks += 2;     /* return trampoline + continuation */
        out->insn_slots += 2; /* br_if + return */
    }
    out->edges += 2;
}

/**
 * measure_br_table() - Count a br_table.
 * @state: the pass state.
 * @tbl: its labels.
 *
 * One instruction and its packed label targets, an edge per label; one
 * trampoline block, shared, if some label returns.
 *
 * Return: %WASMC_OK or a decoder error from the label vector.
 */
static wasmc_err_t measure_br_table(measure_state *state,
                                    const wdec_br_table_t *tbl)
{
    wmeasure_t *out = state->out;
    uint32_t count = tbl->count + 1U;
    uint32_t lab;
    bool trampoline = tbl->default_label >= state->depth;
    wdec_br_table_label_iter_t label_it;
    WDEC_FOR_EACH_BR_TABLE_LABEL(label_it, lab, tbl) {
        if (!mark_target(state, lab)) {
            trampoline = true;
        }
    }
    wasmc_err_t err = wdec_br_table_label_iter_err(&label_it);
    if (err != WASMC_OK) {
        return err;
    }
    mark_target(state, tbl->default_label);
    out->insn_slots += 1 + pack_slots(count);
    out->edges += count;
    if (trampoline) {
        out->blocks += 1;
        out->insn_slots += 1;
    }
    return WASMC_OK;
}

/* call or call_indirect: one instruction and its packed arguments */
static wasmc_err_t measure_call(measure_state *state, const wdec_instr_t *ins)
{
    wdec_functype_t ftype;
    wasmc_err_t err = ins->opcode == WDEC_OP_CALL
                          ? wdec_func_type(state->mod, ins->imm.index, &ftype)
                          : wdec_get_type(state->mod, ins->imm.index, &ftype);
    if (err != WASMC_OK) {
        return err;
    }
    state->out->insn_slots += 1 + pack_slots(ftype.param_count);
    return WASMC_OK;
}

/**
 * measure_instr() - Count one instruction.
 * @state: the pass state.
 * @ins: the instruction.
 *
 * local.get/set/tee emit nothing (the SSA construction resolves them), but
 * the locals written are noted, for the phi bound; nop and drop emit
 * nothing.
 *
 * Return: %WASMC_OK or an error from the helpers.
 */
static wasmc_err_t measure_instr(measure_state *state, const wdec_instr_t *ins)
{
    wmeasure_t *out = state->out;
    switch (ins->opcode) {
    case WDEC_OP_NOP:
    case WDEC_OP_DROP:
    case WDEC_OP_LOCAL_GET:
        return WASMC_OK;
    case WDEC_OP_LOCAL_SET:
    case WDEC_OP_LOCAL_TEE:
        if (ins->imm.index < out->nlocals) {
            bitmap_set(state->written, ins->imm.index);
        }
        return WASMC_OK;
    case WDEC_OP_BLOCK:
    case WDEC_OP_LOOP:
    case WDEC_OP_IF:
        return measure_block(state, ins->opcode);
    case WDEC_OP_ELSE:
        return measure_else(state);
    case WDEC_OP_END:
        measure_end(state);
        return WASMC_OK;
    case WDEC_OP_BR:
        measure_br(state, ins->imm.index);
        return WASMC_OK;
    case WDEC_OP_BR_IF:
        measure_br_if(state, ins->imm.index);
        return WASMC_OK;
    case WDEC_OP_BR_TABLE:
        return measure_br_table(state, &ins->imm.br_table);
    case WDEC_OP_RETURN:
    case WDEC_OP_UNREACHABLE:
        out->insn_slots += 1;
        return WASMC_OK;
    case WDEC_OP_CALL:
    case WDEC_OP_CALL_INDIRECT:
        return measure_call(state, ins);
    case WDEC_OP_I32_CONST:
        out->insn_slots +=
            const_slots(state, 0, IR_T_I32, (uint32_t)ins->imm.i32);
        return WASMC_OK;
    case WDEC_OP_I64_CONST:
        out->insn_slots +=
            const_slots(state, 1, IR_T_I64, (uint64_t)ins->imm.i64);
        return WASMC_OK;
    case WDEC_OP_F32_CONST:
        out->insn_slots += const_slots(
            state, 0, IR_T_F32, read_le(ins->imm.f32_bytes, sizeof(uint32_t)));
        return WASMC_OK;
    case WDEC_OP_F64_CONST:
        out->insn_slots += const_slots(
            state, 1, IR_T_F64, read_le(ins->imm.f64_bytes, sizeof(uint64_t)));
        return WASMC_OK;
    default:
        /* every other operator emits one instruction, two for a load or
         * store whose offset does not fit in 16 bits */
        out->insn_slots +=
            (is_mem_op(ins->opcode) && ins->imm.memarg.offset > UINT16_MAX)
                ? 2U
                : 1U;
        return WASMC_OK;
    }
}

/* bits set in the first `len` bytes of `map` */
static uint32_t count_bits(const uint8_t *map, uint32_t len)
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < len; i++) {
        for (uint8_t byte = map[i]; byte; byte &= (uint8_t)(byte - 1)) {
            count++;
        }
    }
    return count;
}

/**
 * estimate_phis() - Set the first capacity of the phi region.
 * @out: the measurements, complete but for the phi field.
 *
 * The region grows while the IR is built (ir.h), so this is only a guess,
 * which an upper bound caps.
 */
static void estimate_phis(wmeasure_t *out)
{
    /* Only merges get phis: at most one per written local, plus one for
     * the block/if result. Each phi takes 1 slot + ceil(npreds/4) argument
     * slots, and sum(ceil(npreds/4)) <= merges + edges/4. */
    uint64_t bound = (uint64_t)(out->nwritten + 1U) *
                     (2U * (uint64_t)out->merges + pack_slots(out->edges));
    /* the builder creates no trivial phis at joins nor at loop headers of
     * locals the loop keeps: over 4269 real functions the phi region used
     * is a median 10% of the low region (p90 23%, max 51%); insn_slots/4 makes
     * it grow for 0.6% of them */
    enum { PHI_EST_SLACK = 16 };
    uint64_t est = ((uint64_t)out->insn_slots / 4U) + PHI_EST_SLACK;
    if (est > bound) {
        est = bound;
    }
    out->phi_slots = (uint32_t)(est < IR_MAX_SLOTS ? est : IR_MAX_SLOTS);
}

wasmc_err_t wmeasure_func(const wdec_module_t *mod, const wdec_func_t *func,
                          const wdec_functype_t *ftype, wdec_stack_depth caps,
                          uint8_t *tmp, uint32_t tmp_len, wmeasure_t *out)
{
    memset(out, 0, sizeof *out);
    uint64_t nlocals = (uint64_t)ftype->param_count + func->local_count;
    if (nlocals > UINT16_MAX) {
        return WASMC_ERR_TOO_MANY_LOCALS;
    }
    out->body_size = (uint32_t)(func->body_end - func->body);
    out->nlocals = (uint32_t)nlocals;

    /* tmp: [written bitmap][frame bytes][constant cache] */
    uint32_t bitmap_len = bitmap_bytes(out->nlocals);
    if ((uint64_t)bitmap_len + caps.ctrl + 2U + WMEASURE_CONST_TMP > tmp_len) {
        return WASMC_ERR_INVALID_ARG;
    }
    measure_state state = {.mod = mod,
                           .out = out,
                           .written = tmp,
                           .frames = tmp + bitmap_len,
                           .consts = tmp + bitmap_len + caps.ctrl + 2U,
                           .depth = 0,
                           .ctrl_cap = caps.ctrl};
    memset(state.written, 0, bitmap_len);
    memset(state.consts, 0, WMEASURE_CONST_TMP);

    wasmc_err_t err = measure_entry(&state, func, ftype);
    if (err != WASMC_OK) {
        return err;
    }
    wdec_instr_iter_t iter;
    wdec_instr_t ins;
    WDEC_FOR_EACH_INSTR(iter, ins, func) {
        wasmc_err_t ierr = measure_instr(&state, &ins);
        if (ierr != WASMC_OK) {
            return ierr;
        }
    }
    err = wdec_instr_iter_err(&iter);
    if (err != WASMC_OK) {
        return err;
    }
    out->nwritten = count_bits(state.written, bitmap_len);
    estimate_phis(out);
    return WASMC_OK;
}
