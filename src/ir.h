/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * ir.h - compact SSA/CFG intermediate representation for a WASM 1.0 JIT.
 *
 * Summary:
 *
 *  - One function at a time. All storage lives in ONE caller-supplied memory
 *    block (no malloc inside the IR). Resetting the IR for the next function is
 *    O(1); nothing is freed piecemeal, so there is no fragmentation.
 *  - Every instruction is a 64-bit slot. Operands are 16-bit slot indices
 *    (ir_ref) or 16-bit block indices (ir_blk). No pointers between IR objects.
 *  - An SSA value IS the index of the slot that defines it.
 *  - The IR is target independent: WASM semantics only.
 *
 * Error handling: capacity overflows and API misuse set a sticky error code in
 * ir_func.err; emit functions then return IR_NIL and do nothing. The caller
 * checks ir_failed() once (e.g. after ir_finalize()) and rejects the function.
 */
#ifndef IR_H
#define IR_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include "wasmc/error.h"
#include "wasmc/wasmc.h"

#ifdef IR_ENABLE_DUMP
#include <stdio.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Basic types                                                              */
/* ------------------------------------------------------------------------ */

typedef uint64_t ir_insn; /* one 64-bit slot                                  */
typedef uint16_t ir_ref;  /* slot index == SSA value id                       */
typedef uint16_t ir_blk;  /* basic block id                                   */

#define IR_NIL       0xFFFFu /* "no value" / "no block" / end of chain           */
#define IR_MAX_SLOTS 0xFFFFu /* slot ids are 0..0xFFFE */

/* Value types (WASM 1.0). Stored in 3 bits. */
enum ir_type {
    IR_T_VOID = 0,
    IR_T_I32 = 1,
    IR_T_I64 = 2,
    IR_T_F32 = 3,
    IR_T_F64 = 4,
    IR_T_COUNT = 5
};

/* Error codes (ir_func.err) are wasmc_err_t values: WASMC_OK or
 * WASMC_ERR_IR_* (see error.h). */
_Static_assert(WASMC_ERR_COUNT <= UINT8_MAX + 1,
               "wasmc_err_t does not fit in ir_func.err");

/* ------------------------------------------------------------------------ */
/* Instruction encoding                                                     */
/*                                                                          */
/*   63      48 47      32 31      16 15   11 10  8 7      0                */
/*  +----------+----------+----------+-------+-----+--------+               */
/*  |    c     |    b     |    a     | flags | ty  |   op   |               */
/*  +----------+----------+----------+-------+-----+--------+               */
/*                                                                          */
/* `ty` is the type of the value the instruction defines. Exceptions:       */
/*   - stores: ty = type of the stored value (a store defines no value)     */
/*   - comparisons: ty = I32 (result); operand type is that of operand `a`  */
/*   - conversions: ty = destination type; source type is that of `a`       */
/* ------------------------------------------------------------------------ */

/* bit position and width of each field */
enum {
    IR_OP_POS = 0,
    IR_OP_BITS = 8,
    IR_TY_POS = 8,
    IR_TY_BITS = 3,
    IR_FL_POS = 11,
    IR_FL_BITS = 5,
    IR_A_POS = 16,
    IR_B_POS = 32,
    IR_C_POS = 48,
    IR_OPND_BITS = 16, /* a, b and c */
};

static inline unsigned ir_field_mask(unsigned bits)
{
    return (1U << bits) - 1U;
}

static inline unsigned ir_op(ir_insn insn)
{
    return (unsigned)(insn >> IR_OP_POS) & ir_field_mask(IR_OP_BITS);
}
static inline unsigned ir_ty(ir_insn insn)
{
    return (unsigned)(insn >> IR_TY_POS) & ir_field_mask(IR_TY_BITS);
}
static inline unsigned ir_fl(ir_insn insn)
{
    return (unsigned)(insn >> IR_FL_POS) & ir_field_mask(IR_FL_BITS);
}
static inline uint16_t ir_a(ir_insn insn)
{
    return (uint16_t)(insn >> IR_A_POS);
}
static inline uint16_t ir_b(ir_insn insn)
{
    return (uint16_t)(insn >> IR_B_POS);
}
static inline uint16_t ir_c(ir_insn insn)
{
    return (uint16_t)(insn >> IR_C_POS);
}

static inline ir_insn ir_mk(unsigned opcode, unsigned type, unsigned flags,
                            unsigned op_a, unsigned op_b, unsigned op_c)
{
    const unsigned opnd_mask = ir_field_mask(IR_OPND_BITS);
    return ((ir_insn)(opcode & ir_field_mask(IR_OP_BITS)) << IR_OP_POS) |
           ((ir_insn)(type & ir_field_mask(IR_TY_BITS)) << IR_TY_POS) |
           ((ir_insn)(flags & ir_field_mask(IR_FL_BITS)) << IR_FL_POS) |
           ((ir_insn)(op_a & opnd_mask) << IR_A_POS) |
           ((ir_insn)(op_b & opnd_mask) << IR_B_POS) |
           ((ir_insn)(op_c & opnd_mask) << IR_C_POS);
}

