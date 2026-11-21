/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/* ra.c - see ra.h */
#include "ra.h"
#include "ir.h"
#include "wasmc/error.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define POS_NONE 0xFFFFFFFFu
#define SPILLED                                                                \
    0x80000000u /* in the end[] array: value spilled, low bits = offset */
#define MAX_ACTIVE      32u
#define MAX_SPILL_SLOTS 64u /* reusable slots tracked; more get fresh ones */

typedef struct {
    uint32_t head; /* position of the loop header */
    uint32_t end;  /* last position in the loop */
} ra_loop;

/* an interval holding registers */
typedef struct {
    ir_ref val;
    uint32_t start, end;
    uint32_t regs;
} ra_active;

/* a spill slot and the end of the interval of its last owner */
typedef struct {
    uint32_t end;
    uint32_t off;
    uint32_t bytes;
} ra_slot;

static int is_const(const ir_func *func, ir_ref val)
{
    unsigned opcode = ir_op(func->insn[val]);
    return opcode == IR_OP_CONST32 || opcode == IR_OP_CONST64;
}

static int is_wide(unsigned type)
{
    return type == IR_T_I64 || type == IR_T_F64;
}

/* words of a value's location: 2 for a 64-bit value, unless RA_NARROW */
static unsigned value_words(const ra_target *target, const ir_func *func,
                            ir_ref val)
{
    if (!is_wide(ir_ty(func->insn[val]))) {
        return 1U;
    }
    return target->flags && (target->flags[val] & RA_NARROW) ? 1U : 2U;
}

static void use_at(const ir_func *func, uint32_t *end, ir_ref val, uint32_t pos)
{
    if (val == IR_NIL || is_const(func, val)) {
        return;
    }
    if (end[val] == POS_NONE || end[val] < pos) {
        end[val] = pos;
    }
}

static uint32_t term_pos(const ir_func *func, ir_blk blk)
{
    return (2U * func->blocks[blk].term) + 1U;
}

/* the positions (start, end] of a value's live range */
typedef struct {
    uint32_t start, end;
} ra_span;

/* Is there one of the `n` sorted call positions in `span`? */
static int crosses(const uint32_t *calls, uint32_t n, ra_span span)
{
    uint32_t low = 0;
    uint32_t high = n;
    while (low < high) {
        uint32_t mid = (low + high) / 2U;
        if (calls[mid] <= span.start) {
            low = mid + 1U;
        } else {
            high = mid;
        }
    }
    return low < n && calls[low] <= span.end;
}

/* the state of one ra_run() */
typedef struct {
    ir_func *func;
    const ra_target *target;
    ra_result *out;
    uint32_t *end;      /* per slot: interval end, then spill offset */
    uint16_t *lidx;     /* per block: index in the layout, IR_NIL if none */
    uint32_t *loop_end; /* per loop header: last position in the loop */
    uint32_t *calls;    /* positions of the calls, sorted */
    uint32_t ncalls;
    ra_loop *loops; /* sorted by header */
    uint32_t nloops;
    uint32_t callee_mask;
    uint32_t caller_mask;
    uint32_t free_regs;
    ra_active act[MAX_ACTIVE];
    unsigned nact;
    ra_slot slots[MAX_SPILL_SLOTS];
    unsigned nslots;
} ra_state;

uint32_t ra_result_bytes(const ir_func *func)
{
    const unsigned nslots = ir_nslots(func);
    const ir_layout layouts[3] = {
        IR_ARRAY(uint8_t, nslots),
        IR_ARRAY(uint8_t, nslots),
        IR_ARRAY(uint32_t, nslots),
    };
    return ir_scratch_bytes(layouts, 3);
}

