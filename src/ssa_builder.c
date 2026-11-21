/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * ssa_builder.c - On-the-fly SSA construction (interface: ssa_builder.h).
 *
 * Braun et al. (CC 2013), with these differences from the paper's
 * pseudocode:
 *
 *   - Lazy phis at sealed joins (the paper's marker variant): a join first
 *     collects its predecessors' values while marked `visiting`; it gets a
 *     phi only if they differ, or if the walk comes back to it (a cycle,
 *     through a loop), in which case a placeholder is made at that moment.
 *
 *   - Iterative: a read walks the CFG on an explicit stack, bounded by the
 *     number of blocks, instead of recursing on the native stack.
 *
 *   - No incomplete-phi table: a loop header's phis are all incomplete
 *     until it is sealed, and they are on its phi chain; each phi's
 *     variable is found again in the header's definition list.
 *
 *   - Loop write sets: a read of a variable the loop's body never writes
 *     goes through the header to its entry edge, without a phi.
 *
 *   - Memoization only where useful: at joins and new phis (needed for
 *     cycles) and, on request, in the block read; not in the single-
 *     predecessor blocks a walk passes through.
 *
 *   - Trivial phis become forwards (ir_phi_try_trivial()); the cascade the
 *     paper does at once is done by ir_finalize().
 */
#include "ssa_builder.h"
#include "bitmap.h"
#include "ir.h"
#include "wasmc/error.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/**
 * define SSA_WSETS_MAX - Budget of the loops' write sets, in bytes.
 *
 * One bitset of the written variables per loop (loops x nvars / 8 bytes).
 * Above the budget there are none and the shortcut is off: reads through
 * loop headers make phis, as in plain Braun et al.
 */
#define SSA_WSETS_MAX 16384u

/**
 * struct ssa_region - A closed region (ssa_region_close()).
 * @head: the block it was entered from.
 * @begin: its begin event.
 * @end: its end event: a write at an event in [@begin, @end] is inside.
 */
struct ssa_region {
    ir_blk head;
    uint32_t begin;
    uint32_t end;
};

/**
 * struct ssa_def - One definition of a variable in a block (Braun's
 * currentDef).
 * @next: the block's previous definition, or NULL.
 * @var: index of the variable.
 * @value: its value from this point of the block on.
 *
 * Each block has a list of these, newest first, so a lookup finds the most
 * recent definition. They come from ssa_write() and from the values reads
 * memoize.
 */
typedef struct ssa_def {
    struct ssa_def *next;
    uint16_t var;
    ir_ref value;
} ssa_def;

/**
 * struct ssa_rl_frame - One pending step of a read walk.
 * @blk: the block whose value of the variable is wanted.
 * @join: true if @blk collects the values of all its predecessors; false
 *     if its value is that of one predecessor (its only one, or the entry
 *     edge of a loop that does not write the variable).
 * @pred: join: index of the predecessor being read.
 * @arg: join: its predecessors' values start at &ssa_builder.rl_args[@arg].
 *
 * ssa_read() uses an explicit stack of these instead of recursion, so its
 * depth is bounded by the number of blocks, not by the native stack.
 */
typedef struct ssa_rl_frame {
    ir_blk blk;
    bool join;
    uint16_t pred;
    uint32_t arg;
} ssa_rl_frame;

/**
 * struct rl_walk - The state of one ssa_read() walk.
 * @where: the read: the variable and the block it is read in.
 * @memo: memoize the result in @where.blk (for later reads there).
 * @type: IR type of the variable.
 * @top: frames on &ssa_builder.rl.
 * @nargs: values collected in &ssa_builder.rl_args.
 * @result: the value of the frame just popped, when @have.
 * @have: @result is to be delivered to the frame on top.
 * @failed: the IR ran out of capacity; the read gives IR_NIL.
 */
typedef struct rl_walk {
    ssa_site where;
    bool memo;
    unsigned type;
    uint32_t top;
    uint32_t nargs;
    ir_ref result;
    bool have;
    bool failed;
} rl_walk;

/* ------------------------------------------------------------------------ */
/* Allocation and layout                                                    */
/* ------------------------------------------------------------------------ */

/**
 * ssa_fail() - Record that the IR failed (out of capacity, misuse).
 * @ssa: the builder.
 *
 * Sets *&ssa_builder.err to the IR's own error, or %WASMC_ERR_IR_STATE if
 * the IR has none (an inconsistency of the builder).
 */
static void ssa_fail(ssa_builder *ssa)
{
    *ssa->err = ssa->ir->err ? (wasmc_err_t)ssa->ir->err : WASMC_ERR_IR_STATE;
}

/**
 * ssa_alloc() - Allocate from the scratch region, for the whole function.
 * @ssa: the builder.
 * @layout: size and alignment.
 *
 * Return: the memory, or NULL (out of scratch; the error is set).
 */
static void *ssa_alloc(ssa_builder *ssa, ir_layout layout)
{
    void *ptr = ir_scratch_alloc(ssa->ir, layout);
    if (!ptr) {
        ssa_fail(ssa);
    }
    return ptr;
}

void ssa_init(ssa_builder *ssa, ir_func *func, wasmc_err_t *err,
              const uint8_t *var_type)
{
    memset(ssa, 0, sizeof *ssa);
    ssa->ir = func;
    ssa->err = err;
    ssa->var_type = var_type;
}

/* the arrays of ssa_alloc_arrays(), in order */
enum {
    SSA_ARR_DEF_HEAD,
    SSA_ARR_UNSEALED,
    SSA_ARR_VISITING,
    SSA_ARR_RL,
    SSA_ARR_RL_ARGS,
    SSA_ARR_LOOP_WSET,
    SSA_ARR_COUNT
};
_Static_assert(SSA_ARR_COUNT == SSA_NARRAYS, "SSA_NARRAYS is out of date");

unsigned ssa_array_layouts(const ssa_sizes *sizes,
                           ir_layout layouts[SSA_NARRAYS])
{
    /* a walk has each block on its stack at most once (a cycle stops at a
     * join being visited), and collects one value per edge at most */
    layouts[SSA_ARR_DEF_HEAD] = IR_ARRAY(ssa_def *, sizes->blocks);
    layouts[SSA_ARR_UNSEALED] = IR_ARRAY(uint8_t, sizes->blocks);
    layouts[SSA_ARR_VISITING] = IR_ARRAY(uint8_t, sizes->blocks);
    layouts[SSA_ARR_RL] = IR_ARRAY(ssa_rl_frame, sizes->blocks + 1U);
    layouts[SSA_ARR_RL_ARGS] = IR_ARRAY(ir_ref, sizes->edges + 1U);
    layouts[SSA_ARR_LOOP_WSET] = IR_ARRAY(uint16_t, sizes->blocks);
    return SSA_NARRAYS;
}

void ssa_alloc_arrays(ssa_builder *ssa, const ssa_sizes *sizes)
{
    ir_layout layouts[SSA_NARRAYS];
    ssa_array_layouts(sizes, layouts);
    ssa->nvars = sizes->nvars;
    ssa->rl_cap = sizes->blocks + 1U;
    ssa->rl_args_cap = sizes->edges + 1U;
    ssa->wset_bytes = bitmap_bytes(sizes->nvars) + 1U;

    ssa->def_head = (ssa_def **)ssa_alloc(ssa, layouts[SSA_ARR_DEF_HEAD]);
    ssa->unsealed = ssa_alloc(ssa, layouts[SSA_ARR_UNSEALED]);
    ssa->visiting = ssa_alloc(ssa, layouts[SSA_ARR_VISITING]);
    ssa->rl = ssa_alloc(ssa, layouts[SSA_ARR_RL]);
    ssa->rl_args = ssa_alloc(ssa, layouts[SSA_ARR_RL_ARGS]);
    ssa->loop_wset = ssa_alloc(ssa, layouts[SSA_ARR_LOOP_WSET]);

    if (ir_failed(ssa->ir)) {
        return;
    }
    memset((void *)ssa->def_head, 0, (size_t)sizes->blocks * sizeof(ssa_def *));
    memset(ssa->unsealed, 0, sizes->blocks);
    memset(ssa->visiting, 0, sizes->blocks);
    for (uint32_t blk = 0; blk < sizes->blocks; blk++) {
        ssa->loop_wset[blk] = IR_NIL;
    }
}

ir_layout ssa_loops_layout(const ssa_builder *ssa, uint32_t loops)
{
    const uint64_t wset_total = (uint64_t)loops * ssa->wset_bytes;
    if (!loops || wset_total > SSA_WSETS_MAX) {
        return (ir_layout){.size = 0, .align = 1};
    }
    return IR_ARRAY(uint8_t, (size_t)wset_total);
}

void ssa_alloc_loops(ssa_builder *ssa, uint32_t loops)
{
    const ir_layout layout = ssa_loops_layout(ssa, loops);
    ssa->wsets_cap = loops;
    if (layout.size) {
        ssa->wsets = ssa_alloc(ssa, layout);
    }
}

/* ------------------------------------------------------------------------ */
/* Regions                                                                  */
/* ------------------------------------------------------------------------ */

uint32_t *ssa_region_counts(ssa_builder *ssa)
{
    ssa->wstart = ssa_alloc(ssa, IR_ARRAY(uint32_t, ssa->nvars + 1U));
    if (ssa->wstart) {
        memset(ssa->wstart, 0, ((size_t)ssa->nvars + 1U) * sizeof(uint32_t));
    }
    return ssa->wstart;
}

void ssa_alloc_regions(ssa_builder *ssa, uint32_t regions)
{
    if (!ssa->wstart) {
        return;
    }
    /* the counts become where each variable's writes start */
    uint64_t total = 0;
    for (uint32_t var = 0; var < ssa->nvars; var++) {
        const uint32_t count = ssa->wstart[var];
        ssa->wstart[var] = (uint32_t)total;
        total += count;
    }
    if (total >= UINT32_MAX) {
        return; /* (cannot happen: a write takes a byte of the body) */
    }
    ssa->wstart[ssa->nvars] = (uint32_t)total;
    ssa->wfill = ssa_alloc(ssa, IR_ARRAY(uint32_t, ssa->nvars + 1U));
    ssa->wpos = ssa_alloc(ssa, IR_ARRAY(uint32_t, total + 1U));
    struct ssa_region *table =
        ssa_alloc(ssa, IR_ARRAY(struct ssa_region, regions + 1U));
    if (!ssa->wfill || !ssa->wpos || !table) {
        return;
    }
    memcpy(ssa->wfill, ssa->wstart, (size_t)ssa->nvars * sizeof(uint32_t));
    ssa->regions = table;
    ssa->regions_cap = regions;
}

bool ssa_regions_on(const ssa_builder *ssa)
{
    return ssa->regions != NULL;
}

uint32_t ssa_region_open(ssa_builder *ssa)
{
    return ++ssa->event;
}

void ssa_region_close(ssa_builder *ssa, ir_blk join, ir_blk head,
                      uint32_t begin, bool record)
{
    /* region indices follow the loops' write sets in &ssa_builder.loop_wset */
    if (record && ssa->regions && ssa->nregions < ssa->regions_cap &&
        join != IR_NIL && head != IR_NIL &&
        (uint64_t)ssa->wsets_cap + ssa->nregions < IR_NIL) {
        ssa->regions[ssa->nregions] = (struct ssa_region){
            .head = head,
            .begin = begin,
            .end = ssa->event,
        };
        ssa->loop_wset[join] = (uint16_t)(ssa->wsets_cap + ssa->nregions++);
    }
    ssa->event++;
}

/* cppcheck-suppress constParameterPointer ; the caller writes the refs */
ir_ref *ssa_temp_refs(ssa_builder *ssa, uint32_t *cap)
{
    *cap = ssa->rl_args_cap;
    return ssa->rl_args;
}

/* ------------------------------------------------------------------------ */
/* Definitions                                                              */
/* ------------------------------------------------------------------------ */

/**
 * push_def() - Add a definition of a variable to a block.
 * @ssa: the builder.
 * @site: the variable and the block.
 * @value: its value from this point of the block on.
 *
 * For the reads' memos: they only define a variable a block has no
 * definition of yet.
 */
static void push_def(ssa_builder *ssa, ssa_site site, ir_ref value)
{
    ssa_def *def = ssa_alloc(ssa, IR_ARRAY(ssa_def, 1));
    if (!def) {
        return;
    }
    def->var = site.var;
    def->value = value;
    def->next = ssa->def_head[site.blk];
    ssa->def_head[site.blk] = def;
}

/*
 * A block's newest definition of the variable is overwritten rather than
 * shadowed by a new node: only the newest is ever read. But not the memo
 * of an incomplete phi of an open loop header, through which
 * ssa_seal_loop() finds the phi's variable (phi_var()).
 */
void ssa_write(ssa_builder *ssa, ssa_site site, ir_ref value)
{
    /* the write's event, for region_keeps() (the counts bound them) */
    if (ssa->regions && ssa->wfill[site.var] < ssa->wstart[site.var + 1U]) {
        ssa->wpos[ssa->wfill[site.var]++] = ssa->event;
    }
    for (ssa_def *def = ssa->def_head[site.blk]; def; def = def->next) {
        if (def->var == site.var) {
            if (ssa->unsealed[site.blk] && def->value != IR_NIL &&
                ir_op(ir_get(ssa->ir, def->value)) == IR_OP_PHI) {
                break;
            }
            def->value = value;
            return;
        }
    }
    push_def(ssa, site, value);
}

/**
 * update_def() - Replace the newest definition of a variable in a block.
 * @ssa: the builder.
 * @site: the variable and the block.
 * @value: the value that replaces it.
 *
 * Used when a phi memoized as a placeholder turns out trivial: its entry
 * is overwritten instead of getting a second one.
 */
static void update_def(ssa_builder *ssa, ssa_site site, ir_ref value)
{
    for (ssa_def *def = ssa->def_head[site.blk]; def; def = def->next) {
        if (def->var == site.var) {
            def->value = value;
            return;
        }
    }
    push_def(ssa, site, value);
}

/**
 * find_def() - The newest definition of a variable in a block.
 * @ssa: the builder.
 * @site: the variable and the block.
 *
 * Return: its value, or IR_NIL if the block does not define it.
 */
static ir_ref find_def(const ssa_builder *ssa, ssa_site site)
{
    for (const ssa_def *def = ssa->def_head[site.blk]; def; def = def->next) {
        if (def->var == site.var) {
            return def->value;
        }
    }
    return IR_NIL;
}

/**
 * region_keeps() - Whether a block is the join of a region that never
 * writes a variable.
 * @ssa: the builder.
 * @site: the variable and the block.
 *
 * The value of such a variable at the join is the one at the region's
 * entry: the walk goes on from the region's head. The writes of the
 * variable are in event order: the first one at or after the region's
 * begin event tells.
 *
 * Return: the region, or NULL if @site.blk is not such a join.
 */
static const struct ssa_region *region_keeps(const ssa_builder *ssa,
                                             ssa_site site)
{
    const uint16_t idx = ssa->loop_wset[site.blk];
    if (!ssa->regions || idx == IR_NIL || idx < ssa->wsets_cap) {
        return NULL;
    }
    const struct ssa_region *region = &ssa->regions[idx - ssa->wsets_cap];
    uint32_t low = ssa->wstart[site.var];
    uint32_t high = ssa->wfill[site.var];
    while (low < high) {
        const uint32_t mid = low + ((high - low) / 2U);
        if (ssa->wpos[mid] < region->begin) {
            low = mid + 1U;
        } else {
            high = mid;
        }
    }
    const bool writes =
        low < ssa->wfill[site.var] && ssa->wpos[low] <= region->end;
    return writes ? NULL : region;
}

/**
 * loop_keeps() - Whether a block is a loop header whose body never writes
 * a variable.
 * @ssa: the builder.
 * @site: the variable and the block.
 *
 * The value of such a variable in the header is the one on the loop's
 * entry edge, whatever the back edges: no phi is needed.
 *
 * Return: true if @site.blk has a write set without @site.var.
 */
static bool loop_keeps(const ssa_builder *ssa, ssa_site site)
{
    if (!ssa->wsets || ssa->loop_wset[site.blk] == IR_NIL ||
        ssa->loop_wset[site.blk] >= ssa->wsets_cap) {
        return false;
    }
    const uint8_t *wset =
        ssa->wsets + ((size_t)ssa->loop_wset[site.blk] * ssa->wset_bytes);
    return !bitmap_test(wset, site.var);
}

/* ------------------------------------------------------------------------ */
/* Reads: the walk                                                          */
/* ------------------------------------------------------------------------ */

/**
 * rl_push() - Push a frame that reads the variable in a block.
 * @ssa: the builder.
 * @walk: the walk.
 * @blk: the block.
 */
static void rl_push(ssa_builder *ssa, rl_walk *walk, ir_blk blk)
{
    if (walk->top >= ssa->rl_cap) {
        ssa_fail(ssa);
        walk->failed = true;
        return;
    }
    ssa->rl[walk->top++] = (ssa_rl_frame){.blk = blk};
}

/**
 * rl_pop() - Pop the frame on top, whose block has the variable at a value.
 * @walk: the walk.
 * @value: the variable's value in that block.
 */
static void rl_pop(rl_walk *walk, ir_ref value)
{
    walk->result = value;
    walk->have = true;
    walk->top--;
}

/**
 * rl_new_phi() - Give the frame on top a new phi of the variable, and pop
 * it.
 * @ssa: the builder.
 * @walk: the walk.
 * @site: the variable and the frame's block.
 *
 * The phi is memoized in the block at once, which breaks cycles. In an
 * open loop header it is incomplete: ssa_seal_loop() finds it in the
 * header's phi chain, and its variable through this memo.
 */
static void rl_new_phi(ssa_builder *ssa, rl_walk *walk, ssa_site site)
{
    ir_ref phi = ir_phi_new(ssa->ir, site.blk, walk->type);
    if (phi == IR_NIL) {
        walk->failed = true;
        return;
    }
    push_def(ssa, site, phi);
    rl_pop(walk, phi);
}

/**
 * same_value() - The value a list of values all resolve to.
 * @func: the IR.
 * @vals: the values.
 * @count: how many, at least 1.
 *
 * Return: the common value, or IR_NIL if they differ.
 */
static ir_ref same_value(ir_func *func, const ir_ref *vals, unsigned count)
{
    ir_ref same = ir_resolve(func, vals[0]);
    for (unsigned i = 1; i < count && same != IR_NIL; i++) {
        if (ir_resolve(func, vals[i]) != same) {
            same = IR_NIL;
        }
    }
    return same;
}

/**
 * rl_finish_join() - Resolve a join once all its predecessors are read.
 * @ssa: the builder.
 * @walk: the walk.
 * @frame: the join's frame, on top.
 *
 * No phi is needed if the values are all the same. Otherwise the join
 * gets a phi: the placeholder a cycle made through it, if there is one,
 * or a new one. Either way the result is memoized in the join and the
 * frame is popped.
 */
static void rl_finish_join(ssa_builder *ssa, rl_walk *walk,
                           const ssa_rl_frame *frame)
{
    const ssa_site site = {.var = walk->where.var, .blk = frame->blk};
    const unsigned count = ssa->ir->blocks[site.blk].npreds;
    const ir_ref *args = &ssa->rl_args[frame->arg];
    ssa->visiting[site.blk] = 0;
    walk->nargs = frame->arg;

    ir_ref phi = find_def(ssa, site); /* the placeholder of a cycle */
    if (phi == IR_NIL) {
        ir_ref same = same_value(ssa->ir, args, count);
        if (same != IR_NIL) {
            push_def(ssa, site, same);
            rl_pop(walk, same);
            return;
        }
        phi = ir_phi_new(ssa->ir, site.blk, walk->type);
        if (phi == IR_NIL) {
            walk->failed = true;
            return;
        }
        push_def(ssa, site, phi);
    }
    ir_phi_alloc_args(ssa->ir, (ir_block_phi){.blk = site.blk, .phi = phi});
    if (ir_failed(ssa->ir)) {
        walk->failed = true;
        return;
    }
    for (unsigned i = 0; i < count; i++) {
        ir_phi_set_arg(ssa->ir, (ir_phi_slot){.phi = phi, .pred = i}, args[i]);
    }
    ir_ref trivial = ir_phi_try_trivial(ssa->ir, phi);
    if (trivial != IR_NIL) {
        update_def(ssa, site, trivial);
    }
    rl_pop(walk, trivial != IR_NIL ? trivial : phi);
}

/**
 * rl_deliver() - Hand the value of the frame just popped to the one below.
 * @ssa: the builder.
 * @walk: the walk; @walk->result is the value.
 *
 * A single-predecessor frame takes the value as its own and is popped. A
 * join stores it and reads its next predecessor, or is resolved after the
 * last one.
 */
static void rl_deliver(ssa_builder *ssa, rl_walk *walk)
{
    ssa_rl_frame *frame = &ssa->rl[walk->top - 1];
    if (!frame->join) {
        /* memoized only where it was asked for (later reads in the same
         * block): the blocks in between are a pure cache, and caching there
         * costs a node per (variable, block) */
        if (frame->blk == walk->where.blk && walk->memo) {
            push_def(ssa, walk->where, walk->result);
        }
        walk->top--;
        return;
    }
    ssa->rl_args[frame->arg + frame->pred] = walk->result;
    if (++frame->pred < ssa->ir->blocks[frame->blk].npreds) {
        walk->have = false;
        rl_push(ssa, walk,
                ir_pred(ssa->ir,
                        (ir_pred_pos){.blk = frame->blk, .idx = frame->pred}));
        return;
    }
    rl_finish_join(ssa, walk, frame);
}

/**
 * rl_start_join() - Make the frame on top a join and read its first
 * predecessor.
 * @ssa: the builder.
 * @walk: the walk.
 * @frame: the frame on top.
 * @count: its block's number of predecessors, at least 2.
 */
static void rl_start_join(ssa_builder *ssa, rl_walk *walk, ssa_rl_frame *frame,
                          unsigned count)
{
    if (walk->nargs + count > ssa->rl_args_cap) {
        ssa_fail(ssa);
        walk->failed = true;
        return;
    }
    frame->join = true;
    frame->pred = 0;
    frame->arg = walk->nargs;
    walk->nargs += count;
    ssa->visiting[frame->blk] = 1;
    rl_push(ssa, walk, ir_first_pred(ssa->ir, frame->blk));
}

/**
 * rl_visit() - Look at the block of the frame just pushed.
 * @ssa: the builder.
 * @walk: the walk.
 *
 * Its value of the variable is, in order: its own definition; for the join
 * of a region that never writes the variable, the value at the region's
 * head; for a loop header that never writes the variable, the value on the
 * entry edge (the oldest, last of the predecessor chain); a phi if the walk
 * came back to a join being read (a cycle), the block is an open loop header
 * (an incomplete phi) or has no predecessor; the value of its single
 * predecessor; or else the values of all its predecessors (a join).
 */
static void rl_visit(ssa_builder *ssa, rl_walk *walk)
{
    ssa_rl_frame *frame = &ssa->rl[walk->top - 1];
    const ir_blk blk = frame->blk;
    const ssa_site site = {.var = walk->where.var, .blk = blk};
    const unsigned count = ssa->ir->blocks[blk].npreds;
    ir_ref val = find_def(ssa, site);
    if (val != IR_NIL) {
        rl_pop(walk, val);
        return;
    }
    const struct ssa_region *region = region_keeps(ssa, site);
    if (region != NULL) {
        rl_push(ssa, walk, region->head);
    } else if (loop_keeps(ssa, site)) {
        rl_push(ssa, walk,
                ir_pred(ssa->ir, (ir_pred_pos){.blk = blk, .idx = count - 1U}));
    } else if (ssa->visiting[blk] || ssa->unsealed[blk] || count == 0) {
        rl_new_phi(ssa, walk, site);
    } else if (count == 1) {
        rl_push(ssa, walk, ir_first_pred(ssa->ir, blk));
    } else {
        rl_start_join(ssa, walk, frame, count);
    }
}

/*
 * Iterative, on an explicit work stack, with lazy phis (the "marker"
 * variant of the paper): a sealed join first collects its predecessors'
 * values while marked `visiting`; it gets a phi only if they differ, or
 * if the walk came back to it (a cycle, through a loop), in which case the
 * placeholder phi is created at that moment and memoized. Trivial phis are
 * therefore never created at joins, which keeps the phi region small.
 * Each block is on the work stack at most once (a cycle always meets a
 * visiting join), so the stack needs at most max_blocks entries and the
 * collected values at most max_edges.
 */
ir_ref ssa_read(ssa_builder *ssa, ssa_site site, bool memo)
{
    ir_ref val = find_def(ssa, site);
    if (val != IR_NIL) {
        return val;
    }

    rl_walk walk = {
        .where = site,
        .memo = memo,
        .type = ssa->var_type[site.var],
        .result = IR_NIL,
    };
    rl_push(ssa, &walk, site.blk);
    while (walk.top > 0 && !walk.failed) {
        /* out of scratch: the memoizing ssa_write() calls are failing, and
         * without them the walk revisits joins exponentially */
        if (*ssa->err) {
            return IR_NIL;
        }
        if (walk.have) {
            rl_deliver(ssa, &walk);
        } else {
            rl_visit(ssa, &walk);
        }
    }
    return walk.failed ? IR_NIL : walk.result;
}

/* ------------------------------------------------------------------------ */
/* Loops                                                                    */
/* ------------------------------------------------------------------------ */

uint8_t *ssa_open_loop(ssa_builder *ssa, ir_blk header)
{
    if (header == IR_NIL) { /* the IR is out of blocks */
        return NULL;
    }
    ssa->unsealed[header] = 1;
    /* only with a single entry edge: it is then the oldest one, the last of
     * the predecessor chain, which loop_keeps() reads through */
    if (!ssa->wsets || ssa->nwsets >= ssa->wsets_cap ||
        ssa->ir->blocks[header].npreds != 1) {
        return NULL;
    }
    ssa->loop_wset[header] = (uint16_t)ssa->nwsets;
    uint8_t *wset = ssa->wsets + ((size_t)ssa->nwsets++ * ssa->wset_bytes);
    memset(wset, 0, ssa->wset_bytes);
    return wset;
}

/**
 * phi_var() - The variable of an incomplete phi of a loop header.
 * @ssa: the builder.
 * @header_phi: the header and the phi.
 *
 * rl_new_phi() memoized the phi in the header's definition list right
 * after creating it, before the phi could be used anywhere. A later entry
 * may hold the same phi for another variable (e.g. a copy of it made in
 * the header itself), but always nearer the head of the list, so the
 * oldest entry holding the phi is its own.
 *
 * Return: the index of the variable, or UINT16_MAX if none (an IR
 * failure).
 */
static uint32_t phi_var(const ssa_builder *ssa, ir_block_phi header_phi)
{
    uint32_t var = UINT16_MAX;
    for (const ssa_def *def = ssa->def_head[header_phi.blk]; def;
         def = def->next) {
        if (def->value == header_phi.phi) {
            var = def->var;
        }
    }
    return var;
}

/*
 * Every phi of the header was made while it was open, so all of them are
 * incomplete: they get their arguments here, newest first. Reading the
 * arguments makes no new phi in this header (each read finds the phi
 * there first).
 */
void ssa_seal_loop(ssa_builder *ssa, ir_blk header)
{
    const unsigned npreds = ssa->ir->blocks[header].npreds;
    for (ir_ref phi = ssa->ir->blocks[header].phi_head; phi != IR_NIL;
         phi = ir_a(ir_get(ssa->ir, phi))) {
        const ir_block_phi header_phi = {.blk = header, .phi = phi};
        const uint32_t var = phi_var(ssa, header_phi);
        if (var == UINT16_MAX) {
            ssa_fail(ssa);
            return;
        }
        ir_phi_alloc_args(ssa->ir, header_phi);
        for (unsigned i = 0; i < npreds; i++) {
            const ir_pred_pos pos = {.blk = header, .idx = i};
            const ssa_site pred = {
                .var = (uint16_t)var,
                .blk = ir_pred(ssa->ir, pos),
            };
            /* no memo at the latch: this read happens once */
            ir_phi_set_arg(ssa->ir, (ir_phi_slot){.phi = phi, .pred = i},
                           ssa_read(ssa, pred, false));
        }
        /* a trivial phi becomes a FWD, which keeps its link in the chain */
        ir_phi_try_trivial(ssa->ir, phi);
    }
    ssa->unsealed[header] = 0;
}