/* the instruction with operand a replaced */
static inline ir_insn ir_set_a(ir_insn insn, unsigned op_a)
{
    const ir_insn mask = (ir_insn)ir_field_mask(IR_OPND_BITS) << IR_A_POS;
    return (insn & ~mask) |
           ((ir_insn)(op_a & ir_field_mask(IR_OPND_BITS)) << IR_A_POS);
}

/* the halves of a 32-bit value stored in two operands */
static inline unsigned ir_lo16(uint32_t val)
{
    return val & ir_field_mask(IR_OPND_BITS);
}
static inline unsigned ir_hi16(uint32_t val)
{
    return val >> IR_OPND_BITS;
}

/* flags */
#define IR_FL_WIDE                                                             \
    1u /* load/store: 32-bit offset lives in a following ext slot */

/* ------------------------------------------------------------------------ */
/* Opcodes                                                                  */
/*                                                                          */
/* X(id, "text", attributes, value-operand mask)                            */
/*                                                                          */
/* The mask says which of the a/b/c fields are SSA value references (used   */
/* by generic passes: operand resolution, use counting). Ops with variable  */
/* operand lists (PHI, CALL, CALL_INDIRECT) are handled specially by passes.*/
/* ------------------------------------------------------------------------ */

#define IR_A_EFFECT                                                            \
    1U               /* side effect: never removable                         */
#define IR_A_TERM 2U /* block terminator */
#define IR_A_READS                                                             \
    4U               /* reads memory/global state: removable, order-sensitive */
#define IR_A_TRAP 8U /* may trap at run time: never removable */

#define IR_M_A   1U
#define IR_M_B   2U
#define IR_M_C   4U
#define IR_M_AB  (IR_M_A | IR_M_B)
#define IR_M_ABC (IR_M_A | IR_M_B | IR_M_C)

/*
 * Field usage per op:
 *   PARAM       a = parameter index
 *   CONST32     b = low 16 bits, c = high 16 bits (i32 or f32 raw bits)
 *   CONST64     1 ext slot = raw 64 bits (i64 or f64)
 *   unary       a
 *   binary      a, b           (comparisons: ty = I32)
 *   SELECT      a = val if cond != 0, b = val otherwise, c = cond
 *   LOAD*       a = address, c = offset (or IR_FL_WIDE + ext slot with u32
 * offset) STORE*      a = address, b = value, c = offset (or IR_FL_WIDE as
 * above) MEM_GROW    a = delta pages GLOBAL_GET  b | c<<16 = global index
 *   GLOBAL_SET  a = value, b | c<<16 = global index
 *   CALL        a = nargs, b | c<<16 = function index, args in ext slots
 *   CALL_INDIRECT a = nargs, b = table index value, c = type index, args in ext
 *   BR          b = target block
 *   BR_IF       a = cond, b = block if cond != 0, c = block otherwise
 *   BR_TABLE    a = index value, c = n_targets - 1, targets (default LAST) in
 * ext RETURN      a = value or IR_NIL PHI         a = next phi of the block, b
 * = first arg slot, c = nargs FWD         a = next phi of the block (kept), b =
 * replacement value NOP         a = number of extra slots to skip (killed
 * multi-slot insn)
 */