/* the result arrays, every interval still empty */
static wasmc_err_t alloc_result(ra_state *state)
{
    ir_func *func = state->func;
    ra_result *out = state->out;
    uint32_t nslots = ir_nslots(func);
    memset(out, 0, sizeof *out);
    out->reg_lo = ir_scratch_alloc(func, IR_ARRAY(uint8_t, nslots));
    out->reg_hi = ir_scratch_alloc(func, IR_ARRAY(uint8_t, nslots));
    out->slot_off = ir_scratch_alloc(func, IR_ARRAY(uint32_t, nslots));
    if (!out->reg_lo || !out->reg_hi || !out->slot_off) {
        return ir_scratch_err(func);
    }
    memset(out->reg_lo, 0, nslots);
    memset(out->reg_hi, 0, nslots);
    /* interval ends, until the scan turns them into spill offsets */
    state->end = out->slot_off;
    for (uint32_t i = 0; i < nslots; i++) {
        state->end[i] = POS_NONE;
    }
    return WASMC_OK;
}

/* the layout index of every block; positions are slot based: the layout
 * must be in slot order */
static wasmc_err_t index_layout(ra_state *state)
{
    ir_func *func = state->func;
    uint32_t nblocks = func->nblocks;
    state->lidx = ir_scratch_alloc(func, IR_ARRAY(uint16_t, nblocks + 1U));
    state->loop_end = ir_scratch_alloc(func, IR_ARRAY(uint32_t, nblocks + 1U));
    if (!state->lidx || !state->loop_end) {
        return ir_scratch_err(func);
    }
    for (uint32_t blk = 0; blk < nblocks; blk++) {
        state->lidx[blk] = IR_NIL;
        state->loop_end[blk] = POS_NONE;
    }
    for (unsigned k = 0; k < func->nlayout; k++) {
        state->lidx[func->layout[k]] = (uint16_t)k;
        if (k && func->blocks[func->layout[k]].first <=
                     func->blocks[func->layout[k - 1U]].first) {
            return WASMC_ERR_IR_STATE;
        }
    }
    return WASMC_OK;
}

/* the uses of the operands of instruction `ref`, call arguments included */
static void insn_uses(ra_state *state, ir_ref ref)
{
    ir_func *func = state->func;
    ir_insn insn = func->insn[ref];
    unsigned opcode = ir_op(insn);
    unsigned mask = ir_op_vmask(opcode);
    uint32_t pos = (2U * ref) + 1U;
    if (mask & IR_M_A) {
        use_at(func, state->end, ir_a(insn), pos);
    }
    if (mask & IR_M_B) {
        use_at(func, state->end, ir_b(insn), pos);
    }
    if (mask & IR_M_C) {
        use_at(func, state->end, ir_c(insn), pos);
    }
    if (opcode == IR_OP_CALL || opcode == IR_OP_CALL_INDIRECT) {
        for (unsigned nth = 0; nth < ir_a(insn); nth++) {
            use_at(func, state->end, ir_call_arg(func, ref, nth), pos);
        }
    }
}

/* the phi arguments of block `blk` are used at the end of each
 * predecessor */
static void phi_uses(ra_state *state, ir_blk blk)
{
    ir_func *func = state->func;
    for (ir_ref phi = func->blocks[blk].phi_head; phi != IR_NIL;
         phi = ir_a(func->insn[phi])) {
        unsigned idx = 0;
        uint16_t edge;
        IR_FOR_EACH_PRED(func, blk, edge)
        {
            ir_blk pred = func->edges[edge].from;
            if (func->blocks[pred].term != IR_NIL) {
                use_at(func, state->end,
                       ir_phi_arg(func, (ir_phi_slot){.phi = phi, .pred = idx}),
                       term_pos(func, pred));
            }
            idx++;
        }
    }
}

/* a back edge from the block at layout index `k` extends its loop */
static void back_edges(ra_state *state, unsigned layout_idx)
{
    ir_func *func = state->func;
    ir_blk blk = func->layout[layout_idx];
    for (unsigned succ = 0, count = ir_succ_count(func, blk); succ < count;
         succ++) {
        ir_blk header = ir_succ(func, blk, succ);
        if (state->lidx[header] == IR_NIL || state->lidx[header] > layout_idx) {
            continue; /* forward edge */
        }
        if (state->loop_end[header] == POS_NONE ||
            state->loop_end[header] < term_pos(func, blk)) {
            state->loop_end[header] = term_pos(func, blk);
        }
    }
}

