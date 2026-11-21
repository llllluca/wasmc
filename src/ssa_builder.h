/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * ssa_builder.h - On-the-fly SSA construction of a function's variables.
 *
 * Builds SSA form while the CFG of an ir_func is being created, with the
 * algorithm of Braun et al., "Simple and Efficient Construction of Static
 * Single Assignment Form" (CC 2013): each block keeps the definitions made
 * in it, a read walks back through the predecessors, and phis are placed
 * where values meet. The caller owns the CFG and tells the builder only
 * three things: a variable is written (ssa_write()), it is read
 * (ssa_read()), and a loop opens (ssa_open_loop()) or is complete
 * (ssa_seal_loop()).
 *
 * The CFG must be structured: every predecessor of a block is known by
 * the time the block is begun, except for the back edges of a loop header.
 * So loop headers are the only blocks ever left unsealed (their phis stay
 * incomplete until ssa_seal_loop()).
 *
 * Variables are numbered 0..nvars-1. Only the variables the caller
 * actually writes need to go through the builder: one never written keeps
 * a single value, which the caller can hand out directly.
 *
 * The builder never allocates: its arrays and its definition nodes live in
 * the scratch region of the ir_func (ir_scratch_alloc()). The arrays are
 * allocated by ssa_alloc_arrays() and ssa_alloc_loops(); the nodes come
 * after them, as many as building needs.
 */
#ifndef SSA_BUILDER_H_INCLUDED
#define SSA_BUILDER_H_INCLUDED

#include "ir.h"
#include "wasmc/error.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * struct ssa_site - A variable in a block: where it is read or written.
 * @var: index of the variable.
 * @blk: the block.
 */
typedef struct ssa_site {
    uint16_t var;
    ir_blk blk;
} ssa_site;

/**
 * struct ssa_sizes - What the builder's arrays are sized from.
 * @blocks: &ir_func.max_blocks.
 * @edges: &ir_func.max_edges.
 * @nvars: number of variables.
 */
typedef struct ssa_sizes {
    uint32_t blocks;
    uint32_t edges;
    uint32_t nvars;
} ssa_sizes;

struct ssa_def;
struct ssa_rl_frame;

/**
 * struct ssa_builder - The SSA construction of one function.
 * @ir: the IR being built.
 * @err: where the first error is recorded; the caller's own error, so
 *     building stops with the caller's translation.
 * @var_type: [nvars] the IR type (IR_T_*) of each variable, owned by the
 *     caller.
 * @def_head: [max_blocks] per block, its newest definition (NULL: none).
 * @unsealed: [max_blocks] per block, set while it heads a still-open loop.
 * @visiting: [max_blocks] per block, set while its join is on the read
 *     walk's stack.
 * @rl: [@rl_cap] the read walk's stack.
 * @rl_cap: max_blocks + 1.
 * @rl_args: [@rl_args_cap] the values collected by the joins on that
 *     stack; free between calls (see ssa_temp_refs()).
 * @rl_args_cap: max_edges + 1.
 * @wsets: [loops][@wset_bytes] the variables written in each loop's body,
 *     or NULL (over the budget, or no loops).
 * @wsets_cap: loops in the function.
 * @nwsets: write sets handed out so far.
 * @wset_bytes: bytes of one write set.
 * @loop_wset: [max_blocks] loop header -> index of its write set; join
 *     of a region -> @wsets_cap + index of the region; IR_NIL if none.
 * @nvars: number of variables.
 * @wstart: [@nvars + 1] per variable, where its writes start in @wpos (the
 *     last entry: their total); NULL while regions are off.
 * @wfill: [@nvars] per variable, where its next write goes in @wpos.
 * @wpos: the event (@event) of every write of each variable, in order.
 * @regions: [@regions_cap] the closed regions (ssa_region_close()).
 * @nregions: regions recorded so far.
 * @regions_cap: their capacity.
 * @event: counts the region openings and closings.
 *
 * Its fields are private: they are public only so that the builder can be
 * embedded in its caller's state, without an allocation.
 */
typedef struct ssa_builder {
    ir_func *ir;
    wasmc_err_t *err;
    const uint8_t *var_type;

    struct ssa_def **def_head;
    uint8_t *unsealed;
    uint8_t *visiting;
    struct ssa_rl_frame *rl;
    uint32_t rl_cap;
    ir_ref *rl_args;
    uint32_t rl_args_cap;

    uint8_t *wsets;
    uint32_t wsets_cap, nwsets, wset_bytes;
    uint16_t *loop_wset;

    uint32_t nvars;
    uint32_t *wstart, *wfill, *wpos;
    struct ssa_region *regions;
    uint32_t nregions, regions_cap;
    uint32_t event;
} ssa_builder;

/**
 * ssa_init() - Start a builder.
 * @ssa: the builder.
 * @func: the IR it builds into.
 * @err: the caller's error, set on the first failure.
 * @var_type: the IR type of each variable; it must stay valid while the
 *     builder is used.
 */
void ssa_init(ssa_builder *ssa, ir_func *func, wasmc_err_t *err,
              const uint8_t *var_type);

/* the number of arrays ssa_alloc_arrays() allocates */
#define SSA_NARRAYS 6

/**
 * ssa_array_layouts() - The layouts of the arrays ssa_alloc_arrays()
 * allocates, in order (for ir_scratch_reserve()).
 * @sizes: what they are sized from.
 * @layouts: set to them.
 *
 * Return: their number, %SSA_NARRAYS.
 */
unsigned ssa_array_layouts(const ssa_sizes *sizes,
                           ir_layout layouts[SSA_NARRAYS]);