#define IR_OPCODES(X)                                                          \
    X(NOP, "nop", 0, 0)                                                        \
    X(FWD, "fwd", 0, 0)                                                        \
    X(PARAM, "param", 0, 0)                                                    \
    X(PHI, "phi", 0, 0)                                                        \
    X(CONST32, "const32", 0, 0)                                                \
    X(CONST64, "const64", 0, 0)                                                \
    /* integer unary */                                                        \
    X(CLZ, "clz", 0, IR_M_A)                                                   \
    X(CTZ, "ctz", 0, IR_M_A)                                                   \
    X(POPCNT, "popcnt", 0, IR_M_A)                                             \
    X(EQZ, "eqz", 0, IR_M_A)                                                   \
    /* integer binary (ty = I32/I64) */                                        \
    X(ADD, "add", 0, IR_M_AB)                                                  \
    X(SUB, "sub", 0, IR_M_AB)                                                  \
    X(MUL, "mul", 0, IR_M_AB)                                                  \
    X(DIV_S, "div_s", IR_A_TRAP, IR_M_AB)                                      \
    X(DIV_U, "div_u", IR_A_TRAP, IR_M_AB)                                      \
    X(REM_S, "rem_s", IR_A_TRAP, IR_M_AB)                                      \
    X(REM_U, "rem_u", IR_A_TRAP, IR_M_AB)                                      \
    X(AND, "and", 0, IR_M_AB)                                                  \
    X(OR, "or", 0, IR_M_AB)                                                    \
    X(XOR, "xor", 0, IR_M_AB)                                                  \
    X(SHL, "shl", 0, IR_M_AB)                                                  \
    X(SHR_S, "shr_s", 0, IR_M_AB)                                              \
    X(SHR_U, "shr_u", 0, IR_M_AB)                                              \
    X(ROTL, "rotl", 0, IR_M_AB)                                                \
    X(ROTR, "rotr", 0, IR_M_AB)                                                \
    /* integer comparisons (ty = I32) */                                       \
    X(EQ, "eq", 0, IR_M_AB)                                                    \
    X(NE, "ne", 0, IR_M_AB)                                                    \
    X(LT_S, "lt_s", 0, IR_M_AB)                                                \
    X(LT_U, "lt_u", 0, IR_M_AB)                                                \
    X(GT_S, "gt_s", 0, IR_M_AB)                                                \
    X(GT_U, "gt_u", 0, IR_M_AB)                                                \
    X(LE_S, "le_s", 0, IR_M_AB)                                                \
    X(LE_U, "le_u", 0, IR_M_AB)                                                \
    X(GE_S, "ge_s", 0, IR_M_AB)                                                \
    X(GE_U, "ge_u", 0, IR_M_AB)                                                \
    /* float unary (ty = F32/F64) */                                           \
    X(FABS, "fabs", 0, IR_M_A)                                                 \
    X(FNEG, "fneg", 0, IR_M_A)                                                 \
    X(FCEIL, "fceil", 0, IR_M_A)                                               \
    X(FFLOOR, "ffloor", 0, IR_M_A)                                             \
    X(FTRUNC, "ftrunc", 0, IR_M_A)                                             \
    X(FNEAREST, "fnearest", 0, IR_M_A)                                         \
    X(FSQRT, "fsqrt", 0, IR_M_A)                                               \
    /* float binary */                                                         \
    X(FADD, "fadd", 0, IR_M_AB)                                                \
    X(FSUB, "fsub", 0, IR_M_AB)                                                \
    X(FMUL, "fmul", 0, IR_M_AB)                                                \
    X(FDIV, "fdiv", 0, IR_M_AB)                                                \
    X(FMIN, "fmin", 0, IR_M_AB)                                                \
    X(FMAX, "fmax", 0, IR_M_AB)                                                \
    X(FCOPYSIGN, "fcopysign", 0, IR_M_AB)                                      \
    /* float comparisons (ty = I32) */                                         \
    X(FEQ, "feq", 0, IR_M_AB)                                                  \
    X(FNE, "fne", 0, IR_M_AB)                                                  \
    X(FLT, "flt", 0, IR_M_AB)                                                  \
    X(FGT, "fgt", 0, IR_M_AB)                                                  \
    X(FLE, "fle", 0, IR_M_AB)                                                  \
    X(FGE, "fge", 0, IR_M_AB)                                                  \
    /* conversions (ty = destination type, source type = type of a) */         \
    X(WRAP, "wrap", 0, IR_M_A)         /* i64 -> i32                    */     \
    X(EXTEND_S, "extend_s", 0, IR_M_A) /* i32 -> i64                    */     \
    X(EXTEND_U, "extend_u", 0, IR_M_A)                                         \
    X(TRUNC_S, "trunc_s", IR_A_TRAP, IR_M_A) /* float -> int */                \
    X(TRUNC_U, "trunc_u", IR_A_TRAP, IR_M_A)                                   \
    X(CONVERT_S, "convert_s", 0, IR_M_A) /* int -> float                  */   \
    X(CONVERT_U, "convert_u", 0, IR_M_A)                                       \
    X(DEMOTE, "demote", 0, IR_M_A)   /* f64 -> f32                    */       \
    X(PROMOTE, "promote", 0, IR_M_A) /* f32 -> f64                    */       \
    X(REINTERPRET, "reinterpret", 0, IR_M_A)                                   \
    X(SELECT, "select", 0, IR_M_ABC)                                           \
    /* memory: LOAD..STORE32 must stay contiguous, loads first */              \
    X(LOAD, "load", IR_A_READS | IR_A_TRAP, IR_M_A)                            \
    X(LOAD8_S, "load8_s", IR_A_READS | IR_A_TRAP, IR_M_A)                      \
    X(LOAD8_U, "load8_u", IR_A_READS | IR_A_TRAP, IR_M_A)                      \
    X(LOAD16_S, "load16_s", IR_A_READS | IR_A_TRAP, IR_M_A)                    \
    X(LOAD16_U, "load16_u", IR_A_READS | IR_A_TRAP, IR_M_A)                    \
    X(LOAD32_S, "load32_s", IR_A_READS | IR_A_TRAP, IR_M_A)                    \
    X(LOAD32_U, "load32_u", IR_A_READS | IR_A_TRAP, IR_M_A)                    \
    X(STORE, "store", IR_A_EFFECT | IR_A_TRAP, IR_M_AB)                        \
    X(STORE8, "store8", IR_A_EFFECT | IR_A_TRAP, IR_M_AB)                      \
    X(STORE16, "store16", IR_A_EFFECT | IR_A_TRAP, IR_M_AB)                    \
    X(STORE32, "store32", IR_A_EFFECT | IR_A_TRAP, IR_M_AB)                    \
    X(MEM_SIZE, "memory.size", IR_A_READS, 0)                                  \
    X(MEM_GROW, "memory.grow", IR_A_EFFECT, IR_M_A)                            \
    X(GLOBAL_GET, "global.get", IR_A_READS, 0)                                 \
    X(GLOBAL_SET, "global.set", IR_A_EFFECT | IR_A_TRAP,                       \
      IR_M_A) /* aux stack check */                                            \
    X(CALL, "call", IR_A_EFFECT | IR_A_TRAP, 0)                                \
    X(CALL_INDIRECT, "call_indirect", IR_A_EFFECT | IR_A_TRAP, IR_M_B)         \
    /* terminators */                                                          \
    X(BR, "br", IR_A_TERM, 0)                                                  \
    X(BR_IF, "br_if", IR_A_TERM, IR_M_A)                                       \
    X(BR_TABLE, "br_table", IR_A_TERM, IR_M_A)                                 \
    X(RETURN, "return", IR_A_TERM, IR_M_A)                                     \
    X(UNREACHABLE, "unreachable", IR_A_TERM | IR_A_TRAP, 0) /* trap 0 */