/* uses, calls and back edges */
static void collect_uses(ra_state *state)
{
    ir_func *func = state->func;
    state->ncalls = 0;
    for (unsigned k = 0; k < func->nlayout; k++) {
        ir_blk blk = func->layout[k];
        const ir_block *block = &func->blocks[blk];
        for (ir_ref i = block->first; i < block->end;
             i += (ir_ref)ir_insn_len(func->insn[i])) {
            if (state->target->is_call(state->target->ctx, i)) {
                state->ncalls++;
            }
            insn_uses(state, i);
        }
        phi_uses(state, blk);
        back_edges(state, k);
    }
}

/* the sorted positions of the calls and the loops */
static wasmc_err_t collect_calls_and_loops(ra_state *state)
{
    ir_func *func = state->func;
    state->nloops = 0;
    for (unsigned k = 0; k < func->nlayout; k++) {
        if (state->loop_end[func->layout[k]] != POS_NONE) {
            state->nloops++;
        }
    }
    state->calls =
        ir_scratch_alloc(func, IR_ARRAY(uint32_t, state->ncalls + 1U));
    state->loops =
        ir_scratch_alloc(func, IR_ARRAY(ra_loop, state->nloops + 1U));
    if (!state->calls || !state->loops) {
        return ir_scratch_err(func);
    }
    state->ncalls = state->nloops = 0;
    for (unsigned k = 0; k < func->nlayout; k++) {
        ir_blk blk = func->layout[k];
        const ir_block *block = &func->blocks[blk];
        if (state->loop_end[blk] != POS_NONE) {
            state->loops[state->nloops++] =
                (ra_loop){2U * block->first, state->loop_end[blk]};
        }
        for (ir_ref i = block->first; i < block->end;
             i += (ir_ref)ir_insn_len(func->insn[i])) {
            if (state->target->is_call(state->target->ctx, i)) {
                state->calls[state->ncalls++] = (2U * i) + 1U;
            }
        }
    }
    return WASMC_OK;
}

static void register_masks(ra_state *state)
{
    const ra_target *target = state->target;
    state->callee_mask = 0;
    state->caller_mask = 0;
    for (unsigned reg = 0; reg < target->ncallee; reg++) {
        state->callee_mask |= 1U << target->callee[reg];
    }
    for (unsigned reg = 0; reg < target->ncaller; reg++) {
        state->caller_mask |= 1U << target->caller[reg];
    }
    state->free_regs = state->callee_mask | state->caller_mask;
    state->nact = 0;
}

/* A value live into a later loop is live up to its end (loops sorted by
 * header). */
static uint32_t extend_through_loops(const ra_state *state, ra_span live)
{
    uint32_t low = 0;
    uint32_t high = state->nloops;
    while (low < high) {
        uint32_t mid = (low + high) / 2U;
        if (state->loops[mid].head <= live.start) {
            low = mid + 1U;
        } else {
            high = mid;
        }
    }
    uint32_t last_use = live.end;
    for (uint32_t j = low;
         j < state->nloops && state->loops[j].head <= last_use; j++) {
        if (state->loops[j].end > last_use) {
            last_use = state->loops[j].end;
        }
    }
    return last_use;
}

/* frees the registers of the intervals that end before `pos` */
static void expire(ra_state *state, uint32_t pos)
{
    for (unsigned idx = 0; idx < state->nact;) {
        if (state->act[idx].end < pos) {
            state->free_regs |= state->act[idx].regs;
            state->act[idx] = state->act[--state->nact];
        } else {
            idx++;
        }
    }
}

static unsigned count_regs(uint32_t regs)
{
    return (unsigned)__builtin_popcount(regs);
}

/* what a value needs: its registers, those it may use, its live range */
typedef struct {
    ir_ref val;
    unsigned need; /* 1 or 2 registers */
    uint32_t elig; /* registers it may use */
    bool cross;    /* live across a call: callee-saved registers only */
    ra_span live;
} ra_request;

