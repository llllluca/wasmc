/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/* ir.c - see ir.h */
#include "ir.h"
#include "wasmc/error.h"
#include "wasmc/wasmc.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifdef IR_ENABLE_DUMP
#include <stdio.h>
#endif

/* ------------------------------------------------------------------------ */
/* Op info table                                                            */
/* ------------------------------------------------------------------------ */

#define IR_X_INFO(id, name, attr, mask)                                        \
    [IR_OP_##id] = (uint8_t)((mask) | ((attr) << IR_INFO_VMASK_BITS)),
const uint8_t ir_op_info[IR_OP__COUNT] = {IR_OPCODES(IR_X_INFO)};
#undef IR_X_INFO

/* ------------------------------------------------------------------------ */
/* Internal helpers                                                         */
/* ------------------------------------------------------------------------ */

#define SLOT_SIZE sizeof(ir_insn)
#define MEM_ALIGN 8 /* of the arena and of the scratch chunks */

/* the slot ids are 0..IR_MAX_SLOTS - 2: the array never has more slots */
#define SLOTS_MAX (IR_MAX_SLOTS - 1U)

/* phi slots added at least each time the phi region grows */
#define PHI_GROW_MIN 16U

/* the smallest scratch chunk; each next one is twice the previous, up to
 * CHUNK_MAX (a larger allocation gets a chunk of its own size), which
 * bounds the bytes left unused at the end of a chunk; ir_scratch_reserve()
 * makes chunks of the exact size instead */
#define CHUNK_MIN 256U
#define CHUNK_MAX 4096U

/**
 * struct ir_chunk - Header of a scratch chunk, at its start.
 * @prev: the chunk below, or NULL if none.
 * @prev_scratch: the chunk below's memory, or NULL.
 * @prev_cap: its capacity.
 * @prev_used: the bytes used in it when this chunk was allocated.
 * @size: bytes of this chunk, header included, as allocated.
 * @aux: it is an aux block (wasmc_allocator.aux_alloc), not a work block.
 * @exact: it was made for ir_scratch_reserve(), of the exact size.
 */
struct ir_chunk {
    struct ir_chunk *prev;
    uint8_t *prev_scratch;
    uint32_t prev_cap;
    uint32_t prev_used;
    uint32_t size;
    bool aux;
    bool exact;
};

static void set_err(ir_func *func, wasmc_err_t err)
{
    if (func->err == WASMC_OK) {
        func->err = (uint8_t)err;
    }
}

static size_t align_up(size_t val, size_t alignment)
{
    return (val + alignment - 1) & ~(alignment - 1);
}

/* Reserve n contiguous slots in the low (instruction) region. */
static ir_ref alloc_up(ir_func *func, unsigned n)
{
    if (func->err) {
        return IR_NIL;
    }
    if ((unsigned)func->ninsn + n > func->phi_base) {
        set_err(func, WASMC_ERR_IR_SLOTS);
        return IR_NIL;
    }
    ir_ref ref = func->ninsn;
    func->ninsn = (uint16_t)(func->ninsn + n);
    return ref;
}

/* Enlarge the slot array so that the phi region has room for n more slots:
 * by half its capacity, at least PHI_GROW_MIN slots. The array is the
 * newest work block until the IR is finalized. */
static bool grow_phis(ir_func *func, unsigned n)
{
    const unsigned need = (unsigned)func->phi_top + n;
    if (func->finalized || need > SLOTS_MAX) {
        set_err(func,
                func->finalized ? WASMC_ERR_IR_STATE : WASMC_ERR_IR_SLOTS);
        return false;
    }
    const unsigned cap = (unsigned)func->max_slots - func->phi_base;
    unsigned slots =
        func->max_slots + (cap / 2U > PHI_GROW_MIN ? cap / 2U : PHI_GROW_MIN);
    if (slots < need) {
        slots = need;
    }
    if (slots > SLOTS_MAX) {
        slots = SLOTS_MAX;
    }
    ir_insn *insn = func->mem->work_grow(
        func->mem->ctx, func->insn, (uint32_t)(func->max_slots * SLOT_SIZE),
        (uint32_t)(slots * SLOT_SIZE));
    if (!insn) {
        set_err(func, WASMC_ERR_OUT_OF_MEMORY);
        return false;
    }
    func->insn = insn;
    func->max_slots = (uint16_t)slots;
    func->phi_grows++;
    return true;
}

/* Reserve n contiguous slots in the phi region; returns the lowest. It may
 * move the slot array. */
static ir_ref alloc_phi(ir_func *func, unsigned n)
{
    if (func->err) {
        return IR_NIL;
    }
    if ((unsigned)func->phi_top + n > func->max_slots && !grow_phis(func, n)) {
        return IR_NIL;
    }
    ir_ref ref = func->phi_top;
    func->phi_top = (uint16_t)(func->phi_top + n);
    return ref;
}

/* Reserve slots for an instruction in the open block. */
static ir_ref alloc_in_block(ir_func *func, unsigned n)
{
    if (func->err) {
        return IR_NIL;
    }
    if (func->cur == IR_NIL || func->terminated) {
        set_err(func, WASMC_ERR_IR_STATE);
        return IR_NIL;
    }
    return alloc_up(func, n);
}

/* 4 x u16 packed in ext slots (call args, br_table targets, phi args). */
enum { PACK_PER_SLOT = 4 };

static unsigned pack_shift(unsigned idx)
{
    return (idx % PACK_PER_SLOT) * IR_OPND_BITS;
}

static void pack_set(ir_insn *base, unsigned idx, uint16_t val)
{
    const ir_insn mask = (ir_insn)UINT16_MAX << pack_shift(idx);
    ir_insn *slot = &base[idx / PACK_PER_SLOT];
    *slot = (*slot & ~mask) | ((ir_insn)val << pack_shift(idx));
}

static uint16_t pack_get(const ir_insn *base, unsigned idx)
{
    return (uint16_t)(base[idx / PACK_PER_SLOT] >> pack_shift(idx));
}

static unsigned pack_slots(unsigned n)
{
    return (n + PACK_PER_SLOT - 1U) / PACK_PER_SLOT;
}

static void pack_fill_nil(ir_insn *base, unsigned nslots)
{
    for (unsigned i = 0; i < nslots; i++) {
        base[i] = ~(ir_insn)0;
    }
}

static int is_mem_op(unsigned opcode)
{
    return opcode >= IR_OP_LOAD && opcode <= IR_OP_STORE32;
}

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                */
/* ------------------------------------------------------------------------ */

/* Arena layout: blocks | edges | layout; the slot array is a block apart */
wasmc_err_t ir_init(ir_func *func, const ir_limits *lim,
                    const wasmc_allocator *alloc)
{
    memset(func, 0, sizeof *func);
    const uint32_t slots = (uint32_t)lim->insn_slots + lim->phi_slots;
    if (!alloc || lim->max_blocks >= IR_NIL || lim->max_edges >= IR_NIL ||
        slots == 0 || slots > SLOTS_MAX) {
        func->err = WASMC_ERR_IR_MEM;
        return WASMC_ERR_IR_MEM;
    }
    size_t size = (size_t)lim->max_blocks * sizeof(ir_block);
    size += (size_t)lim->max_edges * sizeof(ir_edge);
    size += (size_t)lim->max_blocks * sizeof(uint16_t);
    size = align_up(size ? size : 1U, MEM_ALIGN);
    func->mem = alloc;
    func->arena = alloc->work_alloc(alloc->ctx, (uint32_t)size);
    if (!func->arena) {
        func->err = WASMC_ERR_OUT_OF_MEMORY;
        return WASMC_ERR_OUT_OF_MEMORY;
    }
    func->arena_size = (uint32_t)size;
    func->insn = alloc->work_alloc(alloc->ctx, (uint32_t)(slots * SLOT_SIZE));
    if (!func->insn) {
        ir_free(func);
        func->err = WASMC_ERR_OUT_OF_MEMORY;
        return WASMC_ERR_OUT_OF_MEMORY;
    }
    uint8_t *ptr = (uint8_t *)func->arena;
    func->blocks = (ir_block *)ptr;
    ptr += (size_t)lim->max_blocks * sizeof(ir_block);
    func->edges = (ir_edge *)ptr;
    ptr += (size_t)lim->max_edges * sizeof(ir_edge);
    func->layout = (uint16_t *)ptr;
    func->max_slots = (uint16_t)slots;
    func->max_blocks = lim->max_blocks;
    func->max_edges = lim->max_edges;
    func->phi_base = lim->insn_slots;
    func->phi_top = lim->insn_slots;
    func->cur = IR_NIL;
    return WASMC_OK;
}

void ir_free(ir_func *func)
{
    const wasmc_allocator *mem = func->mem;
    if (!mem) {
        return;
    }
    ir_scratch_release(func, 0);
    if (func->insn) {
        mem->work_free(mem->ctx, func->insn,
                       (uint32_t)(func->max_slots * SLOT_SIZE));
        func->insn = NULL;
    }
    if (func->arena) {
        mem->work_free(mem->ctx, func->arena, func->arena_size);
        func->arena = NULL;
    }
}

/* Goes on in a new chunk that holds `layout`: twice the current chunk, or
 * CHUNK_MIN after none or an exact one, at most CHUNK_MAX, or more if
 * `layout` needs it; just enough for `layout` if `exact` or if that fails.
 * False if out of memory. */
static bool push_chunk(ir_func *func, ir_layout layout, bool exact)
{
    const size_t header = align_up(sizeof(struct ir_chunk), MEM_ALIGN);
    const uint64_t want = header + ir_scratch_end(0, layout);
    uint64_t size = func->chunk && !func->chunk->exact
                        ? 2U * (uint64_t)func->chunk->size
                        : CHUNK_MIN;
    if (size > CHUNK_MAX) {
        size = CHUNK_MAX;
    }
    if (size < want || exact) {
        size = want;
    }
    /* while the slot array may grow, it must stay the newest work block */
    const bool aux = !func->finalized;
    void *(*alloc)(void *, uint32_t) =
        aux ? func->mem->aux_alloc : func->mem->work_alloc;
    struct ir_chunk *chunk = NULL;
    if (size <= UINT32_MAX) {
        chunk = alloc(func->mem->ctx, (uint32_t)size);
        if (!chunk && size > want) {
            size = want;
            chunk = alloc(func->mem->ctx, (uint32_t)size);
        }
    }
    if (!chunk) {
        set_err(func, WASMC_ERR_OUT_OF_MEMORY);
        return false;
    }
    chunk->prev = func->chunk;
    chunk->prev_scratch = func->scratch;
    chunk->prev_cap = func->scratch_cap;
    chunk->prev_used = func->scratch_used;
    chunk->size = (uint32_t)size;
    chunk->aux = aux;
    chunk->exact = exact;
    func->chunk = chunk;
    func->scratch_base += func->scratch_used;
    func->scratch = (uint8_t *)chunk + header;
    func->scratch_cap = (uint32_t)(size - header);
    func->scratch_used = 0;
    return true;
}

/* Enlarges the current chunk in place, if it is an aux block, so that its
 * scratch reaches `end` bytes. False if it cannot. */
static bool extend_chunk(ir_func *func, uint64_t end)
{
    struct ir_chunk *chunk = func->chunk;
    if (!chunk || !chunk->aux) {
        return false;
    }
    const uint64_t added =
        align_up((size_t)(end - func->scratch_cap), MEM_ALIGN);
    const uint64_t size = (uint64_t)chunk->size + added;
    if (size > UINT32_MAX ||
        !func->mem->aux_grow(func->mem->ctx, chunk, chunk->size,
                             (uint32_t)size)) {
        return false;
    }
    chunk->size = (uint32_t)size;
    func->scratch_cap += (uint32_t)added;
    return true;
}

void *ir_scratch_alloc(ir_func *func, ir_layout layout)
{
    if (func->err) {
        return NULL;
    }
    uint64_t end = ir_scratch_end(func->scratch_used, layout);
    if ((end > func->scratch_cap || !func->scratch) &&
        !extend_chunk(func, end)) {
        if (!push_chunk(func, layout, false)) {
            return NULL;
        }
        end = ir_scratch_end(0, layout);
    }
    func->scratch_used = (uint32_t)end;
    if (ir_scratch_mark(func) > func->scratch_peak) {
        func->scratch_peak = ir_scratch_mark(func);
    }
    return func->scratch + (end - layout.size);
}

void ir_scratch_reserve(ir_func *func, uint32_t bytes)
{
    if (func->err || !bytes ||
        (func->scratch && func->scratch_cap - func->scratch_used >= bytes)) {
        return;
    }
    if (!extend_chunk(func, (uint64_t)func->scratch_used + bytes)) {
        push_chunk(func, (ir_layout){.size = bytes, .align = 1}, true);
    }
}

void ir_scratch_release(ir_func *func, uint32_t mark)
{
    /* a chunk is only made by an allocation, so one that starts at `mark`
     * holds nothing from before it */
    while (func->chunk && mark <= func->scratch_base) {
        struct ir_chunk *chunk = func->chunk;
        func->chunk = chunk->prev;
        func->scratch = chunk->prev_scratch;
        func->scratch_cap = chunk->prev_cap;
        func->scratch_base -= chunk->prev_used;
        if (chunk->aux) {
            func->mem->aux_free(func->mem->ctx, chunk, chunk->size);
        } else {
            func->mem->work_free(func->mem->ctx, chunk, chunk->size);
        }
    }
    func->scratch_used = mark - func->scratch_base;
}

/* ------------------------------------------------------------------------ */
/* Blocks                                                                   */
/* ------------------------------------------------------------------------ */

ir_blk ir_block_new(ir_func *func)
{
    if (func->err) {
        return IR_NIL;
    }
    if (func->nblocks >= func->max_blocks) {
        set_err(func, WASMC_ERR_IR_BLOCKS);
        return IR_NIL;
    }
    ir_blk ident = func->nblocks++;
    ir_block *block = &func->blocks[ident];
    block->first = block->end = block->term = block->phi_head =
        block->pred_head = IR_NIL;
    block->npreds = 0;
    return ident;
}

void ir_block_end(ir_func *func)
{
    if (func->cur == IR_NIL) {
        return;
    }
    func->blocks[func->cur].end = func->ninsn;
    func->cur = IR_NIL;
    func->terminated = 0;
}

void ir_block_begin(ir_func *func, ir_blk blk)
{
    if (func->err) {
        return;
    }
    ir_block_end(func);
    if (blk >= func->nblocks || func->blocks[blk].first != IR_NIL ||
        func->nlayout >= func->max_blocks) {
        set_err(func, WASMC_ERR_IR_STATE);
        return;
    }
    func->layout[func->nlayout++] = blk;
    func->blocks[blk].first = func->ninsn;
    func->cur = blk;
    func->terminated = 0;
}

/* Adds the edge from the current block (just terminated) to `dest`. */
static void add_edge(ir_func *func, ir_blk dest)
{
    if (func->err) {
        return;
    }
    if (dest >= func->nblocks) {
        set_err(func, WASMC_ERR_IR_STATE);
        return;
    }
    if (func->nedges >= func->max_edges) {
        set_err(func, WASMC_ERR_IR_EDGES);
        return;
    }
    uint16_t edge = func->nedges++;
    func->edges[edge].from = func->cur;
    func->edges[edge].next = func->blocks[dest].pred_head;
    func->blocks[dest].pred_head = edge;
    func->blocks[dest].npreds++;
}

/* ------------------------------------------------------------------------ */
/* Emission                                                                 */
/* ------------------------------------------------------------------------ */

static ir_ref emit1(ir_func *func, ir_insn insn)
{
    ir_ref ref = alloc_in_block(func, 1);
    if (ref != IR_NIL) {
        func->insn[ref] = insn;
    }
    return ref;
}

/* Emit a terminator header followed by `next` ext slots; returns its slot. */
/* Emits a terminator (with the ext slots of a br_table). */
static ir_ref emit_term(ir_func *func, ir_insn insn)
{
    ir_ref ref = alloc_in_block(func, ir_insn_len(insn));
    if (ref == IR_NIL) {
        return IR_NIL;
    }
    func->insn[ref] = insn;
    func->blocks[func->cur].term = ref;
    func->terminated = 1;
    return ref;
}

ir_ref ir_emit_param(ir_func *func, unsigned type, unsigned index)
{
    return emit1(func, ir_mk(IR_OP_PARAM, type, 0, index, 0, 0));
}

/*
 * A constant with header `head` (and, if `wide`, the 64 bits `bits` in its
 * ext slot): an equal one already emitted in the function if const_cache
 * has it, else a new one. Constants have no location (the code generator
 * rematerializes them at each use), so one in any block serves every
 * block. keccak's main function has 1855 i64.const of 47 values, 3710 of
 * its 8019 slots; nsichneu's 3291 i32.const of 12 values over 759 blocks.
 */
static ir_ref emit_const(ir_func *func, ir_insn head, uint64_t bits, bool wide,
                         unsigned type, uint64_t val)
{
    if (func->err) {
        return IR_NIL;
    }
    unsigned idx = ir_const_cache_idx(type, val);
    ir_ref ref = func->const_cache[idx];
    if (func->cur != IR_NIL && !func->terminated && ref < func->ninsn &&
        func->insn[ref] == head && (!wide || func->insn[ref + 1] == bits)) {
        return ref;
    }
    ref = alloc_in_block(func, wide ? 2U : 1U);
    if (ref == IR_NIL) {
        return IR_NIL;
    }
    func->insn[ref] = head;
    if (wide) {
        func->insn[ref + 1] = bits;
    }
    func->const_cache[idx] = ref;
    return ref;
}

ir_ref ir_emit_const32(ir_func *func, unsigned type, uint32_t bits)
{
    return emit_const(func, ir_const_head(0, type, bits), 0, false, type, bits);
}

ir_ref ir_emit_i64(ir_func *func, uint64_t bits)
{
    return emit_const(func, ir_const_head(1, IR_T_I64, 0), bits, true, IR_T_I64,
                      bits);
}

ir_ref ir_emit_f64(ir_func *func, uint64_t bits)
{
    return emit_const(func, ir_const_head(1, IR_T_F64, 0), bits, true, IR_T_F64,
                      bits);
}

ir_ref ir_emit_unary(ir_func *func, unsigned opcode, unsigned type, ir_ref op_a)
{
    return emit1(func, ir_mk(opcode, type, 0, op_a, 0, 0));
}

ir_ref ir_emit_binary(ir_func *func, unsigned opcode, unsigned type,
                      ir_ref op_a, ir_ref op_b)
{
    return emit1(func, ir_mk(opcode, type, 0, op_a, op_b, 0));
}

ir_ref ir_emit_select(ir_func *func, unsigned type, ir_ref if_true,
                      ir_ref if_false, ir_ref cond)
{
    return emit1(func, ir_mk(IR_OP_SELECT, type, 0, if_true, if_false, cond));
}

ir_ref ir_emit_load(ir_func *func, unsigned opcode, unsigned type, ir_ref addr,
                    uint32_t offset)
{
    if (opcode < IR_OP_LOAD || opcode > IR_OP_LOAD32_U) {
        set_err(func, WASMC_ERR_IR_STATE);
        return IR_NIL;
    }
    if (offset <= UINT16_MAX) {
        return emit1(func, ir_mk(opcode, type, 0, addr, 0, offset));
    }
    ir_ref ref = alloc_in_block(func, 2);
    if (ref == IR_NIL) {
        return IR_NIL;
    }
    func->insn[ref] = ir_mk(opcode, type, IR_FL_WIDE, addr, 0, 0);
    func->insn[ref + 1] = offset;
    return ref;
}

void ir_emit_store(ir_func *func, unsigned opcode, ir_ref addr, ir_ref value,
                   uint32_t offset)
{
    if (func->err) {
        return;
    }
    if (opcode < IR_OP_STORE || opcode > IR_OP_STORE32 ||
        value >= ir_nslots(func)) {
        set_err(func, WASMC_ERR_IR_STATE);
        return;
    }
    unsigned type = ir_ty(func->insn[value]);
    if (offset <= UINT16_MAX) {
        emit1(func, ir_mk(opcode, type, 0, addr, value, offset));
        return;
    }
    ir_ref ref = alloc_in_block(func, 2);
    if (ref == IR_NIL) {
        return;
    }
    func->insn[ref] = ir_mk(opcode, type, IR_FL_WIDE, addr, value, 0);
    func->insn[ref + 1] = offset;
}

ir_ref ir_emit_mem_size(ir_func *func)
{
    return emit1(func, ir_mk(IR_OP_MEM_SIZE, IR_T_I32, 0, 0, 0, 0));
}

ir_ref ir_emit_mem_grow(ir_func *func, ir_ref delta)
{
    return emit1(func, ir_mk(IR_OP_MEM_GROW, IR_T_I32, 0, delta, 0, 0));
}

ir_ref ir_emit_global_get(ir_func *func, unsigned type, uint32_t index)
{
    return emit1(func, ir_mk(IR_OP_GLOBAL_GET, type, 0, 0, ir_lo16(index),
                             ir_hi16(index)));
}

void ir_emit_global_set(ir_func *func, uint32_t index, ir_ref value)
{
    emit1(func, ir_mk(IR_OP_GLOBAL_SET, IR_T_VOID, 0, value, ir_lo16(index),
                      ir_hi16(index)));
}

ir_ref ir_emit_call(ir_func *func, unsigned type, uint32_t func_index,
                    const ir_ref *args, unsigned nargs)
{
    if (nargs > UINT16_MAX) {
        set_err(func, WASMC_ERR_IR_STATE);
        return IR_NIL;
    }
    unsigned nslots = pack_slots(nargs);
    ir_ref ref = alloc_in_block(func, 1 + nslots);
    if (ref == IR_NIL) {
        return IR_NIL;
    }
    func->insn[ref] = ir_mk(IR_OP_CALL, type, 0, nargs, ir_lo16(func_index),
                            ir_hi16(func_index));
    pack_fill_nil(&func->insn[ref + 1], nslots);
    for (unsigned i = 0; i < nargs; i++) {
        pack_set(&func->insn[ref + 1], i, args[i]);
    }
    return ref;
}

ir_ref ir_emit_call_indirect(ir_func *func, unsigned type, uint16_t type_index,
                             ir_ref table_index, const ir_ref *args,
                             unsigned nargs)
{
    if (nargs > UINT16_MAX) {
        set_err(func, WASMC_ERR_IR_STATE);
        return IR_NIL;
    }
    unsigned nslots = pack_slots(nargs);
    ir_ref ref = alloc_in_block(func, 1 + nslots);
    if (ref == IR_NIL) {
        return IR_NIL;
    }
    func->insn[ref] =
        ir_mk(IR_OP_CALL_INDIRECT, type, 0, nargs, table_index, type_index);
    pack_fill_nil(&func->insn[ref + 1], nslots);
    for (unsigned i = 0; i < nargs; i++) {
        pack_set(&func->insn[ref + 1], i, args[i]);
    }
    return ref;
}

void ir_emit_br(ir_func *func, ir_blk target)
{
    if (emit_term(func, ir_mk(IR_OP_BR, IR_T_VOID, 0, 0, target, 0)) !=
        IR_NIL) {
        add_edge(func, target);
    }
}

void ir_emit_br_if(ir_func *func, ir_ref cond, ir_blk if_true, ir_blk if_false)
{
    if (emit_term(func, ir_mk(IR_OP_BR_IF, IR_T_VOID, 0, cond, if_true,
                              if_false)) != IR_NIL) {
        add_edge(func, if_true);
        add_edge(func, if_false);
    }
}

void ir_emit_br_table(ir_func *func, ir_ref index, const ir_blk *targets,
                      unsigned ntargets)
{
    if (ntargets == 0 || ntargets > UINT16_MAX + 1U) {
        set_err(func, WASMC_ERR_IR_STATE);
        return;
    }
    unsigned nslots = pack_slots(ntargets);
    ir_ref ref = emit_term(
        func, ir_mk(IR_OP_BR_TABLE, IR_T_VOID, 0, index, 0, ntargets - 1));
    if (ref == IR_NIL) {
        return;
    }
    pack_fill_nil(&func->insn[ref + 1], nslots);
    for (unsigned i = 0; i < ntargets; i++) {
        pack_set(&func->insn[ref + 1], i, targets[i]);
        add_edge(func, targets[i]);
    }
}

void ir_emit_return(ir_func *func, ir_ref value_or_nil)
{
    unsigned type =
        value_or_nil == IR_NIL ? IR_T_VOID : ir_ty(func->insn[value_or_nil]);
    emit_term(func, ir_mk(IR_OP_RETURN, type, 0, value_or_nil, 0, 0));
}

void ir_emit_unreachable(ir_func *func)
{
    emit_term(func, ir_mk(IR_OP_UNREACHABLE, IR_T_VOID, 0, 0, 0, 0));
}

/* ------------------------------------------------------------------------ */
/* Phis                                                                     */
/* ------------------------------------------------------------------------ */

ir_ref ir_phi_new(ir_func *func, ir_blk blk, unsigned type)
{
    if (func->err) {
        return IR_NIL;
    }
    if (blk >= func->nblocks) {
        set_err(func, WASMC_ERR_IR_STATE);
        return IR_NIL;
    }
    ir_ref ref = alloc_phi(func, 1);
    if (ref == IR_NIL) {
        return IR_NIL;
    }
    func->insn[ref] =
        ir_mk(IR_OP_PHI, type, 0, func->blocks[blk].phi_head, IR_NIL, 0);
    func->blocks[blk].phi_head = ref;
    return ref;
}

wasmc_err_t ir_phi_alloc_args(ir_func *func, ir_block_phi block_phi)
{
    if (func->err) {
        return (wasmc_err_t)func->err;
    }
    const ir_ref phi = block_phi.phi;
    unsigned count = func->blocks[block_phi.blk].npreds;
    ir_insn phi_insn = func->insn[phi];
    if (ir_op(phi_insn) != IR_OP_PHI || ir_b(phi_insn) != IR_NIL) {
        set_err(func, WASMC_ERR_IR_STATE);
        return (wasmc_err_t)func->err;
    }
    if (count == 0) {
        return WASMC_OK;
    }
    unsigned nslots = pack_slots(count);
    ir_ref base = alloc_phi(func, nslots);
    if (base == IR_NIL) {
        return (wasmc_err_t)func->err;
    }
    pack_fill_nil(&func->insn[base], nslots);
    func->insn[phi] =
        ir_mk(IR_OP_PHI, ir_ty(phi_insn), 0, ir_a(phi_insn), base, count);
    return WASMC_OK;
}

void ir_phi_set_arg(ir_func *func, ir_phi_slot slot, ir_ref value)
{
    ir_insn phi_insn = func->insn[slot.phi];
    if (ir_op(phi_insn) != IR_OP_PHI || slot.pred >= ir_c(phi_insn)) {
        set_err(func, WASMC_ERR_IR_STATE);
        return;
    }
    pack_set(&func->insn[ir_b(phi_insn)], slot.pred, value);
}

ir_ref ir_phi_arg(const ir_func *func, ir_phi_slot slot)
{
    ir_insn phi_insn = func->insn[slot.phi];
    if (ir_op(phi_insn) != IR_OP_PHI || slot.pred >= ir_c(phi_insn)) {
        return IR_NIL;
    }
    return pack_get(&func->insn[ir_b(phi_insn)], slot.pred);
}

ir_ref ir_resolve(ir_func *func, ir_ref ref)
{
    if (ref == IR_NIL) {
        return IR_NIL;
    }
    ir_ref cur = ref;
    while (ir_op(func->insn[cur]) == IR_OP_FWD) {
        cur = ir_b(func->insn[cur]);
    }
    /* path compression */
    while (ref != cur) {
        ir_insn insn = func->insn[ref];
        ir_ref next = ir_b(insn);
        func->insn[ref] = ir_mk(IR_OP_FWD, ir_ty(insn), 0, ir_a(insn), cur, 0);
        ref = next;
    }
    return cur;
}

ir_ref ir_phi_try_trivial(ir_func *func, ir_ref phi)
{
    ir_insn phi_insn = func->insn[phi];
    if (ir_op(phi_insn) != IR_OP_PHI || ir_b(phi_insn) == IR_NIL) {
        return IR_NIL; /* not a phi, or args not allocated yet */
    }
    unsigned count = ir_c(phi_insn);
    const ir_insn *args = &func->insn[ir_b(phi_insn)];
    ir_ref same = IR_NIL;
    for (unsigned i = 0; i < count; i++) {
        ir_ref val = pack_get(args, i);
        if (val == IR_NIL) {
            return IR_NIL; /* incomplete */
        }
        val = ir_resolve(func, val);
        if (val == phi || val == same) {
            continue;
        }
        if (same != IR_NIL) {
            return IR_NIL; /* two distinct values: real phi */
        }
        same = val;
    }
    if (same == IR_NIL) {
        return IR_NIL; /* only self references (unreachable): keep */
    }
    /* keep `a` (chain link) so block chains stay walkable until unlinked */
    func->insn[phi] =
        ir_mk(IR_OP_FWD, ir_ty(phi_insn), 0, ir_a(phi_insn), same, 0);
    func->nfwd++;
    return same;
}

ir_ref ir_phi_try_trivial_reclaim(ir_func *func, ir_ref phi, ir_blk blk)
{
    ir_insn phi_insn = func->insn[phi];
    if (ir_op(phi_insn) != IR_OP_PHI || ir_b(phi_insn) == IR_NIL) {
        return IR_NIL;
    }
    ir_ref base = ir_b(phi_insn);
    unsigned nslots = pack_slots(ir_c(phi_insn));
    ir_ref same = ir_phi_try_trivial(func, phi);
    if (same == IR_NIL) {
        return IR_NIL;
    }
    /* the phi and its arguments are the newest phi-region slots, and the
     * phi heads its block's chain: give them back */
    if (base == phi + 1U && func->phi_top == (unsigned)base + nslots &&
        func->blocks[blk].phi_head == phi) {
        func->blocks[blk].phi_head = ir_a(func->insn[phi]);
        func->phi_top = phi;
        func->nfwd--;
    }
    return same;
}

void ir_phi_simplify(ir_func *func)
{
    int changed;
    do {
        changed = 0;
        for (unsigned blk = 0; blk < func->nblocks; blk++) {
            for (ir_ref phi = func->blocks[blk].phi_head; phi != IR_NIL;
                 phi = ir_a(func->insn[phi])) {
                if (ir_op(func->insn[phi]) == IR_OP_PHI &&
                    ir_phi_try_trivial(func, phi) != IR_NIL) {
                    changed = 1;
                }
            }
        }
    } while (changed);

    /* unlink forwarded phis from the chains */
    for (unsigned blk = 0; blk < func->nblocks; blk++) {
        ir_ref head = IR_NIL;
        ir_ref tail = IR_NIL;
        ir_ref phi = func->blocks[blk].phi_head;
        while (phi != IR_NIL) {
            ir_insn insn = func->insn[phi];
            ir_ref next = ir_a(insn);
            if (ir_op(insn) == IR_OP_PHI) {
                if (tail == IR_NIL) {
                    head = phi;
                } else {
                    func->insn[tail] = ir_set_a(func->insn[tail], phi);
                }
                tail = phi;
            }
            phi = next;
        }
        if (tail != IR_NIL) {
            func->insn[tail] = ir_set_a(func->insn[tail], IR_NIL);
        }
        func->blocks[blk].phi_head = head;
    }
}

/* ------------------------------------------------------------------------ */
/* Resolve all + finalize                                                   */
/* ------------------------------------------------------------------------ */

/* `count` refs packed from slot `base` on (call or phi arguments) */
typedef struct {
    ir_ref base;
    unsigned count;
} pack_list;

static void resolve_packed(ir_func *func, pack_list list)
{
    ir_insn *base = &func->insn[list.base];
    for (unsigned i = 0; i < list.count; i++) {
        pack_set(base, i, ir_resolve(func, pack_get(base, i)));
    }
}

/* the arguments of the phis of `blk` */
static void resolve_phis(ir_func *func, const ir_block *blk)
{
    for (ir_ref phi = blk->phi_head; phi != IR_NIL;
         phi = ir_a(func->insn[phi])) {
        ir_insn insn = func->insn[phi];
        if (ir_op(insn) == IR_OP_PHI && ir_b(insn) != IR_NIL) {
            resolve_packed(
                func, (pack_list){.base = ir_b(insn), .count = ir_c(insn)});
        }
    }
}

/* the value operands of instruction `ref`, call arguments included */
static void resolve_operands(ir_func *func, ir_ref ref)
{
    ir_insn insn = func->insn[ref];
    unsigned opcode = ir_op(insn);
    unsigned mask = ir_op_vmask(opcode);
    if (mask) {
        unsigned op_a = ir_a(insn);
        unsigned op_b = ir_b(insn);
        unsigned op_c = ir_c(insn);
        if (mask & IR_M_A) {
            op_a = ir_resolve(func, (ir_ref)op_a);
        }
        if (mask & IR_M_B) {
            op_b = ir_resolve(func, (ir_ref)op_b);
        }
        if (mask & IR_M_C) {
            op_c = ir_resolve(func, (ir_ref)op_c);
        }
        func->insn[ref] =
            ir_mk(opcode, ir_ty(insn), ir_fl(insn), op_a, op_b, op_c);
    }
    if (opcode == IR_OP_CALL || opcode == IR_OP_CALL_INDIRECT) {
        resolve_packed(
            func, (pack_list){.base = (ir_ref)(ref + 1), .count = ir_a(insn)});
    }
}

void ir_resolve_all(ir_func *func)
{
    for (unsigned k = 0; k < func->nlayout; k++) {
        const ir_block *blk = &func->blocks[func->layout[k]];
        resolve_phis(func, blk);
        for (unsigned i = blk->first; i < blk->end;
             i += ir_insn_len(func->insn[i])) {
            resolve_operands(func, (ir_ref)i);
        }
    }
}

/*
 * Gives back the unused end of the slot array, [phi_top, max_slots): the
 * phi region starts at an estimate (measure.c), which the build may not
 * use. After ir_finalize() the array no longer grows, and the code
 * generator allocates its arrays on top of it, so this lowers the peak.
 * The array is still the newest work block. Optional: without
 * work_shrink, or for less than SHRINK_MIN slots, it stays as it is.
 */
static void shrink_slots(ir_func *func)
{
    enum { SHRINK_MIN = 64 }; /* slots: 512 bytes */
    if (!func->mem->work_shrink ||
        (unsigned)func->max_slots - func->phi_top < SHRINK_MIN) {
        return;
    }
    ir_insn *insn = func->mem->work_shrink(
        func->mem->ctx, func->insn, (uint32_t)(func->max_slots * SLOT_SIZE),
        (uint32_t)(func->phi_top * SLOT_SIZE));
    if (insn) {
        func->insn = insn;
        func->max_slots = func->phi_top;
    }
}

wasmc_err_t ir_finalize(ir_func *func)
{
    func->finalized = 1;
    if (func->err) {
        return (wasmc_err_t)func->err;
    }
    ir_block_end(func);

    for (unsigned k = 0; k < func->nlayout; k++) {
        if (func->blocks[func->layout[k]].term == IR_NIL) {
            set_err(func,
                    WASMC_ERR_IR_STATE); /* block left without terminator */
        }
    }
    for (unsigned blk = 0; blk < func->nblocks; blk++) {
        if (func->blocks[blk].first == IR_NIL &&
            func->blocks[blk].npreds != 0) {
            set_err(func, WASMC_ERR_IR_STATE); /* branch to a block that was
                                               never emitted */
        }
    }
    if (func->err) {
        return (wasmc_err_t)func->err;
    }

    ir_phi_simplify(func);
    if (func->nfwd) {
        ir_resolve_all(func);
    }
    shrink_slots(func);
    return (wasmc_err_t)func->err;
}

/* ------------------------------------------------------------------------ */
/* Queries                                                                  */
/* ------------------------------------------------------------------------ */

unsigned ir_insn_len(ir_insn insn)
{
    unsigned opcode = ir_op(insn);
    switch (opcode) {
    case IR_OP_NOP:
        return 1U + ir_a(insn);
    case IR_OP_CONST64:
        return 2U;
    case IR_OP_CALL:
    case IR_OP_CALL_INDIRECT:
        return 1U + pack_slots(ir_a(insn));
    case IR_OP_BR_TABLE:
        return 1U + pack_slots((unsigned)ir_c(insn) + 1U);
    default:
        if (is_mem_op(opcode) && (ir_fl(insn) & IR_FL_WIDE)) {
            return 2U;
        }
        return 1U;
    }
}

int ir_defines_value(ir_insn insn)
{
    unsigned opcode = ir_op(insn);
    if (opcode == IR_OP_NOP || opcode == IR_OP_FWD || ir_op_is_term(opcode)) {
        return 0;
    }
    if (opcode >= IR_OP_STORE && opcode <= IR_OP_STORE32) {
        return 0;
    }
    if (opcode == IR_OP_GLOBAL_SET) {
        return 0;
    }
    return ir_ty(insn) != IR_T_VOID;
}

void ir_kill(ir_func *func, ir_ref ref)
{
    ir_insn insn = func->insn[ref];
    if (ir_op_is_term(ir_op(insn)) || ir_op(insn) == IR_OP_PHI ||
        ir_op(insn) == IR_OP_FWD || ir_op(insn) == IR_OP_CONST32 ||
        ir_op(insn) == IR_OP_CONST64) {
        set_err(func, WASMC_ERR_IR_STATE);
        return;
    }
    func->insn[ref] =
        ir_mk(IR_OP_NOP, IR_T_VOID, 0, ir_insn_len(insn) - 1U, 0, 0);
}

uint32_t ir_mem_offset(const ir_func *func, ir_ref ref)
{
    ir_insn insn = func->insn[ref];
    if (ir_fl(insn) & IR_FL_WIDE) {
        return (uint32_t)func->insn[ref + 1];
    }
    return ir_c(insn);
}

ir_ref ir_call_arg(const ir_func *func, ir_ref call, unsigned idx)
{
    if (idx >= ir_a(func->insn[call])) {
        return IR_NIL;
    }
    return pack_get(&func->insn[call + 1], idx);
}

unsigned ir_succ_count(const ir_func *func, ir_blk blk)
{
    ir_ref tmp = func->blocks[blk].term;
    if (tmp == IR_NIL) {
        return 0;
    }
    switch (ir_op(func->insn[tmp])) {
    case IR_OP_BR:
        return 1;
    case IR_OP_BR_IF:
        return 2;
    case IR_OP_BR_TABLE:
        return (unsigned)ir_c(func->insn[tmp]) + 1U;
    default:
        return 0;
    }
}

ir_blk ir_succ(const ir_func *func, ir_blk blk, unsigned idx)
{
    ir_ref tmp = func->blocks[blk].term;
    if (tmp == IR_NIL || idx >= ir_succ_count(func, blk)) {
        return IR_NIL;
    }
    ir_insn insn = func->insn[tmp];
    switch (ir_op(insn)) {
    case IR_OP_BR:
        return ir_b(insn);
    case IR_OP_BR_IF:
        return idx == 0 ? ir_b(insn) : ir_c(insn);
    default: /* BR_TABLE */
        return pack_get(&func->insn[tmp + 1], idx);
    }
}

ir_blk ir_pred(const ir_func *func, ir_pred_pos pos)
{
    unsigned idx = pos.idx;
    uint16_t edge;
    IR_FOR_EACH_PRED(func, pos.blk, edge)
    {
        if (idx-- == 0) {
            return func->edges[edge].from;
        }
    }
    return IR_NIL;
}

ir_blk ir_first_pred(const ir_func *func, ir_blk blk)
{
    return ir_pred(func, (ir_pred_pos){.blk = blk, .idx = 0});
}

/* ------------------------------------------------------------------------ */
/* Debug dump                                                               */
/* ------------------------------------------------------------------------ */
#ifdef IR_ENABLE_DUMP

#define IR_X_NAME(id, name, attr, mask) [IR_OP_##id] = (name),
static const char *const op_names[IR_OP__COUNT] = {IR_OPCODES(IR_X_NAME)};
#undef IR_X_NAME

static const char *const ty_names[] = {"void", "i32", "i64", "f32", "f64"};

static void put_ref(FILE *out, ir_ref ref)
{
    if (ref == IR_NIL) {
        fputs("nil", out);
    } else {
        fprintf(out, "v%u", ref);
    }
}

/* the callee and the arguments of a call or call_indirect */
static void dump_call(const ir_func *func, ir_ref ref, FILE *out)
{
    ir_insn insn = func->insn[ref];
    if (ir_op(insn) == IR_OP_CALL) {
        fprintf(out, " f%u", ir_bc32(insn));
    } else {
        fprintf(out, " type%u [", ir_c(insn));
        put_ref(out, ir_b(insn));
        fputc(']', out);
    }
    for (unsigned k = 0; k < ir_a(insn); k++) {
        fputs(k ? ", " : " (", out);
        put_ref(out, ir_call_arg(func, ref, k));
    }
    fputs(ir_a(insn) ? ")" : " ()", out);
}

static void dump_br_table(const ir_func *func, ir_ref ref, FILE *out)
{
    ir_insn insn = func->insn[ref];
    fputc(' ', out);
    put_ref(out, ir_a(insn));
    for (unsigned k = 0; k <= ir_c(insn); k++) {
        fprintf(out, "%sbb%u", k ? ", " : ", [",
                pack_get(&func->insn[ref + 1], k));
    }
    fputs("] (last = default)", out);
}

/* the value operands of the instruction (its opcode's operand mask) */
static void dump_operands(const ir_func *func, ir_ref ref, FILE *out)
{
    ir_insn insn = func->insn[ref];
    unsigned opcode = ir_op(insn);
    unsigned mask = ir_op_vmask(opcode);
    const char *sep = " ";
    if (mask & IR_M_A) {
        fputs(sep, out);
        sep = ", ";
        put_ref(out, ir_a(insn));
    }
    if (mask & IR_M_B) {
        fputs(sep, out);
        sep = ", ";
        put_ref(out, ir_b(insn));
    }
    if (mask & IR_M_C) {
        fputs(sep, out);
        put_ref(out, ir_c(insn));
    }
    if (opcode >= IR_OP_LOAD && opcode <= IR_OP_STORE32) {
        fprintf(out, " +%u", ir_mem_offset(func, ref));
    }
}

static void dump_insn(const ir_func *func, ir_ref ref, FILE *out)
{
    ir_insn insn = func->insn[ref];
    unsigned opcode = ir_op(insn);
    fprintf(out, "  ");
    if (ir_defines_value(insn)) {
        fprintf(out, "v%u = ", ref);
    }
    fprintf(out, "%s", op_names[opcode]);
    if (ir_ty(insn) != IR_T_VOID && opcode != IR_OP_RETURN) {
        fprintf(out, ".%s", ty_names[ir_ty(insn)]);
    }
    switch (opcode) {
    case IR_OP_PARAM:
        fprintf(out, " #%u", ir_a(insn));
        break;
    case IR_OP_CONST32:
        fprintf(out, " 0x%x", ir_const32_bits(insn));
        break;
    case IR_OP_CONST64:
        fprintf(out, " 0x%llx", (unsigned long long)func->insn[ref + 1]);
        break;
    case IR_OP_GLOBAL_GET:
        fprintf(out, " g%u", ir_bc32(insn));
        break;
    case IR_OP_GLOBAL_SET:
        fprintf(out, " g%u, ", ir_bc32(insn));
        put_ref(out, ir_a(insn));
        break;
    case IR_OP_CALL:
    case IR_OP_CALL_INDIRECT:
        dump_call(func, ref, out);
        break;
    case IR_OP_BR:
        fprintf(out, " bb%u", ir_b(insn));
        break;
    case IR_OP_BR_IF:
        fputc(' ', out);
        put_ref(out, ir_a(insn));
        fprintf(out, ", bb%u, bb%u", ir_b(insn), ir_c(insn));
        break;
    case IR_OP_BR_TABLE:
        dump_br_table(func, ref, out);
        break;
    case IR_OP_RETURN:
        if (ir_a(insn) != IR_NIL) {
            fputc(' ', out);
            put_ref(out, ir_a(insn));
        }
        break;
    default:
        dump_operands(func, ref, out);
        break;
    }
    fputc('\n', out);
}

void ir_dump(const ir_func *func, FILE *out)
{
    fprintf(out,
            "; slots: %u low + %u phi of %u, blocks %u, edges %u, err %u\n",
            func->ninsn, (unsigned)(func->phi_top - func->phi_base),
            func->max_slots, func->nblocks, func->nedges, func->err);
    for (unsigned k = 0; k < func->nlayout; k++) {
        ir_blk blk_id = func->layout[k];
        const ir_block *blk = &func->blocks[blk_id];
        uint16_t edge;
        fprintf(out, "bb%u:  ; preds:", blk_id);
        IR_FOR_EACH_PRED(func, blk_id, edge)
        fprintf(out, " bb%u", func->edges[edge].from);
        fputc('\n', out);
        for (ir_ref phi = blk->phi_head; phi != IR_NIL;
             phi = ir_a(func->insn[phi])) {
            ir_insn insn = func->insn[phi];
            if (ir_op(insn) == IR_OP_FWD) {
                fprintf(out, "  v%u = fwd v%u\n", phi, ir_b(insn));
                continue;
            }
            fprintf(out, "  v%u = phi.%s [", phi, ty_names[ir_ty(insn)]);
            for (unsigned i = 0; i < ir_c(insn); i++) {
                if (i) {
                    fputs(", ", out);
                }
                put_ref(out,
                        ir_phi_arg(func, (ir_phi_slot){.phi = phi, .pred = i}));
            }
            fputs("]\n", out);
        }
        unsigned end =
            blk->end == IR_NIL ? func->ninsn : blk->end; /* block still open */
        for (unsigned i = blk->first; i < end;
             i += ir_insn_len(func->insn[i])) {
            if (ir_op(func->insn[i]) != IR_OP_NOP) {
                dump_insn(func, (ir_ref)i, out);
            }
        }
    }
}
#endif /* IR_ENABLE_DUMP */