#define IR_X_ENUM(id, name, attr, mask) IR_OP_##id,
enum ir_opcode { IR_OPCODES(IR_X_ENUM) IR_OP__COUNT };
#undef IR_X_ENUM

_Static_assert(IR_OP__COUNT <= (1 << IR_OP_BITS),
               "opcode does not fit in 8 bits");

/* Packed per-op info: bits 0-2 = value-operand mask, bits 3-6 = attributes. */
extern const uint8_t ir_op_info[IR_OP__COUNT];
enum { IR_INFO_VMASK_BITS = 3 };

static inline unsigned ir_op_vmask(unsigned opcode)
{
    return ir_op_info[opcode] & ir_field_mask(IR_INFO_VMASK_BITS);
}
static inline unsigned ir_op_attr(unsigned opcode)
{
    return ir_op_info[opcode] >> IR_INFO_VMASK_BITS;
}
static inline int ir_op_is_term(unsigned opcode)
{
    return (ir_op_attr(opcode) & IR_A_TERM) != 0;
}
static inline int ir_op_may_trap(unsigned opcode)
{
    return (ir_op_attr(opcode) & IR_A_TRAP) != 0;
}
static inline int ir_op_removable(unsigned opcode)
{
    return (ir_op_attr(opcode) & (IR_A_EFFECT | IR_A_TERM | IR_A_TRAP)) == 0;
}

/* ------------------------------------------------------------------------ */
/* Function container                                                       */
/* ------------------------------------------------------------------------ */

/* Basic block: a contiguous slot range [first,end) plus a phi chain. 12 bytes.
 * Successors are derived from the terminator (slot `term`).
 * Predecessors are a chain in the edge pool (most recently added first). */
typedef struct ir_block {
    uint16_t first; /* first body slot, IR_NIL until ir_block_begin()      */
    uint16_t end;   /* one past last body slot                              */
    uint16_t term;  /* slot of the terminator, IR_NIL until emitted         */
    uint16_t phi_head;  /* first phi of this block or IR_NIL  */
    uint16_t pred_head; /* first edge in the pred chain or IR_NIL */
    uint16_t npreds;
} ir_block;

typedef struct ir_edge {
    uint16_t from; /* predecessor block                                    */
    uint16_t next; /* next edge in the same block's pred chain             */
} ir_edge;

/* Capacities given to ir_init(). Size them from the WASM function body. */
typedef struct ir_limits {
    uint16_t insn_slots; /* instruction + ext slots: a bound, fixed          */
    uint16_t phi_slots;  /* first capacity of the phi region, which grows    */
    uint16_t max_blocks;
    uint16_t max_edges;
} ir_limits;