/* A spill slot of `bytes` for the interval `live`: the first tracked slot
 * whose last owner's interval ended before `live` starts, else a new one.
 * The owners of a slot follow each other in position order, so the last one
 * ends last. Slots are reused as registers are (a value's slot is free after
 * its last use), so the frame holds the values spilled at the same time,
 * not all of them: offsets stay small enough for one lw/sw. */
static uint32_t spill_slot(ra_state *state, uint32_t bytes, ra_span live)
{
    for (unsigned idx = 0; idx < state->nslots; idx++) {
        ra_slot *slot = &state->slots[idx];
        if (slot->end < live.start && slot->bytes >= bytes) {
            slot->end = live.end;
            return slot->off;
        }
    }
    uint32_t off = state->out->spill_bytes;
    state->out->spill_bytes += bytes;
    if (state->nslots < MAX_SPILL_SLOTS) {
        state->slots[state->nslots++] = (ra_slot){live.end, off, bytes};
    }
    return off;
}

/* Not enough free registers: spills the interval that ends last, this one
 * or an active one whose registers would be enough. True if it was this
 * one. A value is spilled for its whole interval (spill everywhere): an
 * active one from its start, not from here. */
static bool spill(ra_state *state, const ra_request *req)
{
    int best = -1;
    for (unsigned idx = 0; idx < state->nact; idx++) {
        const ra_active *act = &state->act[idx];
        if ((act->regs & ~req->elig) ||
            count_regs((state->free_regs | act->regs) & req->elig) <
                req->need) {
            continue;
        }
        if (best < 0 || act->end > state->act[best].end) {
            best = (int)idx;
        }
    }
    ir_ref victim = req->val;
    ra_span live = req->live;
    if (best >= 0 && state->act[best].end > req->live.end) {
        victim = state->act[best].val;
        live = (ra_span){state->act[best].start, state->act[best].end};
        state->free_regs |= state->act[best].regs;
        state->out->reg_lo[victim] = state->out->reg_hi[victim] = 0;
        state->act[best] = state->act[--state->nact];
    }
    uint32_t bytes = value_words(state->target, state->func, victim) * 4U;
    state->end[victim] = SPILLED | spill_slot(state, bytes, live);
    return victim == req->val;
}

/* A phi takes the free registers of one of its arguments if it can: its
 * copy on that edge is then a no-op. Returns the registers taken. */
static unsigned phi_hint(const ra_state *state, const ir_block *blk,
                         const ra_request *req, uint8_t *got)
{
    const ra_result *out = state->out;
    unsigned count = 0;
    for (unsigned pred = 0; pred < blk->npreds && !count; pred++) {
        ir_ref arg = ir_phi_arg(state->func,
                                (ir_phi_slot){.phi = req->val, .pred = pred});
        if (arg == IR_NIL || arg == req->val || !out->reg_lo[arg]) {
            continue;
        }
        uint32_t want = (1U << out->reg_lo[arg]) |
                        (req->need == 2U ? 1U << out->reg_hi[arg] : 0U);
        if ((want & state->free_regs & req->elig) != want) {
            continue;
        }
        got[count++] = out->reg_lo[arg];
        if (req->need == 2U) {
            got[count++] = out->reg_hi[arg];
        }
    }
    return count;
}

/* the first free registers of `regs`, after the `count` already taken */
static unsigned take_free(const ra_state *state, const uint8_t *regs,
                          unsigned nregs, uint8_t *got, unsigned count,
                          unsigned need)
{
    for (unsigned reg = 0; reg < nregs && count < need; reg++) {
        if (state->free_regs & (1U << regs[reg])) {
            got[count++] = regs[reg];
        }
    }
    return count;
}

/* Gives the value registers: a phi argument's if possible, then the
 * caller-saved ones if it does not cross a call, then the callee-saved. */