/**
 * ssa_alloc_arrays() - Allocate the builder's arrays from the scratch.
 * @ssa: the builder.
 * @sizes: what they are sized from.
 *
 * The arrays are also cleared. Call it before the first block is begun. On
 * an allocation failure, *@ssa->err is set.
 */
void ssa_alloc_arrays(ssa_builder *ssa, const ssa_sizes *sizes);

/**
 * ssa_loops_layout() - The layout of what ssa_alloc_loops() allocates.
 * @ssa: the builder, after ssa_alloc_arrays().
 * @loops: the number of loops of the function.
 *
 * Return: the layout, of size 0 if it allocates nothing.
 */
ir_layout ssa_loops_layout(const ssa_builder *ssa, uint32_t loops);

/**
 * ssa_alloc_loops() - Allocate the loops' write sets.
 * @ssa: the builder, after ssa_alloc_arrays().
 * @loops: the number of loops of the function.
 *
 * The write sets are optional: if they would take more than a fixed budget
 * there are none, ssa_open_loop() then hands out none, and reads through
 * loop headers make phis as in the plain algorithm.
 */
void ssa_alloc_loops(ssa_builder *ssa, uint32_t loops);

/**
 * ssa_region_counts() - Start the regions: the array of write counts.
 * @ssa: the builder, after ssa_alloc_loops().
 *
 * Regions are the function's `block` and `if` constructs. All the
 * predecessors of a region's join are inside it, so a variable the region
 * never writes has, at the join, its value at the region's entry: a read
 * goes straight there, without visiting the region's arms nor leaving a
 * definition in the join (which, with many variables read through long
 * chains of joins, would otherwise be most of the builder's memory).
 * Whether a region writes a variable is answered from the events of the
 * variable's writes.
 *
 * Regions are optional (they cost memory). To have them, the caller fills
 * the returned array with an upper bound of the writes of each variable
 * (ssa_write() calls), then calls ssa_alloc_regions().
 *
 * Return: [nvars] counts, cleared; NULL if out of scratch (*@ssa->err is
 * set).
 */
uint32_t *ssa_region_counts(ssa_builder *ssa);

/**
 * ssa_alloc_regions() - Allocate the regions.
 * @ssa: the builder, its counts filled (ssa_region_counts()).
 * @regions: an upper bound of the regions the function has.
 *
 * On an allocation failure, *@ssa->err is set.
 */
void ssa_alloc_regions(ssa_builder *ssa, uint32_t regions);

/**
 * ssa_regions_on() - Whether the builder has regions.
 * @ssa: the builder.
 *
 * Return: true after a successful ssa_alloc_regions().
 */
bool ssa_regions_on(const ssa_builder *ssa);

/**
 * ssa_region_open() - A region begins.
 * @ssa: the builder, with regions.
 *
 * Return: its begin event, for ssa_region_close().
 */
uint32_t ssa_region_open(ssa_builder *ssa);

/**
 * ssa_region_close() - A region ends.
 * @ssa: the builder, with regions.
 * @join: its join; it must have every predecessor, all inside the region.
 * @head: the block it was entered from (the body of a `block` continues
 *     in it); it must define nothing the region does not after the entry.
 * @begin: its begin event, from ssa_region_open().
 * @record: false if the join is unreachable (nothing will be read there).
 */
void ssa_region_close(ssa_builder *ssa, ir_blk join, ir_blk head,
                      uint32_t begin, bool record);

/**
 * ssa_write() - Define a variable in a block.
 * @ssa: the builder.
 * @site: the variable and the block (normally the block being built).
 * @value: its new value from this point of the block on.
 *
 * With regions, it is also a write of the variable at the current event.
 */
void ssa_write(ssa_builder *ssa, ssa_site site, ir_ref value);

/**
 * ssa_read() - The value of a variable in a block (Braun's readVariable()).
 * @ssa: the builder.
 * @site: the variable and the block.
 * @memo: remember the result in @site.blk, for later reads there.
 *
 * Every predecessor of a sealed block must be known.
 *
 * Return: the value, or IR_NIL if the IR ran out of capacity (*@ssa->err
 * is set).
 */
ir_ref ssa_read(ssa_builder *ssa, ssa_site site, bool memo);

/**
 * ssa_open_loop() - Start a loop: its header stays unsealed.
 * @ssa: the builder.
 * @header: the loop header, with its entry edge (if any) already added.
 *
 * If the header has exactly one predecessor (its entry edge) and the
 * budget allows, the loop gets a write set: the caller sets in it the bit
 * of every variable the loop's body writes. A read of any other variable
 * through the header then goes straight to the entry edge, without a phi.
 *
 * Return: the write set, cleared, a bitmap of nvars bits; or NULL if
 * none.
 */
uint8_t *ssa_open_loop(ssa_builder *ssa, ir_blk header);

/**
 * ssa_seal_loop() - Complete a loop: give its header's phis their
 * arguments.
 * @ssa: the builder.
 * @header: the loop header; every back edge to it has been added.
 */
void ssa_seal_loop(ssa_builder *ssa, ir_blk header);

/**
 * ssa_temp_refs() - The builder's value buffer, free between calls.
 * @ssa: the builder.
 * @cap: set to its length, &ssa_sizes.edges + 1.
 *
 * ssa_read() and ssa_seal_loop() use it while they run, and nothing is
 * kept in it between calls: the caller may use it as a temporary array of
 * IR values or blocks (e.g. the targets of a multi-way branch) until its
 * next call into the builder.
 *
 * Return: the buffer.
 */
ir_ref *ssa_temp_refs(ssa_builder *ssa, uint32_t *cap);

#endif /* SSA_BUILDER_H_INCLUDED */