/*
 * Slot array layout (single array, two regions):
 *
 *   0 ........ ninsn       phi_base ........ phi_top ........ max_slots
 *   [ instructions + ext ] free [ phis + phi arg slots ] free
 *        grows up  ->                         grows up ->
 *
 * The low region has a fixed capacity, phi_base (an exact bound measured
 * before building); the phi region grows as needed: the array is the newest
 * work block of the allocator while the IR is built, so it is enlarged in
 * place (wasmc_allocator.work_grow), which may move it but keeps every
 * slot id. Hold no pointer into it across a call that may create a phi.
 * Block bodies only ever contain slots from the low region, so a block body is
 * always contiguous even though phis are created lazily (loop headers).
 * NOTE: slot index order is NOT program order for phis; passes must number
 * program positions themselves by walking blocks in layout order.
 */
struct ir_chunk;

/* entries of ir_func.const_cache (a power of two) */
#define IR_CONST_CACHE_BITS 7u
#define IR_CONST_CACHE      (1u << IR_CONST_CACHE_BITS)
/* ir_const_cache_idx(): small values of different types apart by this */
#define IR_CONST_TYPE_STRIDE 37u
/* ir_const_cache_idx(): the type goes into the top bits of a large value */
#define IR_CONST_TYPE_SHIFT 59u

typedef struct ir_func {
    ir_insn *insn;
    ir_block *blocks;
    ir_edge *edges;
    uint16_t *layout; /* block ids in emission (== reverse post order) order */
    void *arena;      /* work block of blocks, edges and layout             */
    uint32_t arena_size;
    /* scratch: the current chunk (none at first, see ir_init()) */
    uint8_t *scratch;
    uint32_t scratch_cap;
    uint32_t scratch_used;
    uint32_t scratch_base;  /* bytes used in the chunks below the current   */
    uint32_t scratch_peak;  /* most scratch bytes used at once              */
    struct ir_chunk *chunk; /* the current chunk, NULL: none                */
    const wasmc_allocator *mem; /* gives the arena, the slots, the chunks  */

    uint16_t max_slots; /* capacity of the slot array                     */
    uint16_t max_blocks;
    uint16_t max_edges;

    uint16_t ninsn;     /* next free slot in the low region                  */
    uint16_t phi_base;  /* first slot of the phi region (low capacity)       */
    uint16_t phi_top;   /* next free slot of the phi region                  */
    uint16_t phi_grows; /* times the phi region was enlarged                */
    uint16_t nblocks;
    uint16_t nedges;
    uint16_t nlayout;
    uint16_t nfwd; /* number of phis forwarded (removed)                  */
    uint16_t cur;  /* currently open block or IR_NIL                      */
    uint8_t terminated; /* current block already has its terminator */
    uint8_t finalized;  /* ir_finalize() ran: the slots no longer grow     */
    uint8_t err; /* wasmc_err_t (WASMC_ERR_IR_*), sticky                */
    /* recent constants, by a hash of their bits: a constant equal to one
     * already emitted is that one (checked on the slot itself, so a stale
     * or colliding entry is only a miss) */
    uint16_t const_cache[IR_CONST_CACHE];
} ir_func;

/* The header word of a constant: CONST32 (`bits` in its operands) or
 * CONST64 (its bits in the ext slot). */
static inline ir_insn ir_const_head(int wide, unsigned type, uint32_t bits)
{
    return wide
               ? ir_mk(IR_OP_CONST64, type, 0, 0, 0, 0)
               : ir_mk(IR_OP_CONST32, type, 0, 0, ir_lo16(bits), ir_hi16(bits));
}

/* The const_cache entry of a constant of IR type `type` with value bits
 * `val` (direct-mapped). Values below IR_CONST_CACHE, by far the most
 * frequent, each have their own entry, so they never collide with each
 * other; larger ones are hashed (multiplicative: masks such as 0xffffffff
 * and -1 spread). measure.c simulates the cache with the same entries.
 * (Direct-mapped on purpose: a new constant only ever replaces its own
 * entry, which keeps measure.c's simulation an upper bound across code the
 * IR may not emit; see measure.c.) */
static inline unsigned ir_const_cache_idx(unsigned type, uint64_t val)
{
    if (val < IR_CONST_CACHE) {
        return (unsigned)(val + ((uint64_t)type * IR_CONST_TYPE_STRIDE)) &
               (IR_CONST_CACHE - 1U);
    }
    uint64_t hash = (val ^ ((uint64_t)type << IR_CONST_TYPE_SHIFT)) *
                    UINT64_C(0x9E3779B97F4A7C15);
    return (unsigned)(hash >> ((sizeof hash * CHAR_BIT) - IR_CONST_CACHE_BITS));
}

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                */
/* ------------------------------------------------------------------------ */