static wasmc_err_t assign_registers(ra_state *state, const ir_block *blk,
                                    const ra_request *req)
{
    const ra_target *target = state->target;
    uint8_t got[2] = {0, 0};
    unsigned count = 0;
    if (ir_in_phi_region(state->func, req->val)) {
        count = phi_hint(state, blk, req, got);
    }
    if (!req->cross) {
        count = take_free(state, target->caller, target->ncaller, got, count,
                          req->need);
    }
    take_free(state, target->callee, target->ncallee, got, count, req->need);
    uint32_t regs = (1U << got[0]) | (req->need == 2U ? 1U << got[1] : 0U);
    state->out->reg_lo[req->val] = got[0];
    state->out->reg_hi[req->val] = req->need == 2U ? got[1] : 0;
    state->free_regs &= ~regs;
    state->out->used_regs |= regs;
    if (state->nact >= MAX_ACTIVE) {
        return WASMC_ERR_IR_STATE;
    }
    state->act[state->nact++] =
        (ra_active){req->val, req->live.start, req->live.end, regs};
    return WASMC_OK;
}

/* Allocates the value `val` defined at `def_pos` in block `blk`. */
static wasmc_err_t allocate(ra_state *state, const ir_block *blk, ir_ref val,
                            uint32_t def_pos)
{
    const ir_func *func = state->func;
    if (state->end[val] == POS_NONE) {
        return WASMC_OK; /* never used: no location */
    }
    if (state->target->flags && (state->target->flags[val] & RA_FOLDED)) {
        state->end[val] = POS_NONE;
        return WASMC_OK;
    }
    ra_request req;
    req.val = val;
    req.live = (ra_span){def_pos, state->end[val]};
    req.live.end = extend_through_loops(state, req.live);
    req.cross = ir_op(func->insn[val]) == IR_OP_PARAM ||
                crosses(state->calls, state->ncalls, req.live);
    req.need = value_words(state->target, func, val);
    req.elig = req.cross ? state->callee_mask
                         : state->callee_mask | state->caller_mask;

    expire(state, def_pos); /* free after the last use */
    if (count_regs(state->free_regs & req.elig) < req.need &&
        spill(state, &req)) {
        return WASMC_OK;
    }
    return assign_registers(state, blk, &req);
}

/* linear scan of a block in position order: the phis first, all at the
 * block's start, then the values its instructions define */
static wasmc_err_t scan_block(ra_state *state, const ir_block *blk)
{
    const ir_func *func = state->func;
    for (ir_ref phi = blk->phi_head; phi != IR_NIL;
         phi = ir_a(func->insn[phi])) {
        wasmc_err_t err = allocate(state, blk, phi, 2U * blk->first);
        if (err) {
            return err;
        }
    }
    for (ir_ref ref = blk->first; ref < blk->end;
         ref += (ir_ref)ir_insn_len(func->insn[ref])) {
        if (!ir_defines_value(func->insn[ref]) || is_const(func, ref)) {
            continue;
        }
        wasmc_err_t err = allocate(state, blk, ref, (2U * ref) + 1U);
        if (err) {
            return err;
        }
    }
    return WASMC_OK;
}

/* the interval ends become the spill offsets of the spilled values */
static void finish_spill_offsets(ra_state *state)
{
    for (uint32_t val = 0; val < ir_nslots(state->func); val++) {
        uint32_t loc = state->end[val];
        state->end[val] =
            (!state->out->reg_lo[val] && loc != POS_NONE && (loc & SPILLED))
                ? loc & ~SPILLED
                : RA_SLOT_NONE;
    }
}

wasmc_err_t ra_run(ir_func *func, const ra_target *target, ra_result *out)
{
    ra_state state;
    memset(&state, 0, sizeof state);
    state.func = func;
    state.target = target;
    state.out = out;

    wasmc_err_t err = alloc_result(&state);
    if (err) {
        return err;
    }
    uint32_t mark = ir_scratch_mark(func);
    err = index_layout(&state);
    if (!err) {
        collect_uses(&state);
        err = collect_calls_and_loops(&state);
    }
    if (err) {
        return err;
    }
    register_masks(&state);
    for (unsigned k = 0; k < func->nlayout; k++) {
        err = scan_block(&state, &func->blocks[func->layout[k]]);
        if (err) {
            return err;
        }
    }
    finish_spill_offsets(&state);
    ir_scratch_release(func, mark);
    return WASMC_OK;
}