/*
 * Allocate the IR of one function from `alloc`: two work blocks, the
 * blocks/edges/layout arena, then the slot array (lim->insn_slots +
 * lim->phi_slots slots), which stays the newest work block while the IR
 * is built, for the phi region to grow. The scratch starts empty and goes
 * on in chunks (twice the previous chunk, from 1 KiB to 16 KiB, or as large
 * as the allocation): from alloc->aux_alloc() while the IR is built, from
 * alloc->work_alloc() once it is finalized (above the slot array).
 * Returns WASMC_OK, WASMC_ERR_IR_MEM (limits out of range) or
 * WASMC_ERR_OUT_OF_MEMORY; on failure nothing stays allocated.
 */
wasmc_err_t ir_init(ir_func *func, const ir_limits *lim,
                    const wasmc_allocator *alloc);

/* Free the scratch chunks, the slot array and the arena: the "free the IR"
 * step. Safe after a failed ir_init(). */
void ir_free(ir_func *func);

/* The slot ids in use are below this: per-slot side arrays need this many
 * entries. */
static inline unsigned ir_nslots(const ir_func *func)
{
    return func->phi_top;
}

/* Is `ref` a slot of the phi region? */
static inline int ir_in_phi_region(const ir_func *func, ir_ref ref)
{
    return ref != IR_NIL && ref >= func->phi_base;
}

static inline int ir_failed(const ir_func *func)
{
    return func->err != WASMC_OK;
}

/* ------------------------------------------------------------------------ */
/* Scratch area (side arrays: use counts, live intervals, register map, ...)*/
/* Stack discipline: mark, allocate, release back to the mark.              */
/* ------------------------------------------------------------------------ */

/* size and alignment of a scratch allocation */
typedef struct {
    uint32_t size;
    uint32_t align;
} ir_layout;

/* the layout of an array of `count` objects of `type` */
#define IR_ARRAY(type, count)                                                  \
    ((ir_layout){.size = (uint32_t)((count) * sizeof(type)),                   \
                 .align = (uint32_t)_Alignof(type)})

/* The scratch bytes in use once `layout` is allocated with `used` bytes
 * already in use: the padding that aligns it, then its size. The region
 * starts 8-aligned, so offsets align like addresses. Code that lays out
 * scratch arrays without an ir_func measures them with this. */
static inline uint64_t ir_scratch_end(uint64_t used, ir_layout layout)
{
    const uint64_t align = layout.align ? layout.align : 1U;
    return (((used + align - 1U) / align) * align) + layout.size;
}

/* Allocates from the scratch; NULL on failure (WASMC_ERR_OUT_OF_MEMORY if a
 * chunk could not be allocated, or the IR's earlier error; func->err). */
void *ir_scratch_alloc(ir_func *func, ir_layout layout);

/* A hint: the next allocations will take `bytes` of scratch (their padding
 * included, as ir_scratch_end() counts it), so let them share one chunk of
 * the exact size rather than leave unused bytes at the end of smaller
 * chunks. A wrong count only costs memory. On failure func->err is set, as
 * for ir_scratch_alloc(). */
void ir_scratch_reserve(ir_func *func, uint32_t bytes);

/* The scratch bytes of `count` allocations laid out one after the other,
 * from an 8-aligned start, for ir_scratch_reserve(). */
static inline uint32_t ir_scratch_bytes(const ir_layout *layouts,
                                        unsigned count)
{
    uint64_t used = 0;
    for (unsigned i = 0; i < count; i++) {
        used = ir_scratch_end(used, layouts[i]);
    }
    return used > UINT32_MAX ? UINT32_MAX : (uint32_t)used;
}

/* The error after ir_scratch_alloc() returned NULL. */
static inline wasmc_err_t ir_scratch_err(const ir_func *func)
{
    return func->err ? (wasmc_err_t)func->err : WASMC_ERR_IR_SCRATCH;
}

/* The scratch in use, for ir_scratch_release(): the bytes used in every
 * region, alignment padding included. */
static inline uint32_t ir_scratch_mark(const ir_func *func)
{
    return func->scratch_base + func->scratch_used;
}

/* Frees what was allocated since `mark`, and the chunks that held it. */
void ir_scratch_release(ir_func *func, uint32_t mark);

/* ------------------------------------------------------------------------ */
/* Blocks                                                                   */
/* ------------------------------------------------------------------------ */

/* Create a block (a label). It may be branched to before it is begun. */
ir_blk ir_block_new(ir_func *func);

/* Start emitting into `blk`. Closes the currently open block, if any. Blocks
 * are laid out in the order they are begun; each block is begun at most once.
 */
void ir_block_begin(ir_func *func, ir_blk blk);

/* Close the currently open block. */
void ir_block_end(ir_func *func);

/* True if the open block already has a terminator (or no block is open). */
static inline int ir_block_terminated(const ir_func *func)
{
    return func->cur == IR_NIL || func->terminated;
}

/* ------------------------------------------------------------------------ */
/* Emission. All return IR_NIL (and set func->err) on failure.              */
/* ------------------------------------------------------------------------ */

ir_ref ir_emit_param(ir_func *func, unsigned type, unsigned index);
/* Constants: an equal constant already emitted in the function is
 * returned again (constants have no location: the code generator
 * rematerializes them at each use), so the same ref may come back. */
ir_ref ir_emit_const32(ir_func *func, unsigned type,
                       uint32_t bits); /* I32 or F32 */
ir_ref ir_emit_i64(ir_func *func, uint64_t bits);
ir_ref ir_emit_f64(ir_func *func, uint64_t bits);

/* Unary/binary/conversion ops. `type` = result type (see encoding notes). */
ir_ref ir_emit_unary(ir_func *func, unsigned opcode, unsigned type,
                     ir_ref op_a);
ir_ref ir_emit_binary(ir_func *func, unsigned opcode, unsigned type,
                      ir_ref op_a, ir_ref op_b);
ir_ref ir_emit_select(ir_func *func, unsigned type, ir_ref if_true,
                      ir_ref if_false, ir_ref cond);

/* `opcode` in IR_OP_LOAD..IR_OP_LOAD32_U; `type` = result type
 * (I32/I64/F32/F64). */
ir_ref ir_emit_load(ir_func *func, unsigned opcode, unsigned type, ir_ref addr,
                    uint32_t offset);
/* `opcode` in IR_OP_STORE..IR_OP_STORE32. Stored type is taken from `value`.
 */
void ir_emit_store(ir_func *func, unsigned opcode, ir_ref addr, ir_ref value,
                   uint32_t offset);

ir_ref ir_emit_mem_size(ir_func *func);
ir_ref ir_emit_mem_grow(ir_func *func, ir_ref delta);
ir_ref ir_emit_global_get(ir_func *func, unsigned type, uint32_t index);
void ir_emit_global_set(ir_func *func, uint32_t index, ir_ref value);

/* `type` = result type or IR_T_VOID. Returns the call's value ref (also valid,
 * but meaningless as a value, for void calls). */
ir_ref ir_emit_call(ir_func *func, unsigned type, uint32_t func_index,
                    const ir_ref *args, unsigned nargs);
ir_ref ir_emit_call_indirect(ir_func *func, unsigned type, uint16_t type_index,
                             ir_ref table_index, const ir_ref *args,
                             unsigned nargs);

/* Terminators. They also register the CFG edges. */
void ir_emit_br(ir_func *func, ir_blk target);
void ir_emit_br_if(ir_func *func, ir_ref cond, ir_blk if_true, ir_blk if_false);
/* `ntargets` includes the default target, which must be the last entry (>= 1).
 */
void ir_emit_br_table(ir_func *func, ir_ref index, const ir_blk *targets,
                      unsigned ntargets);
void ir_emit_return(ir_func *func, ir_ref value_or_nil);
void ir_emit_unreachable(ir_func *func);

/* ------------------------------------------------------------------------ */
/* Phis                                                                     */
/*                                                                          */
/* Typical use (Braun et al. construction):                                 */
/*   phi = ir_phi_new(func, blk, type);            // may be incomplete     */
/*   ... blk gets sealed: no more predecessors ...                          */
/*   ir_phi_alloc_args(func, (ir_block_phi){blk, phi}); // npreds slots      */
/*   ir_phi_set_arg(func, (ir_phi_slot){phi, i}, value) for each pred i     */
/*   ir_phi_try_trivial(func, phi);                                         */
/* Argument i corresponds to the i-th predecessor in ir_for_each_pred order.*/
/* Phis are created in the high slot region: they never disturb the         */
/* contiguity of any block body.                                            */
/* ------------------------------------------------------------------------ */

ir_ref ir_phi_new(ir_func *func, ir_blk blk, unsigned type);
/* phi `phi` of block `blk` */
typedef struct {
    ir_blk blk;
    ir_ref phi;
} ir_block_phi;

/* argument `pred` of phi `phi`: its value from predecessor `pred` */
typedef struct {
    ir_ref phi;
    unsigned pred;
} ir_phi_slot;

wasmc_err_t ir_phi_alloc_args(ir_func *func, ir_block_phi block_phi);
void ir_phi_set_arg(ir_func *func, ir_phi_slot slot, ir_ref value);
ir_ref ir_phi_arg(const ir_func *func, ir_phi_slot slot);

/* Follow forwarding (FWD) slots, compressing the path. IR_NIL stays IR_NIL. */
ir_ref ir_resolve(ir_func *func, ir_ref ref);

/* If all args (ignoring self references) are the same value, turn the phi into
 * a forward to that value and return it; otherwise return IR_NIL. */
ir_ref ir_phi_try_trivial(ir_func *func, ir_ref phi);

/* ir_phi_try_trivial() for a phi just completed by its creator, who
 * guarantees that nothing else refers to it yet (other than its own
 * arguments): if it is trivial and it and its argument slots are still the
 * newest slots of the phi region, they are freed and the phi is unlinked
 * from block `blk`. The returned value replaces the phi. */
ir_ref ir_phi_try_trivial_reclaim(ir_func *func, ir_ref phi, ir_blk blk);

/* Fixpoint over all phis (cascades through loops), then unlink forwarded phis
 * from block chains. Called by ir_finalize(). */
void ir_phi_simplify(ir_func *func);

/* Rewrite every operand in place so no FWD slot is referenced anymore. */
void ir_resolve_all(ir_func *func);

/* Close the open block, verify the CFG, simplify phis, resolve forwards,
 * and give back the unused end of the slot array (work_shrink, which may
 * move it: re-read func->insn). Returns func->err (WASMC_OK on success).
 * After this the IR is ready for passes.
 */
wasmc_err_t ir_finalize(ir_func *func);

/* ------------------------------------------------------------------------ */
/* Queries                                                                  */
/* ------------------------------------------------------------------------ */

/* Number of slots taken by the instruction at this header (>= 1). Walk a block
 * body with: for (i = first; i < end; i += ir_insn_len(func->insn[i])) */
unsigned ir_insn_len(ir_insn insn);

/* Does this instruction define an SSA value? */
int ir_defines_value(ir_insn insn);

/* Replace an instruction (all its slots) with NOP; body stays walkable.
 * Not for constants: the emitters share an equal constant among its uses. */
void ir_kill(ir_func *func, ir_ref ref);

static inline ir_insn ir_get(const ir_func *func, ir_ref ref)
{
    return func->insn[ref];
}

/* Constants */
static inline uint32_t ir_const32_bits(ir_insn insn)
{
    return (uint32_t)ir_b(insn) | ((uint32_t)ir_c(insn) << IR_OPND_BITS);
}
static inline uint64_t ir_const64_bits(const ir_func *func, ir_ref ref)
{
    return func->insn[ref + 1];
}

/* global index (GLOBAL_GET / GLOBAL_SET) and CALL function index */
static inline uint32_t ir_bc32(ir_insn insn)
{
    return (uint32_t)ir_b(insn) | ((uint32_t)ir_c(insn) << IR_OPND_BITS);
}

/* Memory offset of a load/store. */
uint32_t ir_mem_offset(const ir_func *func, ir_ref ref);

/* `idx`-th argument of CALL / CALL_INDIRECT. */
ir_ref ir_call_arg(const ir_func *func, ir_ref call, unsigned idx);

/* Successors of a block (from its terminator). */
unsigned ir_succ_count(const ir_func *func, ir_blk blk);
ir_blk ir_succ(const ir_func *func, ir_blk blk, unsigned idx);

/* Iterate predecessors in phi-argument order:
 *   uint16_t edge;
 *   IR_FOR_EACH_PRED(func, blk, edge) { ir_blk pred = func->edges[edge].from; }
 * Only stable once the block is sealed (no more edges will be added). */
#define IR_FOR_EACH_PRED(func, blk, edge)                                      \
    for ((edge) = (func)->blocks[(blk)].pred_head; (edge) != IR_NIL;           \
         (edge) = (func)->edges[(edge)].next)

/* predecessor `idx` of block `blk`, in pred-chain order */
typedef struct {
    ir_blk blk;
    unsigned idx;
} ir_pred_pos;

/* The predecessor at `pos` (O(pos.idx)); IR_NIL if out of range. */
ir_blk ir_pred(const ir_func *func, ir_pred_pos pos);

/* The first predecessor of `blk`, IR_NIL if none. */
ir_blk ir_first_pred(const ir_func *func, ir_blk blk);

/* ------------------------------------------------------------------------ */
/* Debug dump (host only; compile with -DIR_ENABLE_DUMP)                    */
/* ------------------------------------------------------------------------ */
#ifdef IR_ENABLE_DUMP
void ir_dump(const ir_func *func, FILE *out);
#endif

#ifdef __cplusplus
}
#endif

#endif /* IR_H */
