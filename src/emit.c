/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * emit.c - RV32IM code generator: see emit.h and aotfmt.tex (runtime ABI).
 */
#include "emit.h"
#include "aotw.h"
#include "ir.h"
#include "ra.h"
#include "rv32.h"
#include "wamr_abi.h"
#include "wasm_decode.h"
#include "wasmc/error.h"
#include "wasmc/wasmc.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/*
 * Registers:
 *   s1 exec_env, s2 module_inst: pinned for the whole function.
 *   s3 linear memory base, s4 memory bound (size - 1): pinned if the
 *   function accesses the memory, allocatable otherwise.
 *   s0, s5-s11 (and a6, a7 for values not live across a call): allocated
 *   to SSA values by ra.c.
 *   Scratch for instruction selection, never allocated: t0/t1 first
 *   operand (lo/hi), t2/t3 second operand, a2/a3 result, t4/t5 address
 *   computations, t6 large stack offsets only, a4/a5 extra temporaries,
 *   a0-a5 argument passing at calls.
 *
 * Frame (sp 16-byte aligned, grows down):
 *   [frame-4] ra, then the saved callee-saved registers (s1, s2, ...),
 *   spill slots, outgoing stack arguments at [0..].
 */

#define SLOT_NONE RA_SLOT_NONE
/* emitter bits of ctx->flags, next to ra.h's RA_FOLDED and RA_NARROW and
 * the use count (bits 0-1) */
#define HI_ZERO   0x20u /* i64 whose high word is known zero (find_narrow()) */
#define HI_USED   0x10u /* i64 whose high word a user reads (find_dead_hi()) */
#define NTRAPS    16u
#define MAX_SAVED 13u
/* alignment padding between the groups of one scratch reservation */
#define MEM_PAD 16u
#define LOC_MEM 0x80000000u /* move operand: frame offset, not register */

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

enum {
    WORD_BITS = 32,             /* a register, an i32 */
    SIGN_SHIFT = WORD_BITS - 1, /* moves the sign bit to bit 0 or back */
    I32_SHIFT_MASK = WORD_BITS - 1,
    I64_SHIFT_MASK = (2 * WORD_BITS) - 1,
    STACK_ALIGN = 16,    /* the psABI keeps sp 16-byte aligned */
    PINNED_MEM_REGS = 2, /* s3 and s4, when memory is used */
};

/* the high word of INT32_MIN / INT64_MIN and of the float sign bit, as the
 * 20-bit immediate of a lui */
#define SIGN_BIT_HI20 (0x80000000U >> RV_POS_IMM_U)

/**
 * struct pmove_t - One word of the parallel copy of an edge (its phi moves).
 * @dst: Destination: a register (< 32) or LOC_MEM | frame offset.
 * @src: Source: a register, LOC_MEM | frame offset, or an immediate.
 * @src_const: @src is an immediate value.
 * @done: The move has been emitted.
 */
typedef struct {
    uint32_t dst, src;
    uint8_t src_const;
    uint8_t done;
} pmove_t;

/**
 * struct em_t - State of the code generator for one function.
 * @f: The function's finalized IR.
 * @w: The AOT writer.
 * @m: The decoded module.
 * @funcidx: Index of the function.
 * @ft: Type of the function.
 * @bounds: Emit software bounds checks.
 * @aux_check: Emit the aux stack check.
 * @uses_mem: The function accesses the linear memory.
 * @short_br: The function is below 4 KiB: conditional branches reach
 *   everything.
 * @err: First error, or WASMC_OK.
 * @reg_lo: [ir_nslots()] Register of the low word of each value, 0 = none.
 * @reg_hi: [ir_nslots()] Register of the high word of each value, 0 = none.
 * @slot_off: [ir_nslots()] Frame offset of each spilled value, else SLOT_NONE.
 * @flags: [ir_nslots()] Use count of each value (saturating at 2), RA_FOLDED.
 * @frame: Frame size.
 * @out_args: Bytes of outgoing stack arguments, at the bottom of the frame.
 * @saved: Callee-saved registers saved by the prologue.
 * @nsaved: Number of @saved registers.
 * @ncalls: Number of instructions that clobber the caller-saved registers.
 * @moves: [max_moves] Moves of the parallel copy being emitted.
 * @max_moves: Largest number of moves of an edge.
 * @blk: [nblocks] Fix-up chain of each block, then its code offset.
 * @edge_stub: [nblocks] Chain of a br_table copy stub to each block.
 * @ifstub: [nblocks] Chain of the copy stub of each block's br_if.
 * @thread: [nblocks] Block a jump to each block may go to instead.
 * @skip: [nblocks] The block emits no code at all.
 * @cur: The block being emitted.
 * @next: The block emitted after @cur, IR_NIL at the end: a jump to it
 *   falls through.
 * @trap: Chains of the trap stubs (li a1, id; j raise), by exception id.
 * @raise: Chain of the call to aot_set_exception_with_id, then @ret0.
 * @ret0: Chain of the return-zero path (exception already raised).
 * @ret: Chain of the shared epilogue.
 *
 * The per-value arrays come from ra.c; all the arrays are in the scratch
 * region of @f.
 */
typedef struct {
    ir_func *f;
    aotw_t *w;
    const wdec_module_t *m;
    uint32_t funcidx;
    wdec_functype_t ft;
    int bounds;
    int aux_check;
    int uses_mem;
    int short_br;
    wasmc_err_t err;

    uint8_t *reg_lo, *reg_hi;
    uint32_t *slot_off;
    uint8_t *flags;

    uint32_t frame;
    uint32_t out_args;
    uint8_t saved[MAX_SAVED];
    unsigned nsaved;
    uint32_t ncalls;

    pmove_t *moves;
    uint32_t max_moves;

    uint32_t *blk;
    uint32_t *edge_stub;
    uint32_t *ifstub;
    uint16_t *thread;
    uint8_t *skip;
    ir_blk cur;
    ir_blk next;

    uint32_t trap[NTRAPS];
    uint32_t raise;
    uint32_t ret0;
    uint32_t ret;
} em_t;

/**
 * typedef rtype_fn - Encoder of a register-register instruction.
 * @rd: Destination register.
 * @a: First source register.
 * @b: Second source register.
 *
 * Return: The encoded instruction.
 */
typedef uint32_t (*rtype_fn)(unsigned dst, unsigned idx, unsigned blk);

/**
 * typedef itype_fn - Encoder of an instruction with a 12-bit immediate.
 * @rd: Destination register (the source register for a store).
 * @rs1: Base or source register.
 * @imm: The immediate.
 *
 * Also fits the loads and the stores (rv_lw, rv_sw, ...).
 *
 * Return: The encoded instruction.
 */
typedef uint32_t (*itype_fn)(unsigned dst, unsigned rs1, int32_t imm);

/**
 * emit() - Emit one instruction.
 * @ctx: Emitter state.
 * @insn: The encoded instruction.
 */
static void emit(em_t *ctx, uint32_t insn)
{
    aotw_emit32(ctx->w, insn);
}

/**
 * fail() - Record an error, keeping the first one.
 * @ctx: Emitter state.
 * @err: The error.
 */
static void fail(em_t *ctx, wasmc_err_t err)
{
    if (!ctx->err) {
        ctx->err = err;
    }
}

/**
 * unsupported() - Record that the function uses something not supported.
 * @ctx: Emitter state.
 */
static void unsupported(em_t *ctx)
{
    fail(ctx, WASMC_ERR_UNSUPPORTED);
}

/**
 * is_wide() - Check whether an IR type takes two words.
 * @type: IR type.
 *
 * Return: 1 for i64 and f64, else 0.
 */
static int is_wide(unsigned type)
{
    return type == IR_T_I64 || type == IR_T_F64;
}

/**
 * is_wide_param() - Check whether a WASM value type takes two words.
 * @type: WASM value type.
 *
 * Return: 1 for i64 and f64, else 0.
 */
static int is_wide_param(wdec_valtype_t type)
{
    return type == WDEC_TYPE_I64 || type == WDEC_TYPE_F64;
}

/**
 * val_size() - Size of a value of an IR type.
 * @type: IR type.
 *
 * Return: 8 for i64 and f64, else 4.
 */
static uint32_t val_size(unsigned type)
{
    return is_wide(type) ? (uint32_t)sizeof(uint64_t)
                         : (uint32_t)sizeof(uint32_t);
}

/**
 * align_up() - Round up to a multiple of a power of two.
 * @val: The value.
 * @align: The alignment, a power of two.
 *
 * Return: @val rounded up to a multiple of @align.
 */
static uint32_t align_up(uint32_t val, uint32_t align)
{
    return (val + align - 1U) & ~(align - 1U);
}

/**
 * in_range() - Check whether an opcode is in a range of opcodes.
 * @opcode: The opcode.
 * @low: First opcode of the range.
 * @high: Last opcode of the range.
 *
 * The ranges follow the order of IR_OPCODES in ir.h.
 *
 * Return: 1 if @low <= @opcode <= @high, else 0.
 */
static int in_range(unsigned opcode, unsigned low, unsigned high)
{
    return opcode >= low && opcode <= high;
}

/**
 * emits_code() - Check whether an instruction emits code where it stands.
 * @opcode: The opcode.
 *
 * Constants are rematerialized at each use and nops vanish: they emit no code
 * of their own.
 *
 * Return: 0 for constants and nops, else 1.
 */
static int emits_code(unsigned opcode)
{
    return opcode != IR_OP_CONST32 && opcode != IR_OP_CONST64 &&
           opcode != IR_OP_NOP;
}

/* ------------------------------------------------------------------------ */
/* Opcode tables                                                            */
/* ------------------------------------------------------------------------ */

/* Operations that clobber the caller-saved registers: calls,
 * runtime helpers and the soft-float intrinsics, i.e. every float
 * operation but the sign-bit ones (i64 div/rem: see clobbers_regs()). */
static const uint8_t op_clobbers[IR_OP__COUNT] = {
    [IR_OP_CALL] = 1,      [IR_OP_CALL_INDIRECT] = 1, [IR_OP_MEM_GROW] = 1,
    [IR_OP_TRUNC_S] = 1,   [IR_OP_TRUNC_U] = 1,       [IR_OP_CONVERT_S] = 1,
    [IR_OP_CONVERT_U] = 1, [IR_OP_DEMOTE] = 1,        [IR_OP_PROMOTE] = 1,
    [IR_OP_FSQRT] = 1,     [IR_OP_FCEIL] = 1,         [IR_OP_FFLOOR] = 1,
    [IR_OP_FTRUNC] = 1,    [IR_OP_FNEAREST] = 1,      [IR_OP_FADD] = 1,
    [IR_OP_FSUB] = 1,      [IR_OP_FMUL] = 1,          [IR_OP_FDIV] = 1,
    [IR_OP_FMIN] = 1,      [IR_OP_FMAX] = 1,          [IR_OP_FEQ] = 1,
    [IR_OP_FNE] = 1,       [IR_OP_FLT] = 1,           [IR_OP_FGT] = 1,
    [IR_OP_FLE] = 1,       [IR_OP_FGE] = 1,
};

/* i32 operations that are one register-register instruction */
static const rtype_fn alu_rr[IR_OP__COUNT] = {
    [IR_OP_ADD] = rv_add,    [IR_OP_SUB] = rv_sub,    [IR_OP_MUL] = rv_mul,
    [IR_OP_DIV_S] = rv_div,  [IR_OP_DIV_U] = rv_divu, [IR_OP_REM_S] = rv_rem,
    [IR_OP_REM_U] = rv_remu, [IR_OP_AND] = rv_and,    [IR_OP_OR] = rv_or,
    [IR_OP_XOR] = rv_xor,    [IR_OP_SHL] = rv_sll,    [IR_OP_SHR_S] = rv_sra,
    [IR_OP_SHR_U] = rv_srl,
};

/* ... and their 12-bit immediate forms */
static const itype_fn alu_ri[IR_OP__COUNT] = {
    [IR_OP_ADD] = rv_addi,
    [IR_OP_AND] = rv_andi,
    [IR_OP_OR] = rv_ori,
    [IR_OP_XOR] = rv_xori,
};

/* Soft-float intrinsic of each float operation, by operand width [f32,
 * f64]; 0 (aot_set_exception_with_id) = none. */
static const uint8_t float_sym[IR_OP__COUNT][2] = {
    [IR_OP_FADD] = {WAMR_SYM_F32_ADD, WAMR_SYM_F64_ADD},
    [IR_OP_FSUB] = {WAMR_SYM_F32_SUB, WAMR_SYM_F64_SUB},
    [IR_OP_FMUL] = {WAMR_SYM_F32_MUL, WAMR_SYM_F64_MUL},
    [IR_OP_FDIV] = {WAMR_SYM_F32_DIV, WAMR_SYM_F64_DIV},
    [IR_OP_FMIN] = {WAMR_SYM_F32_MIN, WAMR_SYM_F64_MIN},
    [IR_OP_FMAX] = {WAMR_SYM_F32_MAX, WAMR_SYM_F64_MAX},
    [IR_OP_FSQRT] = {WAMR_SYM_F32_SQRT, WAMR_SYM_F64_SQRT},
    [IR_OP_FCEIL] = {WAMR_SYM_F32_CEIL, WAMR_SYM_F64_CEIL},
    [IR_OP_FFLOOR] = {WAMR_SYM_F32_FLOOR, WAMR_SYM_F64_FLOOR},
    [IR_OP_FTRUNC] = {WAMR_SYM_F32_TRUNC, WAMR_SYM_F64_TRUNC},
    [IR_OP_FNEAREST] = {WAMR_SYM_F32_RINT, WAMR_SYM_F64_RINT},
    [IR_OP_DEMOTE] = {WAMR_SYM_F64_TO_F32, WAMR_SYM_F64_TO_F32},
    [IR_OP_PROMOTE] = {WAMR_SYM_F32_TO_F64, WAMR_SYM_F32_TO_F64},
};

/**
 * struct cmp_t - An ordered comparison x OP y computed as x < y.
 * @swap: Compare y < x: gt and le.
 * @inv: Invert the result: le and ge.
 * @uns: Unsigned comparison.
 *
 * gt = y < x, le = !(y < x), ge = !(x < y).
 */
typedef struct {
    uint8_t swap, inv, uns;
} cmp_t;

_Static_assert(IR_OP_LT_U == IR_OP_LT_S + 1 && IR_OP_GT_S == IR_OP_LT_U + 1 &&
                   IR_OP_GT_U == IR_OP_GT_S + 1 &&
                   IR_OP_LE_S == IR_OP_GT_U + 1 &&
                   IR_OP_LE_U == IR_OP_LE_S + 1 &&
                   IR_OP_GE_S == IR_OP_LE_U + 1 && IR_OP_GE_U == IR_OP_GE_S + 1,
               "ordered comparisons: lt, gt, le, ge, signed then unsigned");

/**
 * is_ordered_cmp() - Check for an ordered integer comparison.
 * @opcode: The opcode.
 *
 * Return: 1 for lt, gt, le, ge (signed or unsigned), else 0.
 */
static int is_ordered_cmp(unsigned opcode)
{
    return in_range(opcode, IR_OP_LT_S, IR_OP_GE_U);
}

/**
 * cmp_form() - Form of an ordered comparison as x < y.
 * @opcode: An ordered comparison (is_ordered_cmp()).
 *
 * Return: How to compute x op y from x < y: operands swapped, result inverted,
 *   unsigned.
 */
static cmp_t cmp_form(unsigned opcode)
{
    static const cmp_t form[] = {
        {0, 0, 0}, {0, 0, 1}, /* lt_s, lt_u */
        {1, 0, 0}, {1, 0, 1}, /* gt */
        {1, 1, 0}, {1, 1, 1}, /* le */
        {0, 1, 0}, {0, 1, 1}, /* ge */
    };
    return form[opcode - IR_OP_LT_S];
}

/* ------------------------------------------------------------------------ */
/* Emission helpers                                                         */
/* ------------------------------------------------------------------------ */

/**
 * li() - Load a 32-bit constant into a register.
 * @ctx: Emitter state.
 * @dst: Destination register.
 * @val: The constant.
 *
 * Emits addi for a 12-bit value, else lui and an optional addi.
 */
static void li(em_t *ctx, unsigned dst, uint32_t val)
{
    int32_t sval = (int32_t)val;
    if (rv_fits12(sval)) {
        emit(ctx, rv_addi(dst, RV_ZERO, sval));
        return;
    }
    int32_t lower = rv_lo12(val);
    emit(ctx, rv_lui(dst, rv_hi20(val)));
    if (lower) {
        emit(ctx, rv_addi(dst, dst, lower));
    }
}

/**
 * mv() - Copy a register, unless it is the destination already.
 * @ctx: Emitter state.
 * @dst: Destination register.
 * @src: Source register.
 */
static void mv(em_t *ctx, unsigned dst, unsigned src)
{
    if (dst != src) {
        emit(ctx, rv_mv(dst, src));
    }
}

/**
 * add_k() - Add an unsigned constant: @dst = @src + @imm.
 * @ctx: Emitter state.
 * @dst: Destination register.
 * @src: Source register.
 * @imm: The constant.
 * @tmp: Register for an @imm too large for addi; may be @dst.
 */
static void add_k(em_t *ctx, unsigned dst, unsigned src, uint32_t imm,
                  unsigned tmp)
{
    if (imm <= RV_IMM12_MAX) {
        emit(ctx, rv_addi(dst, src, (int32_t)imm));
        return;
    }
    li(ctx, tmp, imm);
    emit(ctx, rv_add(dst, src, tmp));
}

/**
 * sub_k() - Subtract an unsigned constant: @dst = @src - @imm.
 * @ctx: Emitter state.
 * @dst: Destination register.
 * @src: Source register.
 * @imm: The constant.
 * @tmp: Register for an @imm too large for addi; may be @dst.
 */
static void sub_k(em_t *ctx, unsigned dst, unsigned src, uint32_t imm,
                  unsigned tmp)
{
    if (imm <= (uint32_t)-RV_IMM12_MIN) {
        emit(ctx, rv_addi(dst, src, -(int32_t)imm));
        return;
    }
    li(ctx, tmp, imm);
    emit(ctx, rv_sub(dst, src, tmp));
}

/**
 * mem_word() - Load or store a word at base + off.
 * @ctx: Emitter state.
 * @encode: rv_lw or rv_sw.
 * @reg: Register loaded or stored.
 * @base: Base register, not t6.
 * @off: Byte offset; a large one goes through t6.
 */
static void mem_word(em_t *ctx, itype_fn encode, unsigned reg, unsigned base,
                     uint32_t off)
{
    if (off > RV_IMM12_MAX) {
        add_k(ctx, RV_T6, base, off, RV_T6);
        base = RV_T6;
        off = 0;
    }
    emit(ctx, encode(reg, base, (int32_t)off));
}

/**
 * lw_at() - Load a word: @dst = [@base + @off].
 * @ctx: Emitter state.
 * @dst: Destination register.
 * @base: Base register, not t6.
 * @off: Byte offset.
 */
static void lw_at(em_t *ctx, unsigned dst, unsigned base, uint32_t off)
{
    mem_word(ctx, rv_lw, dst, base, off);
}

/**
 * sw_at() - Store a word: [@base + @off] = @src.
 * @ctx: Emitter state.
 * @src: Register stored.
 * @base: Base register, not t6.
 * @off: Byte offset.
 */
static void sw_at(em_t *ctx, unsigned src, unsigned base, uint32_t off)
{
    mem_word(ctx, rv_sw, src, base, off);
}

/**
 * jump() - Jump to the target of a fix-up chain.
 * @ctx: Emitter state.
 * @chain: The chain (target bound now or later).
 */
static void jump(em_t *ctx, uint32_t *chain)
{
    aotw_jump(ctx->w, chain, RV_ZERO);
}

/**
 * jump_trap() - Jump to the stub raising a trap.
 * @ctx: Emitter state.
 * @ident: The WAMR exception id.
 */
static void jump_trap(em_t *ctx, unsigned ident)
{
    jump(ctx, &ctx->trap[ident]);
}

/**
 * chain_pending() - Check whether a chain has jumps waiting for it.
 * @chain: The chain.
 *
 * Return: 1 if jumps to it were emitted and its target is not bound yet, else
 *   0.
 */
static int chain_pending(uint32_t chain)
{
    return chain != AOTW_CHAIN_EMPTY && (chain & AOTW_CHAIN_PENDING);
}

/**
 * cond_jump() - Conditional jump to the target of a chain.
 * @ctx: Emitter state.
 * @branch: B-type branch encoded with offset 8, taken when the jump must be.
 * @chain: The chain of the target.
 *
 * A short branch if the function is small enough, otherwise the inverted
 * branch over a jal.
 */
static void cond_jump(em_t *ctx, uint32_t branch, uint32_t *chain)
{
    if (ctx->short_br) {
        aotw_branch(ctx->w, chain, branch);
        return;
    }
    emit(ctx, rv_branch_invert(branch));
    jump(ctx, chain);
}

/**
 * br_trap() - Conditional jump to the stub raising a trap.
 * @ctx: Emitter state.
 * @branch: B-type branch encoded with offset 8.
 * @ident: The WAMR exception id.
 */
static void br_trap(em_t *ctx, uint32_t branch, unsigned ident)
{
    cond_jump(ctx, branch, &ctx->trap[ident]);
}

/**
 * call_sym() - Call a runtime symbol.
 * @ctx: Emitter state.
 * @sym: The WAMR_SYM_* symbol.
 * @tail: A tail call: through t1 and without a return address.
 *
 * Emits auipc + jalr with an R_RISCV_CALL_PLT relocation, patched by the
 * runtime.
 */
static void call_sym(em_t *ctx, unsigned sym, bool tail)
{
    const unsigned tmp = tail ? RV_T1 : RV_RA;
    const unsigned link = tail ? RV_ZERO : RV_RA;
    aotw_reloc(ctx->w, aotw_code_pos(ctx->w), R_RISCV_CALL_PLT, 0, sym);
    emit(ctx, rv_auipc(tmp, 0));
    emit(ctx, rv_jalr(link, tmp, 0));
}

/**
 * call_rt() - Call a runtime symbol, linking in ra.
 * @ctx: Emitter state.
 * @sym: The WAMR_SYM_* symbol.
 */
static void call_rt(em_t *ctx, unsigned sym)
{
    call_sym(ctx, sym, false);
}

/**
 * fwd_br() - Emit a branch whose target is bound later by bind_br().
 * @ctx: Emitter state.
 * @branch: The B-type branch, any offset.
 *
 * Return: Code offset of the branch.
 */
static uint32_t fwd_br(em_t *ctx, uint32_t branch)
{
    uint32_t site = aotw_code_pos(ctx->w);
    emit(ctx, branch);
    return site;
}

/**
 * bind_br() - Bind a forward branch to the current position.
 * @ctx: Emitter state.
 * @site: Code offset returned by fwd_br().
 *
 * Records WASMC_ERR_CODE_TOO_LARGE if the branch cannot reach.
 */
static void bind_br(em_t *ctx, uint32_t site)
{
    int32_t off = (int32_t)(aotw_code_pos(ctx->w) - site);
    if (!rv_fits_branch(off)) {
        fail(ctx, WASMC_ERR_CODE_TOO_LARGE);
        return;
    }
    aotw_patch32(ctx->w, site, rv_b_set_off(aotw_read32(ctx->w, site), off));
}

/**
 * fwd_j() - Emit a jal whose target is bound later by bind_j().
 * @ctx: Emitter state.
 *
 * Return: Code offset of the jal.
 */
static uint32_t fwd_j(em_t *ctx)
{
    return fwd_br(ctx, rv_jal(RV_ZERO, 0));
}

/**
 * bind_j() - Bind a forward jal to the current position.
 * @ctx: Emitter state.
 * @site: Code offset returned by fwd_j().
 */
static void bind_j(const em_t *ctx, uint32_t site)
{
    aotw_patch32(ctx->w, site,
                 rv_jal(RV_ZERO, (int32_t)(aotw_code_pos(ctx->w) - site)));
}

/* ------------------------------------------------------------------------ */
/* Values                                                                   */
/* ------------------------------------------------------------------------ */

#define FOR_EACH_PHI(func, blk, phi)                                           \
    for (ir_ref phi = (func)->blocks[(blk)].phi_head; (phi) != IR_NIL;         \
         (phi) = ir_a((func)->insn[(phi)]))

#define FOR_EACH_INSN(func, blk, ref)                                          \
    for (ir_ref ref = (func)->blocks[(blk)].first;                             \
         (ref) < (func)->blocks[(blk)].end;                                    \
         (ref) += (ir_ref)ir_insn_len((func)->insn[(ref)]))

/**
 * ty_of() - IR type of a value.
 * @ctx: Emitter state.
 * @ref: The value.
 *
 * Return: Its IR_T_* type.
 */
static unsigned ty_of(const em_t *ctx, ir_ref ref)
{
    return ir_ty(ctx->f->insn[ref]);
}

/**
 * const_word() - Get one word of a constant value.
 * @ctx: Emitter state.
 * @ref: The value.
 * @high: 1 for the high word, 0 for the low word.
 * @out: Set to the word if @ref is a constant.
 *
 * Return: 1 if @ref is a constant, else 0.
 */
static int const_word(em_t *ctx, ir_ref ref, bool high, uint32_t *out)
{
    ir_insn insn = ctx->f->insn[ref];
    if (ir_op(insn) == IR_OP_CONST32) {
        *out = high ? 0U : ir_const32_bits(insn);
        return 1;
    }
    if (ir_op(insn) == IR_OP_CONST64) {
        uint64_t val = ir_const64_bits(ctx->f, ref);
        *out = (uint32_t)(high ? val >> WORD_BITS : val);
        return 1;
    }
    if (high && (ctx->flags[ref] & RA_NARROW)) {
        *out = 0U; /* known zero: no location */
        return 1;
    }
    return 0;
}

/**
 * const64_of() - Get the value of a 64-bit constant.
 * @ctx: Emitter state.
 * @ref: The value, or IR_NIL.
 * @out: Set to the constant.
 *
 * Return: 1 if @ref is a CONST64, else 0.
 */
static int const64_of(const em_t *ctx, ir_ref ref, uint64_t *out)
{
    if (ref == IR_NIL || ir_op(ctx->f->insn[ref]) != IR_OP_CONST64) {
        return 0;
    }
    *out = ir_const64_bits(ctx->f, ref);
    return 1;
}

/**
 * is_narrow() - Check whether a 64-bit value has its low word only.
 * @ctx: Emitter state.
 * @ref: The value.
 *
 * A narrow value (RA_NARROW) has a high word that is known to be zero
 * (HI_ZERO, find_narrow()) or that no user reads (find_dead_hi()): it has a
 * location for its low word only, reads of its high word give x0, and its
 * high word is never computed or stored. Only HI_ZERO says the high word
 * is really zero.
 *
 * Return: 1 if @ref is narrow, else 0.
 */
static int is_narrow(const em_t *ctx, ir_ref ref)
{
    return (ctx->flags[ref] & RA_NARROW) != 0;
}

/**
 * word_loc() - Location of one word of a value, as a move operand.
 * @ctx: Emitter state.
 * @val: The value.
 * @high: 1 for the high word, 0 for the low word.
 *
 * Return: Its register, LOC_MEM | its frame offset, or SLOT_NONE if it has no
 *   location.
 */
static uint32_t word_loc(em_t *ctx, ir_ref val, bool high)
{
    if (high && is_narrow(ctx, val)) {
        return SLOT_NONE; /* its slot has the low word only */
    }
    unsigned reg = high ? ctx->reg_hi[val] : ctx->reg_lo[val];
    if (reg) {
        return reg;
    }
    if (ctx->slot_off[val] == SLOT_NONE) {
        return SLOT_NONE;
    }
    return LOC_MEM | (ctx->slot_off[val] + (high ? 4U : 0U));
}

/**
 * rd_word() - Register holding one word of a value, for reading only.
 * @ctx: Emitter state.
 * @ref: The value, or IR_NIL.
 * @high: 1 for the high word, 0 for the low word.
 * @scratch: Register to load the word into if needed.
 *
 * The caller must not write to the returned register.
 *
 * Return: The value's own register, x0 for a zero (or IR_NIL), or scratch
 *   loaded with the word.
 */
static unsigned rd_word(em_t *ctx, ir_ref ref, bool high, unsigned scratch)
{
    if (ref == IR_NIL) {
        return RV_ZERO; /* never needed at run time (dead contribution) */
    }
    uint32_t val;
    if (const_word(ctx, ref, high, &val)) {
        if (val) {
            li(ctx, scratch, val);
        }
        return val ? scratch : RV_ZERO;
    }
    uint32_t loc = word_loc(ctx, ref, high);
    if (loc == SLOT_NONE) {
        unsupported(ctx);
        return RV_ZERO;
    }
    if (!(loc & LOC_MEM)) {
        return loc;
    }
    lw_at(ctx, scratch, RV_SP, loc & ~LOC_MEM);
    return scratch;
}

/**
 * rd_lo() - Register holding the low word of a value, for reading only.
 * @ctx: Emitter state.
 * @ref: The value, or IR_NIL.
 * @scratch: Register to load the word into if needed.
 *
 * Return: See rd_word().
 */
static unsigned rd_lo(em_t *ctx, ir_ref ref, unsigned scratch)
{
    return rd_word(ctx, ref, false, scratch);
}

/**
 * rd_hi() - Register holding the high word of a value, for reading only.
 * @ctx: Emitter state.
 * @ref: The value, or IR_NIL.
 * @scratch: Register to load the word into if needed.
 *
 * Return: See rd_word().
 */
static unsigned rd_hi(em_t *ctx, ir_ref ref, unsigned scratch)
{
    return rd_word(ctx, ref, true, scratch);
}

/**
 * load_val() - Copy a value into registers.
 * @ctx: Emitter state.
 * @val: The value, or IR_NIL.
 * @reg_lo: Register for the low word.
 * @reg_hi: Register for the high word of a 64-bit value.
 *
 * The registers may then be modified freely.
 */
static void load_val(em_t *ctx, ir_ref val, unsigned reg_lo, unsigned reg_hi)
{
    mv(ctx, reg_lo, rd_lo(ctx, val, reg_lo));
    if (val == IR_NIL || is_wide(ty_of(ctx, val))) {
        mv(ctx, reg_hi, rd_hi(ctx, val, reg_hi));
    }
}

/**
 * store_val() - Store a result to the location of its value.
 * @ctx: Emitter state.
 * @ref: The value.
 * @reg_lo: Register holding the low word.
 * @reg_hi: Register holding the high word, if 64-bit.
 *
 * Nothing is stored for a value that is never used, nor for the high word of
 * a narrow value.
 */
static void store_val(em_t *ctx, ir_ref ref, unsigned reg_lo, unsigned reg_hi)
{
    int wide = is_wide(ty_of(ctx, ref)) && !is_narrow(ctx, ref);
    if (ctx->reg_lo[ref]) {
        mv(ctx, ctx->reg_lo[ref], reg_lo);
        if (wide) {
            mv(ctx, ctx->reg_hi[ref], reg_hi);
        }
        return;
    }
    if (ctx->slot_off[ref] == SLOT_NONE) {
        return; /* never used */
    }
    sw_at(ctx, reg_lo, RV_SP, ctx->slot_off[ref]);
    if (wide) {
        sw_at(ctx, reg_hi, RV_SP, ctx->slot_off[ref] + 4U);
    }
}

/**
 * wr_lo() - Register to compute the low word of a result into.
 * @ctx: Emitter state.
 * @ref: The value.
 *
 * Finish with wr_done().
 *
 * Return: The value's register, or a2.
 */
static unsigned wr_lo(em_t *ctx, ir_ref ref)
{
    return ctx->reg_lo[ref] ? ctx->reg_lo[ref] : RV_A2;
}

/**
 * wr_hi() - Register to compute the high word of a result into.
 * @ctx: Emitter state.
 * @ref: The value.
 *
 * Finish with wr_done().
 *
 * Return: The value's register, or a3.
 */
static unsigned wr_hi(em_t *ctx, ir_ref ref)
{
    return ctx->reg_hi[ref] ? ctx->reg_hi[ref] : RV_A3;
}

/**
 * wr_done() - Finish a result computed into wr_lo() and wr_hi().
 * @ctx: Emitter state.
 * @ref: The value.
 * @reg_lo: Register returned by wr_lo().
 * @reg_hi: Register returned by wr_hi() (any, if 32-bit).
 *
 * Stores it if the value lives in a stack slot.
 */
static void wr_done(em_t *ctx, ir_ref ref, unsigned reg_lo, unsigned reg_hi)
{
    if (!ctx->reg_lo[ref]) {
        store_val(ctx, ref, reg_lo, reg_hi);
    }
}

/* ------------------------------------------------------------------------ */
/* Calling convention (ILP32, soft-float): aotfmt.tex "Function Calling     */
/* Convention". Argument 0 is exec_env in a0.                               */
/* ------------------------------------------------------------------------ */

/**
 * struct place_t - Where an argument of a call is passed.
 * @reg_lo: Argument register of the low word, -1 = on the stack.
 * @reg_hi: Argument register of the high word, -1 = on the stack.
 * @stk_lo: Stack offset of the low word, when on the stack.
 * @stk_hi: Stack offset of the high word, when on the stack.
 */
typedef struct {
    int8_t reg_lo, reg_hi;
    uint16_t stk_lo, stk_hi;
} place_t;

/**
 * struct abi_cursor - Where the next argument of a call goes.
 * @reg: Next argument register; starts at a1.
 * @stack: Next stack offset; starts at 0.
 */
typedef struct {
    unsigned reg;
    uint32_t stack;
} abi_cursor;

#define ABI_CURSOR_START {RV_A1, 0}

/**
 * abi_place() - Place the next argument of a call.
 * @type: Its WASM type.
 * @next: Where it goes; advanced past it.
 *
 * ILP32 soft-float: a1..a7, then the stack; a 64-bit value takes two registers,
 * is split between a7 and the stack, or takes an 8-byte aligned stack slot.
 *
 * Return: Its registers and stack offsets.
 */
static place_t abi_place(wdec_valtype_t type, abi_cursor *next)
{
    place_t place = {-1, -1, 0, 0};
    if (!is_wide_param(type)) {
        if (next->reg <= RV_A7) {
            place.reg_lo = (int8_t)next->reg++;
        } else {
            place.stk_lo = (uint16_t)next->stack;
            next->stack += 4;
        }
    } else if (next->reg < RV_A7) {
        place.reg_lo = (int8_t)next->reg++;
        place.reg_hi = (int8_t)next->reg++;
    } else if (next->reg ==
               RV_A7) { /* split: low half in a7, high on the stack */
        place.reg_lo = (int8_t)next->reg++;
        place.stk_hi = (uint16_t)next->stack;
        next->stack += 4;
    } else { /* 2*XLEN scalars are 8-byte aligned on the stack */
        next->stack = align_up(next->stack, sizeof(uint64_t));
        place.stk_lo = (uint16_t)next->stack;
        place.stk_hi = (uint16_t)(next->stack + sizeof(uint32_t));
        next->stack += sizeof(uint64_t);
    }
    return place;
}

/**
 * place_lo() - Register for the low word of a placed argument.
 * @place: The place.
 *
 * Return: Its argument register, or t0 if on the stack.
 */
static unsigned place_lo(place_t place)
{
    return place.reg_lo >= 0 ? (unsigned)place.reg_lo : RV_T0;
}

/**
 * place_hi() - Register for the high word of a placed argument.
 * @place: The place.
 *
 * Return: Its argument register, or t1 if on the stack.
 */
static unsigned place_hi(place_t place)
{
    return place.reg_hi >= 0 ? (unsigned)place.reg_hi : RV_T1;
}

/**
 * abi_stack_bytes() - Stack bytes taken by the arguments of a call.
 * @ftype: Type of the callee.
 *
 * Return: The size of the stack arguments.
 */
static uint32_t abi_stack_bytes(const wdec_functype_t *ftype)
{
    abi_cursor next = ABI_CURSOR_START;
    for (uint32_t i = 0; i < ftype->param_count; i++) {
        abi_place(ftype->params[i], &next);
    }
    return next.stack;
}

/**
 * call_type() - Type of the callee of a call or call_indirect.
 * @ctx: Emitter state.
 * @insn: The call instruction.
 * @ftype: Set to the type.
 *
 * Return: WASMC_OK or a decoder error.
 */
static wasmc_err_t call_type(const em_t *ctx, ir_insn insn,
                             wdec_functype_t *ftype)
{
    return ir_op(insn) == IR_OP_CALL
               ? wdec_func_type(ctx->m, ir_bc32(insn), ftype)
               : wdec_get_type(ctx->m, ir_c(insn), ftype);
}

/* ------------------------------------------------------------------------ */
/* Pre-pass and frame layout                                                */
/* ------------------------------------------------------------------------ */

/**
 * clobbers_regs() - Check whether an instruction clobbers the caller-saved
 *   registers.
 * @ctx: Emitter state (ra_target.ctx).
 * @ref: Reference of the instruction.
 *
 * Used as ra_target.is_call: calls, runtime helpers, soft-float intrinsics and
 * i64 div/rem.
 *
 * Return: 1 if it does, else 0.
 */
static int clobbers_regs(void *ctx, ir_ref ref)
{
    const em_t *ectx = ctx;
    ir_insn insn = ectx->f->insn[ref];
    unsigned opcode = ir_op(insn);
    if (in_range(opcode, IR_OP_DIV_S, IR_OP_REM_U)) {
        return ty_of(ectx, ir_a(insn)) == IR_T_I64;
    }
    return op_clobbers[opcode];
}

/**
 * count_use() - Count one use of a value, saturating at 2.
 * @ctx: Emitter state.
 * @val: The value, or IR_NIL.
 */
static void count_use(em_t *ctx, ir_ref val)
{
    if (val != IR_NIL && ctx->flags[val] < 2U) {
        ctx->flags[val]++;
    }
}

/**
 * count_operand_uses() - Count the uses of the operands of an instruction.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * Includes the arguments of calls.
 */
static void count_operand_uses(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    unsigned opcode = ir_op(insn);
    unsigned mask = ir_op_vmask(opcode);
    if (mask & IR_M_A) {
        count_use(ctx, ir_a(insn));
    }
    if (mask & IR_M_B) {
        count_use(ctx, ir_b(insn));
    }
    if (mask & IR_M_C) {
        count_use(ctx, ir_c(insn));
    }
    if (opcode == IR_OP_CALL || opcode == IR_OP_CALL_INDIRECT) {
        for (unsigned idx = 0; idx < ir_a(insn); idx++) {
            count_use(ctx, ir_call_arg(ctx->f, ref, idx));
        }
    }
}

/**
 * fusable_cmp() - Check whether a comparison can become a branch condition.
 * @ctx: Emitter state.
 * @insn: The instruction word.
 *
 * Return: 1 for an i32 comparison or eqz, else 0.
 */
static int fusable_cmp(const em_t *ctx, ir_insn insn)
{
    unsigned opcode = ir_op(insn);
    return (opcode == IR_OP_EQZ || in_range(opcode, IR_OP_EQ, IR_OP_GE_U)) &&
           ty_of(ctx, ir_a(insn)) == IR_T_I32;
}

/**
 * count_uses() - Count the uses of every value and the calls.
 * @ctx: Emitter state.
 */
static void count_uses(em_t *ctx)
{
    ir_func *func = ctx->f;
    for (unsigned k = 0; k < func->nlayout; k++) {
        ir_blk blk = func->layout[k];
        FOR_EACH_PHI(func, blk, phi)
        {
            for (unsigned idx = 0; idx < func->blocks[blk].npreds; idx++) {
                count_use(ctx, ir_phi_arg(func, (ir_phi_slot){.phi = phi,
                                                              .pred = idx}));
            }
        }
        FOR_EACH_INSN(func, blk, i)
        {
            count_operand_uses(ctx, i);
            ctx->ncalls += (uint32_t)clobbers_regs(ctx, i);
        }
    }
}

/* passes of find_narrow() before it gives up (all values wide then) */
#define NARROW_MAX_PASSES 8U

/**
 * hi_zero() - Check whether the high word of an i64 operand is known zero.
 * @ctx: Emitter state.
 * @ref: The operand, or IR_NIL (a dead phi contribution: any value will do).
 *
 * Return: 1 for a constant below 2^32 or a value marked HI_ZERO, else 0
 *   (not for a value whose high word is merely unused).
 */
static int hi_zero(em_t *ctx, ir_ref ref)
{
    uint64_t val;
    if (ref == IR_NIL) {
        return 1;
    }
    if (const64_of(ctx, ref, &val)) {
        return (val >> WORD_BITS) == 0U;
    }
    return ir_op(ctx->f->insn[ref]) == IR_OP_CONST32 ||
           (ctx->flags[ref] & HI_ZERO) != 0;
}

/**
 * narrow_rule() - Whether an i64 instruction's result has a zero high word,
 *   given what is known of its operands.
 * @ctx: Emitter state.
 * @ref: The instruction, of type i64, not a phi.
 *
 * Return: 1 if it is known zero, else 0.
 */
static int narrow_rule(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    ir_ref op_a = ir_a(insn);
    ir_ref op_b = ir_b(insn);
    switch (ir_op(insn)) {
    case IR_OP_EXTEND_U:
    case IR_OP_LOAD8_U:
    case IR_OP_LOAD16_U:
    case IR_OP_LOAD32_U:
    case IR_OP_CLZ: /* 0..64 */
    case IR_OP_CTZ:
    case IR_OP_POPCNT:
        return 1;
    case IR_OP_AND:
    case IR_OP_REM_U: /* a %u b < b */
        return hi_zero(ctx, op_a) || hi_zero(ctx, op_b);
    case IR_OP_OR:
    case IR_OP_XOR:
    case IR_OP_SELECT:
        return hi_zero(ctx, op_a) && hi_zero(ctx, op_b);
    case IR_OP_DIV_U: /* a /u b <= a */
    case IR_OP_SHR_S: /* of a value < 2^32: as shr_u */
        return hi_zero(ctx, op_a);
    case IR_OP_SHR_U: {
        uint64_t amount;
        return hi_zero(ctx, op_a) || (const64_of(ctx, op_b, &amount) &&
                                      (amount & I64_SHIFT_MASK) >= WORD_BITS);
    }
    default:
        return 0;
    }
}

/**
 * set_flag() - Set or clear one of the flags of a value.
 * @ctx: Emitter state.
 * @ref: The value.
 * @flag: The flag.
 * @set: The new state.
 *
 * Return: 1 if the flag changed, else 0.
 */
static int set_flag(em_t *ctx, ir_ref ref, uint8_t flag, int set)
{
    uint8_t old = ctx->flags[ref];
    ctx->flags[ref] = set ? (uint8_t)(old | flag) : (uint8_t)(old & ~flag);
    return ctx->flags[ref] != old;
}

/* find_narrow(): every i64 phi starts narrow; returns 1 if there is one */
static int narrow_seed_phis(em_t *ctx)
{
    ir_func *func = ctx->f;
    int any = 0;
    for (unsigned k = 0; k < func->nlayout; k++) {
        FOR_EACH_PHI(func, func->layout[k], phi)
        {
            if (ir_ty(func->insn[phi]) == IR_T_I64) {
                set_flag(ctx, phi, HI_ZERO, 1);
                any = 1;
            }
        }
    }
    return any;
}

/* an i64 phi of @block is narrow if all its arguments are (or are itself) */
static int narrow_phi(em_t *ctx, const ir_block *block, ir_ref phi)
{
    for (unsigned idx = 0; idx < block->npreds; idx++) {
        ir_ref arg = ir_phi_arg(ctx->f, (ir_phi_slot){.phi = phi, .pred = idx});
        if (arg != phi && !hi_zero(ctx, arg)) {
            return 0;
        }
    }
    return 1;
}

/* one find_narrow() pass over @blk; sets *@any if an instruction is narrow.
 * Returns 1 if a flag changed. */
static int narrow_block(em_t *ctx, ir_blk blk, int *any)
{
    ir_func *func = ctx->f;
    int changed = 0;
    FOR_EACH_PHI(func, blk, phi)
    {
        if (ir_ty(func->insn[phi]) == IR_T_I64) {
            changed |= set_flag(ctx, phi, HI_ZERO,
                                narrow_phi(ctx, &func->blocks[blk], phi));
        }
    }
    FOR_EACH_INSN(func, blk, i)
    {
        ir_insn insn = func->insn[i];
        if (ir_ty(insn) == IR_T_I64 && emits_code(ir_op(insn))) {
            int narrow = narrow_rule(ctx, i);
            *any |= narrow;
            changed |= set_flag(ctx, i, HI_ZERO, narrow);
        }
    }
    return changed;
}

/**
 * find_narrow() - Mark the i64 values whose high word is known zero.
 * @ctx: Emitter state.
 *
 * The sources are zero extensions, unsigned narrow loads, bit counts and
 * constants below 2^32; and, or, xor, shr_u/shr_s, div_u/rem_u, select and
 * phis carry the property (narrow_rule()). A phi is narrow when all its
 * arguments are, which a loop phi's back-edge argument may depend on: the
 * phis start narrow (optimistic) and the passes over the function in layout
 * order clear what turns out not to be, until nothing changes. Flags only
 * ever go from set to clear, so this ends; after NARROW_MAX_PASSES passes
 * every value is made wide instead.
 *
 * The values found get HI_ZERO and RA_NARROW: one register or a 4-byte slot
 * (ra.c), their high word never computed or stored. A wrong mark would be
 * a wrong result, so each rule must be exact.
 */
static void find_narrow(em_t *ctx)
{
    ir_func *func = ctx->f;
    int any = narrow_seed_phis(ctx);
    for (unsigned pass = 0; pass < NARROW_MAX_PASSES; pass++) {
        int changed = 0;
        for (unsigned k = 0; k < func->nlayout; k++) {
            changed |= narrow_block(ctx, func->layout[k], &any);
        }
        if (!changed || !any) {
            for (uint32_t i = 0; i < ir_nslots(func); i++) {
                if (ctx->flags[i] & HI_ZERO) {
                    ctx->flags[i] |= RA_NARROW;
                }
            }
            return; /* stable, or nothing was ever narrow */
        }
    }
    for (uint32_t i = 0; i < ir_nslots(func); i++) {
        ctx->flags[i] &= (uint8_t)~HI_ZERO;
    }
}

/**
 * use_hi() - Record whether a user reads the high word of an i64 operand.
 * @ctx: Emitter state.
 * @ref: The operand, or IR_NIL.
 * @used: The user reads it.
 *
 * Return: 1 if the operand's HI_USED flag was set by this call, else 0.
 */
static int use_hi(em_t *ctx, ir_ref ref, int used)
{
    if (!used || ref == IR_NIL || ir_ty(ctx->f->insn[ref]) != IR_T_I64) {
        return 0;
    }
    return set_flag(ctx, ref, HI_USED, 1);
}

/**
 * shift_amount() - The constant amount of a shift or rotation, modulo 64.
 * @ctx: Emitter state.
 * @insn: The instruction word.
 *
 * Return: The amount, or -1 if it is not a constant.
 */
static int shift_amount(const em_t *ctx, ir_insn insn)
{
    uint64_t amount;
    return const64_of(ctx, ir_b(insn), &amount) ? (int)(amount & I64_SHIFT_MASK)
                                                : -1;
}

/**
 * hi_uses() - Mark the operands of an instruction whose high word it reads.
 * @ctx: Emitter state.
 * @ref: The instruction.
 *
 * @ref's own high word is read when it has HI_USED (an i64 result). The low
 * word of the result of and/or/xor/add/sub/mul/select depends on the low
 * words of the operands only, and the high word of an and with an operand
 * whose high word is zero is zero, whatever the other operand; shl by a
 * constant >= 32 takes no high word; shr/rotl/rotr bring high bits down unless
 * the amount is 0; a shift amount, wrap and the narrow stores read the low word
 * only. Every other user reads both words.
 *
 * Return: 1 if a flag changed, else 0.
 */
static int hi_uses(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    unsigned opcode = ir_op(insn);
    unsigned mask = ir_op_vmask(opcode);
    int own = ir_ty(insn) == IR_T_I64 && (ctx->flags[ref] & HI_USED) != 0;
    int used_a = 1;
    int used_b = 1;
    switch (opcode) {
    case IR_OP_AND: /* x & k with k < 2^32 reads no high word of x */
        used_a = own && !hi_zero(ctx, ir_b(insn));
        used_b = own && !hi_zero(ctx, ir_a(insn));
        break;
    case IR_OP_ADD:
    case IR_OP_SUB:
    case IR_OP_MUL:
    case IR_OP_OR:
    case IR_OP_XOR:
    case IR_OP_SELECT:
        used_a = used_b = own;
        break;
    case IR_OP_SHL:
    case IR_OP_SHR_S:
    case IR_OP_SHR_U:
    case IR_OP_ROTL:
    case IR_OP_ROTR: {
        int amount = shift_amount(ctx, insn);
        used_b = 0; /* the amount: 6 bits of its low word */
        if (ir_ty(insn) != IR_T_I64) {
            break;
        }
        if (opcode == IR_OP_SHL) {
            used_a = own && amount < WORD_BITS;
        } else {
            used_a = own || amount != 0;
        }
        break;
    }
    case IR_OP_WRAP:
        used_a = 0;
        break;
    case IR_OP_STORE8:
    case IR_OP_STORE16:
    case IR_OP_STORE32:
        used_b = 0; /* the value: its low bytes */
        break;
    default:
        break;
    }
    int changed = 0;
    if (mask & IR_M_A) {
        changed |= use_hi(ctx, ir_a(insn), used_a);
    }
    if (mask & IR_M_B) {
        changed |= use_hi(ctx, ir_b(insn), used_b);
    }
    if (mask & IR_M_C) {
        changed |= use_hi(ctx, ir_c(insn), 1); /* c reads its high word */
    }
    if (opcode == IR_OP_CALL || opcode == IR_OP_CALL_INDIRECT) {
        for (unsigned idx = 0; idx < ir_a(insn); idx++) {
            changed |= use_hi(ctx, ir_call_arg(ctx->f, ref, idx), 1);
        }
    }
    return changed;
}

/* the most instructions in a block of @func */
static uint32_t longest_block(const ir_func *func)
{
    uint32_t longest = 0;
    for (unsigned k = 0; k < func->nlayout; k++) {
        const ir_block *block = &func->blocks[func->layout[k]];
        uint32_t len = (uint32_t)(block->end - block->first);
        longest = len > longest ? len : longest;
    }
    return longest;
}

/* one find_dead_hi() pass over @blk, backward; @list has room for its
 * instructions. Returns 1 if a flag changed. */
static int dead_hi_block(em_t *ctx, ir_blk blk, ir_ref *list)
{
    ir_func *func = ctx->f;
    const ir_block *block = &func->blocks[blk];
    int changed = 0;
    uint32_t count = 0;
    FOR_EACH_INSN(func, blk, i)
    {
        list[count++] = i;
    }
    while (count) {
        changed |= hi_uses(ctx, list[--count]);
    }
    FOR_EACH_PHI(func, blk, phi)
    {
        int own = (ctx->flags[phi] & HI_USED) != 0;
        for (unsigned idx = 0; idx < block->npreds; idx++) {
            ir_ref arg =
                ir_phi_arg(func, (ir_phi_slot){.phi = phi, .pred = idx});
            changed |= use_hi(ctx, arg, own);
        }
    }
    return changed;
}

/**
 * find_dead_hi() - Make narrow the i64 values whose high word no user reads.
 * @ctx: Emitter state.
 *
 * Backward: hi_uses() marks the operands whose high word an instruction
 * reads, which for and/or/xor/add/... depends on whether the instruction's
 * own high word is read; a phi passes its own state to its arguments. The
 * passes run in reverse layout order, so a user is usually seen before its
 * operands; a loop phi used before its back-edge argument is defined needs
 * another pass. HI_USED only goes from clear to set, so this ends; after
 * NARROW_MAX_PASSES passes every i64 is taken as read.
 *
 * The values left without HI_USED get RA_NARROW (not HI_ZERO: their high
 * word is not zero, only never read): Sightglass's keccak rotates and then
 * masks to 32 bits, and the rotations need no high word.
 *
 * Instructions have variable length, so each block's instruction list is
 * collected in a temporary scratch array to be walked backward.
 *
 * Return: WASMC_OK or the scratch allocation error.
 */
static wasmc_err_t find_dead_hi(em_t *ctx)
{
    ir_func *func = ctx->f;
    uint32_t mark = ir_scratch_mark(func);
    ir_ref *list =
        ir_scratch_alloc(func, IR_ARRAY(ir_ref, longest_block(func) + 1U));
    if (!list) {
        return ir_scratch_err(func);
    }
    for (unsigned pass = 0;; pass++) {
        if (pass == NARROW_MAX_PASSES) {
            ir_scratch_release(func, mark);
            return WASMC_OK; /* no RA_NARROW added */
        }
        int changed = 0;
        for (unsigned k = func->nlayout; k-- > 0;) {
            changed |= dead_hi_block(ctx, func->layout[k], list);
        }
        if (!changed) {
            break;
        }
    }
    ir_scratch_release(func, mark);
    for (uint32_t i = 0; i < ir_nslots(func); i++) {
        if (!(ctx->flags[i] & HI_USED) && ir_ty(func->insn[i]) == IR_T_I64) {
            ctx->flags[i] |= RA_NARROW;
        }
    }
    return WASMC_OK;
}

/**
 * phi_words() - Words copied into the phis of a block on each edge.
 * @func: The function.
 * @blk: The block.
 *
 * Return: The number of words.
 */
static uint32_t phi_words(const ir_func *func, ir_blk blk)
{
    uint32_t words = 0;
    FOR_EACH_PHI(func, blk, phi)
    {
        words += is_wide(ir_ty(func->insn[phi])) ? 2U : 1U;
    }
    return words;
}

/**
 * fold_into_br_if() - Fuse a comparison into the br_if that uses it.
 * @ctx: Emitter state.
 * @insn: A br_if.
 * @last: The last instruction before it that emits code, or IR_NIL.
 *
 * The comparison is folded when it is that instruction, the br_if is its
 * only use and the comparison fits a branch.
 */
static void fold_into_br_if(em_t *ctx, ir_insn insn, ir_ref last)
{
    if (last != IR_NIL && ir_a(insn) == last && (ctx->flags[last] & 3U) == 1U &&
        fusable_cmp(ctx, ctx->f->insn[last])) {
        ctx->flags[last] |= RA_FOLDED;
    }
}

/**
 * scan_block() - Pre-pass over the instructions of a block.
 * @ctx: Emitter state.
 * @blk: The block.
 * @out_args: Largest stack argument area of a call so far; updated.
 *
 * Return: WASMC_OK or a decoder error.
 */
static wasmc_err_t scan_block(em_t *ctx, ir_blk blk, uint32_t *out_args)
{
    ir_func *func = ctx->f;
    ir_ref last = IR_NIL; /* last instruction that emits code */
    FOR_EACH_INSN(func, blk, i)
    {
        ir_insn insn = func->insn[i];
        unsigned opcode = ir_op(insn);
        if (opcode == IR_OP_BR_IF) {
            fold_into_br_if(ctx, insn, last);
        }
        if (emits_code(opcode)) {
            last = i;
        }
        if (in_range(opcode, IR_OP_LOAD, IR_OP_MEM_GROW)) {
            ctx->uses_mem = 1; /* loads, stores, memory.size/grow */
        }
        if (opcode == IR_OP_CALL || opcode == IR_OP_CALL_INDIRECT) {
            wdec_functype_t ftype;
            wasmc_err_t err = call_type(ctx, insn, &ftype);
            if (err) {
                return err;
            }
            uint32_t start = abi_stack_bytes(&ftype);
            if (start > *out_args) {
                *out_args = start;
            }
        }
    }
    return WASMC_OK;
}

/**
 * scan_func() - Pre-pass over the function.
 * @ctx: Emitter state.
 *
 * Computes the use counts, the narrow i64 values, the memory use, the
 * outgoing argument area and the largest phi move set, and marks with RA_FOLDED
 * the comparisons fused into the br_if right after them (only constants in
 * between: their operands are still in place).
 *
 * Return: WASMC_OK or a decoder error.
 */
static wasmc_err_t scan_func(em_t *ctx)
{
    ir_func *func = ctx->f;
    memset(ctx->flags, 0, ir_nslots(func));
    count_uses(ctx);
    find_narrow(ctx); /* after count_uses: RA_NARROW is above the counts */
    wasmc_err_t err = find_dead_hi(ctx);
    if (err) {
        return err;
    }

    uint32_t out_args = 0;
    for (unsigned k = 0; k < func->nlayout; k++) {
        ir_blk blk = func->layout[k];
        uint32_t words = phi_words(func, blk);
        if (words > ctx->max_moves) {
            ctx->max_moves = words;
        }
        err = scan_block(ctx, blk, &out_args);
        if (err) {
            return err;
        }
    }
    ctx->out_args = align_up(out_args, sizeof(uint64_t));
    return WASMC_OK;
}

/* s3/s4 come last: they are pinned (not allocatable) when the function
 * uses the memory. */
static const uint8_t callee_regs[] = {RV_S5,  RV_S6,  RV_S7, RV_S8, RV_S9,
                                      RV_S10, RV_S11, RV_S0, RV_S3, RV_S4};
static const uint8_t caller_regs[] = {RV_A6, RV_A7};

/**
 * layout_frame() - Allocate the registers and lay out the frame.
 * @ctx: Emitter state.
 *
 * Runs ra.c, then places the outgoing arguments, the spill slots, the saved
 * registers and ra.
 *
 * Return: WASMC_OK or the error of ra_run().
 */
static wasmc_err_t layout_frame(em_t *ctx)
{
    ir_func *func = ctx->f;
    ra_target target = {
        .callee = callee_regs,
        .ncallee =
            ARRAY_LEN(callee_regs) - (ctx->uses_mem ? PINNED_MEM_REGS : 0),
        .caller = caller_regs,
        .ncaller = ARRAY_LEN(caller_regs),
        .is_call = clobbers_regs,
        .ctx = ctx,
        .flags = ctx->flags,
    };
    ra_result res;
    wasmc_err_t err = ra_run(func, &target, &res);
    if (err) {
        return err;
    }
    ctx->reg_lo = res.reg_lo;
    ctx->reg_hi = res.reg_hi;
    ctx->slot_off = res.slot_off;
    for (uint32_t i = 0; i < ir_nslots(func); i++) {
        if (ctx->slot_off[i] != SLOT_NONE) {
            ctx->slot_off[i] += ctx->out_args;
        }
    }

    ctx->nsaved = 0;
    ctx->saved[ctx->nsaved++] = RV_S1;
    ctx->saved[ctx->nsaved++] = RV_S2;
    if (ctx->uses_mem) {
        ctx->saved[ctx->nsaved++] = RV_S3;
        ctx->saved[ctx->nsaved++] = RV_S4;
    }
    for (unsigned k = 0; k < target.ncallee; k++) {
        if (res.used_regs & (1U << callee_regs[k])) {
            ctx->saved[ctx->nsaved++] = callee_regs[k];
        }
    }
    ctx->frame = align_up(
        ctx->out_args + res.spill_bytes + 4U + (4U * ctx->nsaved), STACK_ALIGN);
    return WASMC_OK;
}

/* ------------------------------------------------------------------------ */
/* Prologue / epilogue                                                      */
/* ------------------------------------------------------------------------ */

/**
 * load_mem_regs() - Load the memory base (s3) and bound (s4).
 * @ctx: Emitter state.
 */
static void load_mem_regs(em_t *ctx)
{
    lw_at(ctx, RV_S3, RV_S2, WAMR_INST_MEM_DATA);
    lw_at(ctx, RV_S4, RV_S2, WAMR_INST_MEM_BOUND_CHECK_1BYTE);
}

/**
 * reload_mem() - Reload the memory base and bound after a call.
 * @ctx: Emitter state.
 *
 * Only if the function uses the memory and the module can grow it: then they
 * may have moved.
 */
static void reload_mem(em_t *ctx)
{
    if (ctx->uses_mem && ctx->w->has_mem_grow) {
        load_mem_regs(ctx);
    }
}

/**
 * frame_regs() - Save or restore ra and the callee-saved registers.
 * @ctx: Emitter state.
 * @encode: rv_sw to save, rv_lw to restore.
 */
static void frame_regs(em_t *ctx, itype_fn encode)
{
    mem_word(ctx, encode, RV_RA, RV_SP, ctx->frame - 4U);
    for (unsigned k = 0; k < ctx->nsaved; k++) {
        /* below ra, one word each */
        mem_word(ctx, encode, ctx->saved[k], RV_SP,
                 ctx->frame - (4U * (k + 2U)));
    }
}

/**
 * prologue() - Emit the function prologue.
 * @ctx: Emitter state.
 *
 * The native stack check first (aotfmt.tex "Native stack overflow"): if the new
 * sp would go below exec_env->native_stack_boundary, raise trap 11 with a tail
 * call, before any frame exists. Then the frame, the saved registers and the
 * pinned registers.
 */
static void prologue(em_t *ctx)
{
    /* native stack check (aotfmt.tex "Native stack overflow"): the new sp
     * must not go below exec_env->native_stack_boundary. On overflow raise
     * trap 11 with a tail call (no frame yet): the caller sees the
     * exception after the call returns. */
    emit(ctx, rv_lw(RV_T0, RV_A0, WAMR_EXEC_ENV_NATIVE_STACK_BOUNDARY));
    sub_k(ctx, RV_T1, RV_SP, ctx->frame, RV_T1);
    emit(ctx, rv_bgeu(RV_T1, RV_T0, rv_skip(4)));
    emit(ctx, rv_lw(RV_A0, RV_A0, WAMR_EXEC_ENV_MODULE_INST));
    emit(ctx, rv_addi(RV_A1, RV_ZERO, WAMR_EXCE_NATIVE_STACK_OVERFLOW));
    call_sym(ctx, WAMR_SYM_SET_EXCEPTION_WITH_ID, true);

    sub_k(ctx, RV_SP, RV_SP, ctx->frame, RV_T0);
    frame_regs(ctx, rv_sw);
    emit(ctx, rv_mv(RV_S1, RV_A0));
    emit(ctx, rv_lw(RV_S2, RV_S1, WAMR_EXEC_ENV_MODULE_INST));
    if (ctx->uses_mem) {
        load_mem_regs(ctx);
    }
}

/**
 * epilogue() - Emit the function epilogue: restore, pop, return.
 * @ctx: Emitter state.
 */
static void epilogue(em_t *ctx)
{
    frame_regs(ctx, rv_lw);
    add_k(ctx, RV_SP, RV_SP, ctx->frame, RV_T0);
    emit(ctx, rv_ret());
}

/* ------------------------------------------------------------------------ */
/* Phi copies on an edge                                                    */
/* ------------------------------------------------------------------------ */

/**
 * struct cfg_edge - A control-flow edge.
 * @from: The predecessor block.
 * @to: The successor block.
 */
typedef struct {
    ir_blk from;
    ir_blk to;
} cfg_edge;

/**
 * pred_index() - Index of an edge among the predecessors of its target.
 * @ctx: Emitter state.
 * @edge: The edge.
 *
 * Return: The index, the phi argument of that edge.
 */
static unsigned pred_index(const em_t *ctx, cfg_edge edge)
{
    unsigned idx = 0;
    uint16_t pred;
    IR_FOR_EACH_PRED(ctx->f, edge.to, pred)
    {
        if (ctx->f->edges[pred].from == edge.from) {
            return idx;
        }
        idx++;
    }
    return 0;
}

/**
 * emit_move() - Emit one move.
 * @ctx: Emitter state.
 * @move: The move (its done flag is not used).
 *
 * Memory to memory and constant to memory go through t1.
 */
static void emit_move(em_t *ctx, const pmove_t *move)
{
    const uint32_t dst = move->dst;
    const uint32_t src = move->src;
    int to_mem = (dst & LOC_MEM) != 0;
    unsigned reg = to_mem ? RV_T1 : dst;
    if (move->src_const) {
        li(ctx, reg, src);
    } else if (src & LOC_MEM) {
        lw_at(ctx, reg, RV_SP, src & ~LOC_MEM);
    } else if (to_mem) {
        reg = src;
    } else {
        emit(ctx, rv_mv(reg, src));
        return;
    }
    if (to_mem) {
        sw_at(ctx, reg, RV_SP, dst & ~LOC_MEM);
    }
}

/**
 * edge_moves() - Collect the phi moves of an edge.
 * @ctx: Emitter state.
 * @edge: The edge.
 *
 * Fills ctx->moves with one move per word that changes location.
 *
 * Return: The number of moves, 0 on error.
 */
static uint32_t edge_moves(em_t *ctx, cfg_edge edge)
{
    const ir_func *func = ctx->f;
    unsigned pred = pred_index(ctx, edge);
    uint32_t count = 0;
    FOR_EACH_PHI(func, edge.to, phi)
    {
        ir_ref val = ir_phi_arg(func, (ir_phi_slot){.phi = phi, .pred = pred});
        if (val == phi || val == IR_NIL) {
            continue;
        }
        for (int hi = 0; hi <= is_wide(ir_ty(func->insn[phi])); hi++) {
            pmove_t move = {word_loc(ctx, phi, hi), 0, 0, 0};
            if (move.dst == SLOT_NONE) {
                continue;
            }
            if (const_word(ctx, val, hi, &move.src)) {
                move.src_const = 1;
            } else {
                move.src = word_loc(ctx, val, hi);
                if (move.src == SLOT_NONE) {
                    unsupported(ctx);
                    return 0;
                }
                if (move.src == move.dst) {
                    continue;
                }
            }
            if (count >= ctx->max_moves) {
                fail(ctx, WASMC_ERR_IR_STATE);
                return 0;
            }
            ctx->moves[count++] = move;
        }
    }
    return count;
}

/**
 * dst_pending_read() - Check whether a move's destination is still to be read.
 * @moves: The moves.
 * @n: Their number.
 * @move: One of @moves.
 *
 * Return: 1 if a pending move other than @move reads the destination of
 *   @move, else 0.
 */
static int dst_pending_read(const pmove_t *moves, uint32_t n,
                            const pmove_t *move)
{
    for (uint32_t i = 0; i < n; i++) {
        const pmove_t *other = &moves[i];
        if (other != move && !other->done && !other->src_const &&
            other->src == move->dst) {
            return 1;
        }
    }
    return 0;
}

/**
 * parallel_copy() - Emit the collected moves as a parallel copy.
 * @ctx: Emitter state.
 * @n: Number of moves in c->moves.
 *
 * A move is emitted once no pending move still reads its destination; a cycle
 * is broken by saving one destination in t0 and letting its readers read t0.
 *
 * Each round emits at least one move or breaks a cycle. Each round scans the n
 * moves, and pending_read() scans them again, so the worst case is O(n^3). In
 * practice n is the number of phi words on one edge, usually just a few, so
 * this simple form is fine. The linear-time versions (Boissinot et al. 2009,
 * "Revisiting Out-of-SSA Translation", or the version formally verified in
 * CompCert: Rideau, Serpette and Leroy, "Tilting at windmills with Coq") keep
 * a worklist of ready moves instead of rescanning. They produce the same kind
 * of move sequence.
 */
static void parallel_copy(em_t *ctx, uint32_t n)
{
    pmove_t *moves = ctx->moves;
    for (uint32_t left = n; left;) {
        int progress = 0;
        for (uint32_t idx = 0; idx < n; idx++) {
            if (moves[idx].done || dst_pending_read(moves, n, &moves[idx])) {
                continue;
            }
            emit_move(ctx, &moves[idx]);
            moves[idx].done = 1;
            left--;
            progress = 1;
        }
        if (progress || !left) {
            continue;
        }
        uint32_t idx = 0;
        while (moves[idx].done) {
            idx++;
        }
        uint32_t dst = moves[idx].dst;
        emit_move(ctx, &(pmove_t){.dst = RV_T0, .src = dst});
        for (uint32_t i = 0; i < n; i++) {
            if (!moves[i].done && !moves[i].src_const && moves[i].src == dst) {
                moves[i].src = RV_T0;
            }
        }
    }
}

/**
 * edge_copies() - Emit the phi copies of an edge.
 * @ctx: Emitter state.
 * @edge: The edge.
 */
static void edge_copies(em_t *ctx, cfg_edge edge)
{
    if (ctx->f->blocks[edge.to].phi_head != IR_NIL) {
        parallel_copy(ctx, edge_moves(ctx, edge));
    }
}

/**
 * edge_is_nop() - Check whether an edge needs no copy at all.
 * @ctx: Emitter state.
 * @edge: The edge.
 *
 * Return: 1 if every phi of @edge.to already holds the location of its
 * argument, else 0.
 */
static int edge_is_nop(em_t *ctx, cfg_edge edge)
{
    const ir_func *func = ctx->f;
    unsigned pred = pred_index(ctx, edge);
    FOR_EACH_PHI(func, edge.to, phi)
    {
        ir_ref val = ir_phi_arg(func, (ir_phi_slot){.phi = phi, .pred = pred});
        if (val == phi || val == IR_NIL) {
            continue;
        }
        for (int hi = 0; hi <= is_wide(ir_ty(func->insn[phi])); hi++) {
            uint32_t phi_loc = word_loc(ctx, phi, hi);
            uint32_t word;
            if (phi_loc != SLOT_NONE && (const_word(ctx, val, hi, &word) ||
                                         word_loc(ctx, val, hi) != phi_loc)) {
                return 0;
            }
        }
    }
    return 1;
}

/**
 * needs_copies() - Check whether an edge needs code for phi copies.
 * @ctx: Emitter state.
 * @edge: The edge.
 *
 * Return: 1 if it does, else 0.
 */
static int needs_copies(em_t *ctx, cfg_edge edge)
{
    return ctx->f->blocks[edge.to].phi_head != IR_NIL &&
           !edge_is_nop(ctx, edge);
}

/* ------------------------------------------------------------------------ */
/* i32 bit counting: operand in t0, result in a2                            */
/* ------------------------------------------------------------------------ */

/**
 * popcnt32() - i32 popcnt: a2 = popcnt(t0).
 * @ctx: Emitter state.
 *
 * SWAR; uses t1 and t3.
 */
static void popcnt32(em_t *ctx)
{
    const uint32_t every_2nd_bit = 0x55555555U;
    const uint32_t low_2_of_4 = 0x33333333U;
    const uint32_t low_nibbles = 0x0F0F0F0FU;
    const uint32_t byte_ones =
        0x01010101U; /* sums the bytes into the top one */
    const unsigned top_byte = WORD_BITS - 8U;

    emit(ctx, rv_srli(RV_T1, RV_T0, 1));
    li(ctx, RV_T3, every_2nd_bit);
    emit(ctx, rv_and(RV_T1, RV_T1, RV_T3));
    emit(ctx, rv_sub(RV_T0, RV_T0, RV_T1)); /* 2-bit counts */
    li(ctx, RV_T3, low_2_of_4);
    emit(ctx, rv_and(RV_T1, RV_T0, RV_T3));
    emit(ctx, rv_srli(RV_T0, RV_T0, 2));
    emit(ctx, rv_and(RV_T0, RV_T0, RV_T3));
    emit(ctx, rv_add(RV_T0, RV_T0, RV_T1)); /* 4-bit counts */
    emit(ctx, rv_srli(RV_T1, RV_T0, 4));
    emit(ctx, rv_add(RV_T0, RV_T0, RV_T1));
    li(ctx, RV_T3, low_nibbles);
    emit(ctx, rv_and(RV_T0, RV_T0, RV_T3)); /* byte counts */
    li(ctx, RV_T3, byte_ones);
    emit(ctx, rv_mul(RV_T0, RV_T0, RV_T3));
    emit(ctx, rv_srli(RV_A2, RV_T0, top_byte));
}

/**
 * clz32() - i32 clz: a2 = clz(t0).
 * @ctx: Emitter state.
 *
 * Branchless binary search; uses t1, t2 and t3.
 */
static void clz32(em_t *ctx)
{
    static const struct {
        uint32_t limit;
        unsigned log;
    } step[] = {
        {0x00010000U, 4}, {0x01000000U, 3}, {0x10000000U, 2},
        {0x40000000U, 1}, {0x80000000U, 0},
    };
    emit(ctx, rv_mv(RV_T1, RV_T0));
    emit(ctx, rv_mv(RV_A2, RV_ZERO));
    for (unsigned i = 0; i < ARRAY_LEN(step); i++) {
        li(ctx, RV_T3, step[i].limit);
        emit(ctx, rv_sltu(RV_T2, RV_T1, RV_T3));
        if (step[i].log) {
            emit(ctx, rv_slli(RV_T2, RV_T2, step[i].log));
        }
        emit(ctx, rv_sll(RV_T1, RV_T1, RV_T2));
        emit(ctx, rv_add(RV_A2, RV_A2, RV_T2));
    }
    emit(ctx, rv_seqz(RV_T2, RV_T1)); /* x == 0: 31 + 1 */
    emit(ctx, rv_add(RV_A2, RV_A2, RV_T2));
}

/**
 * ctz32() - i32 ctz: a2 = ctz(t0).
 * @ctx: Emitter state.
 *
 * popcnt((x & -x) - 1), which is 32 for x == 0; uses t1 and t3.
 */
static void ctz32(em_t *ctx)
{
    emit(ctx, rv_neg(RV_T1, RV_T0));
    emit(ctx, rv_and(RV_T1, RV_T0, RV_T1));
    emit(ctx, rv_addi(RV_T0, RV_T1, -1));
    popcnt32(ctx);
}

/* ------------------------------------------------------------------------ */
/* i64 (register pairs): a = t0:t1, b = t2:t3, result a2:a3                 */
/* ------------------------------------------------------------------------ */

/**
 * shift64() - i64 shift: a2:a3 = t0:t1 shifted by t2 & 63.
 * @ctx: Emitter state.
 * @opcode: IR_OP_SHL, IR_OP_SHR_U or IR_OP_SHR_S.
 *
 * For n < 32 the bits crossing the halves are shifted in two steps, so that n
 * == 0 works. Uses t4, t5 and t6.
 */
static void shift64(em_t *ctx, unsigned opcode)
{
    emit(ctx, rv_andi(RV_T4, RV_T2, I64_SHIFT_MASK));
    emit(ctx, rv_addi(RV_T5, RV_T4, -WORD_BITS));
    uint32_t small = fwd_br(ctx, rv_blt(RV_T5, RV_ZERO, 0));
    if (opcode == IR_OP_SHL) { /* n >= 32 */
        emit(ctx, rv_sll(RV_A3, RV_T0, RV_T5));
        emit(ctx, rv_mv(RV_A2, RV_ZERO));
    } else if (opcode == IR_OP_SHR_U) {
        emit(ctx, rv_srl(RV_A2, RV_T1, RV_T5));
        emit(ctx, rv_mv(RV_A3, RV_ZERO));
    } else {
        emit(ctx, rv_sra(RV_A2, RV_T1, RV_T5));
        emit(ctx, rv_srai(RV_A3, RV_T1, SIGN_SHIFT));
    }
    uint32_t done = fwd_j(ctx);

    /* n < 32: the bits crossing the halves are shifted in two steps
     * ((x >> 1) >> (31 - n)), so that n == 0 works */
    bind_br(ctx, small);
    emit(ctx, rv_xori(RV_T5, RV_T4, I32_SHIFT_MASK));
    if (opcode == IR_OP_SHL) {
        emit(ctx, rv_sll(RV_A3, RV_T1, RV_T4));
        emit(ctx, rv_sll(RV_A2, RV_T0, RV_T4));
        emit(ctx, rv_srli(RV_T6, RV_T0, 1));
        emit(ctx, rv_srl(RV_T6, RV_T6, RV_T5));
        emit(ctx, rv_or(RV_A3, RV_A3, RV_T6));
    } else {
        emit(ctx, rv_srl(RV_A2, RV_T0, RV_T4));
        emit(ctx, opcode == IR_OP_SHR_U ? rv_srl(RV_A3, RV_T1, RV_T4)
                                        : rv_sra(RV_A3, RV_T1, RV_T4));
        emit(ctx, rv_slli(RV_T6, RV_T1, 1));
        emit(ctx, rv_sll(RV_T6, RV_T6, RV_T5));
        emit(ctx, rv_or(RV_A2, RV_A2, RV_T6));
    }
    bind_j(ctx, done);
}

/**
 * rot64() - i64 rotation: a2:a3 = t0:t1 rotated by t2 & 63.
 * @ctx: Emitter state.
 * @opcode: IR_OP_ROTL or IR_OP_ROTR.
 *
 * rotl is (x << n) | (x >>u (-n & 63)), rotr its mirror. Uses a4 and a5, and
 * negates t2.
 */
static void rot64(em_t *ctx, unsigned opcode)
{
    int left = opcode == IR_OP_ROTL;
    shift64(ctx, left ? IR_OP_SHL : IR_OP_SHR_U);
    emit(ctx, rv_mv(RV_A4, RV_A2));
    emit(ctx, rv_mv(RV_A5, RV_A3));
    emit(ctx, rv_neg(RV_T2, RV_T2));
    shift64(ctx, left ? IR_OP_SHR_U : IR_OP_SHL);
    emit(ctx, rv_or(RV_A2, RV_A2, RV_A4));
    emit(ctx, rv_or(RV_A3, RV_A3, RV_A5));
}

/**
 * struct reg_op - The registers of a one-operand operation: dst = op(src).
 * @dst: Destination register.
 * @src: Operand register.
 */
typedef struct {
    unsigned dst, src;
} reg_op;

/**
 * struct reg_pair - The registers of an i64.
 * @lo: Low word.
 * @hi: High word.
 */
typedef struct {
    unsigned lo, hi;
} reg_pair;

/**
 * lt64() - i64 less-than: @dst = (@first < @second).
 * @ctx: Emitter state.
 * @dst: Destination register.
 * @first: The left operand.
 * @second: The right operand.
 * @is_signed: Signed comparison.
 *
 * Uses t4, t5 and t6.
 */
static void lt64(em_t *ctx, unsigned dst, reg_pair first, reg_pair second,
                 bool is_signed)
{
    emit(ctx, is_signed ? rv_slt(RV_T4, first.hi, second.hi)
                        : rv_sltu(RV_T4, first.hi, second.hi));
    emit(ctx, rv_xor(RV_T5, first.hi, second.hi));
    emit(ctx, rv_seqz(RV_T5, RV_T5));
    emit(ctx, rv_sltu(RV_T6, first.lo, second.lo));
    emit(ctx, rv_and(RV_T5, RV_T5, RV_T6));
    emit(ctx, rv_or(dst, RV_T4, RV_T5));
}

/**
 * typedef shift_fn - Encoder of a shift by an immediate amount.
 * @dst: Destination register.
 * @src: Source register.
 * @shamt: The amount, 0-31.
 *
 * Return: The encoded instruction.
 */
typedef uint32_t (*shift_fn)(unsigned dst, unsigned src, unsigned shamt);

/**
 * shift_word() - @dst = @src shifted by @shamt, a copy when @shamt is 0.
 * @ctx: Emitter state.
 * @encode: rv_slli, rv_srli or rv_srai.
 * @dst: Destination register.
 * @src: Source register.
 * @shamt: The amount, 0-31.
 */
static void shift_word(em_t *ctx, shift_fn encode, unsigned dst, unsigned src,
                       unsigned shamt)
{
    if (shamt) {
        emit(ctx, encode(dst, src, shamt));
    } else {
        mv(ctx, dst, src);
    }
}

/**
 * funnel() - One word of a 64-bit shift: @dst = (@src op @shamt) | the bits
 *   of @other shifted in from the other side.
 * @ctx: Emitter state.
 * @left: Shift left (bits of @other come from its top), else right
 *   (logical; bits of @other come from its bottom).
 * @dst: Destination register, not @src or @other.
 * @src: The word shifted.
 * @other: The word whose bits are shifted in.
 * @shamt: The amount, 1-31.
 *
 * Uses t4.
 */
static void funnel(em_t *ctx, bool left, unsigned dst, unsigned src,
                   unsigned other, unsigned shamt)
{
    emit(ctx, left ? rv_slli(dst, src, shamt) : rv_srli(dst, src, shamt));
    emit(ctx, left ? rv_srli(RV_T4, other, WORD_BITS - shamt)
                   : rv_slli(RV_T4, other, WORD_BITS - shamt));
    emit(ctx, rv_or(dst, dst, RV_T4));
}

/* @dst = @src; the high word only if @want_hi */
static void copy64(em_t *ctx, reg_pair dst, reg_pair src, bool want_hi)
{
    mv(ctx, dst.lo, src.lo);
    if (want_hi) {
        mv(ctx, dst.hi, src.hi);
    }
}

/* shift64_imm() of a rotl by @shamt, 0-63 */
static void rotl64_imm(em_t *ctx, reg_pair dst, reg_pair src, unsigned shamt,
                       bool want_hi)
{
    if (shamt >= WORD_BITS) { /* rotl 32 swaps the words */
        src = (reg_pair){src.hi, src.lo};
        shamt -= WORD_BITS;
    }
    if (!shamt) {
        copy64(ctx, dst, src, want_hi);
        return;
    }
    if (want_hi) {
        funnel(ctx, true, dst.hi, src.hi, src.lo, shamt);
    }
    funnel(ctx, true, dst.lo, src.lo, src.hi, shamt);
}

/* shift64_imm() of a shl by @shamt, 1-63 */
static void shl64_imm(em_t *ctx, reg_pair dst, reg_pair src, unsigned shamt,
                      bool want_hi)
{
    if (shamt >= WORD_BITS) {
        if (want_hi) {
            shift_word(ctx, rv_slli, dst.hi, src.lo, shamt - WORD_BITS);
        }
        mv(ctx, dst.lo, RV_ZERO);
        return;
    }
    if (want_hi) {
        funnel(ctx, true, dst.hi, src.hi, src.lo, shamt);
    }
    emit(ctx, rv_slli(dst.lo, src.lo, shamt));
}

/* shift64_imm() of a shr_s (@sign) or shr_u by @shamt, 1-63 */
static void shr64_imm(em_t *ctx, bool sign, reg_pair dst, reg_pair src,
                      unsigned shamt, bool want_hi)
{
    shift_fn shr = sign ? rv_srai : rv_srli;
    if (shamt >= WORD_BITS) {
        shift_word(ctx, shr, dst.lo, src.hi, shamt - WORD_BITS);
        if (want_hi && sign) {
            emit(ctx, rv_srai(dst.hi, src.hi, SIGN_SHIFT));
        } else if (want_hi) {
            mv(ctx, dst.hi, RV_ZERO);
        }
        return;
    }
    funnel(ctx, false, dst.lo, src.lo, src.hi, shamt);
    if (want_hi) {
        emit(ctx, shr(dst.hi, src.hi, shamt));
    }
}

/**
 * shift64_imm() - i64 shift or rotation by a constant amount.
 * @ctx: Emitter state.
 * @opcode: IR_OP_SHL, _SHR_S, _SHR_U, _ROTL or _ROTR.
 * @dst: Result registers, neither of them a register of @src.
 * @src: Operand registers.
 * @amount: The amount (taken modulo 64, as WASM does).
 * @want_hi: Compute the high word (0 when it is known to be zero).
 *
 * At most 6 instructions, no branch: a known amount picks the sequence at
 * compile time where shift64() tests it at run time. Uses t4.
 */
static void shift64_imm(em_t *ctx, unsigned opcode, reg_pair dst, reg_pair src,
                        unsigned amount, bool want_hi)
{
    unsigned shamt = amount & I64_SHIFT_MASK;
    if (opcode == IR_OP_ROTR) { /* rotr n = rotl (64 - n) */
        opcode = IR_OP_ROTL;
        shamt = ((2U * WORD_BITS) - shamt) & I64_SHIFT_MASK;
    }
    if (opcode == IR_OP_ROTL) {
        rotl64_imm(ctx, dst, src, shamt, want_hi);
    } else if (!shamt) {
        copy64(ctx, dst, src, want_hi);
    } else if (opcode == IR_OP_SHL) {
        shl64_imm(ctx, dst, src, shamt, want_hi);
    } else {
        shr64_imm(ctx, opcode == IR_OP_SHR_S, dst, src, shamt, want_hi);
    }
}

/**
 * logic_word() - One word of an i64 and/or/xor with a constant.
 * @ctx: Emitter state.
 * @opcode: IR_OP_AND, _OR or _XOR.
 * @dst: Destination register.
 * @src: Operand register.
 * @word: The constant's word.
 * @tmp: Register for a constant that fits no immediate.
 *
 * All-zero and all-one words become a copy, a zero or a constant; 12-bit
 * ones an immediate form.
 */
static void logic_word(em_t *ctx, unsigned opcode, unsigned dst, unsigned src,
                       uint32_t word, unsigned tmp)
{
    if (word == 0U) { /* x & 0 = 0, x | 0 = x ^ 0 = x */
        mv(ctx, dst, opcode == IR_OP_AND ? RV_ZERO : src);
        return;
    }
    if (word == UINT32_MAX && opcode != IR_OP_XOR) { /* x & ~0, x | ~0 */
        if (opcode == IR_OP_AND) {
            mv(ctx, dst, src);
        } else {
            emit(ctx, rv_addi(dst, RV_ZERO, -1));
        }
        return;
    }
    if (rv_fits12((int32_t)word)) {
        emit(ctx, alu_ri[opcode](dst, src, (int32_t)word));
        return;
    }
    li(ctx, tmp, word);
    emit(ctx, alu_rr[opcode](dst, src, tmp));
}

/**
 * add_word() - @dst = @src + @word.
 * @ctx: Emitter state.
 * @dst: Destination register.
 * @src: Operand register.
 * @word: The constant.
 * @tmp: Register for a constant that fits no immediate.
 */
static void add_word(em_t *ctx, unsigned dst, unsigned src, uint32_t word,
                     unsigned tmp)
{
    if (!word) {
        mv(ctx, dst, src);
    } else if (rv_fits12((int32_t)word)) {
        emit(ctx, rv_addi(dst, src, (int32_t)word));
    } else {
        li(ctx, tmp, word);
        emit(ctx, rv_add(dst, src, tmp));
    }
}

/* @dst = @src + @cval; the high word only if @want_hi. Uses t2-t4. */
static void add64_imm(em_t *ctx, reg_pair dst, reg_pair src, uint64_t cval,
                      bool want_hi)
{
    uint32_t low = (uint32_t)cval;
    uint32_t high = (uint32_t)(cval >> WORD_BITS);
    add_word(ctx, dst.lo, src.lo, low, RV_T2);
    if (!want_hi) {
        return;
    }
    if (!low) {
        add_word(ctx, dst.hi, src.hi, high, RV_T3);
        return;
    }
    emit(ctx, rv_sltu(RV_T4, dst.lo, src.lo)); /* carry */
    if (high) {
        add_word(ctx, dst.hi, src.hi, high, RV_T3);
        emit(ctx, rv_add(dst.hi, dst.hi, RV_T4));
    } else {
        emit(ctx, rv_add(dst.hi, src.hi, RV_T4));
    }
}

/* emit_i64_imm() of a shift or rotation: 1 if emitted (a constant amount) */
static int emit_shift64_imm(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    ir_ref op_a = ir_a(insn);
    uint64_t cval;
    if (!const64_of(ctx, ir_b(insn), &cval)) {
        return 0;
    }
    reg_pair src = {rd_lo(ctx, op_a, RV_T0), rd_hi(ctx, op_a, RV_T1)};
    reg_pair dst = {wr_lo(ctx, ref), wr_hi(ctx, ref)};
    shift64_imm(ctx, ir_op(insn), dst, src, (unsigned)cval,
                !is_narrow(ctx, ref));
    wr_done(ctx, ref, dst.lo, dst.hi);
    return 1;
}

/**
 * emit_i64_imm() - Emit an i64 operation with a constant operand.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * Shifts and rotations by a constant amount, and add, sub, and, or, xor with
 * a constant (first operand: only for the commutative ones). Each word of the
 * constant gets its cheapest form, so i64.and x, 0xffffffff is one copy and a
 * zero.
 *
 * Return: 1 if emitted, 0 if not applicable (nothing emitted).
 */
static int emit_i64_imm(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    unsigned opcode = ir_op(insn);
    ir_ref op_a = ir_a(insn);
    uint64_t cval;
    bool want_hi = !is_narrow(ctx, ref);
    if (in_range(opcode, IR_OP_SHL, IR_OP_ROTR)) {
        return emit_shift64_imm(ctx, ref);
    }
    bool comm = opcode == IR_OP_ADD || opcode == IR_OP_AND ||
                opcode == IR_OP_OR || opcode == IR_OP_XOR;
    if (!comm && opcode != IR_OP_SUB) {
        return 0;
    }
    if (!const64_of(ctx, ir_b(insn), &cval)) {
        if (!comm || !const64_of(ctx, op_a, &cval)) {
            return 0;
        }
        op_a = ir_b(insn);
    }
    reg_pair src = {rd_lo(ctx, op_a, RV_T0), rd_hi(ctx, op_a, RV_T1)};
    reg_pair dst = {wr_lo(ctx, ref), wr_hi(ctx, ref)};
    if (opcode == IR_OP_ADD || opcode == IR_OP_SUB) {
        /* x - k = x + (-k) */
        add64_imm(ctx, dst, src, opcode == IR_OP_SUB ? 0U - cval : cval,
                  want_hi);
    } else {
        logic_word(ctx, opcode, dst.lo, src.lo, (uint32_t)cval, RV_T2);
        if (want_hi) {
            logic_word(ctx, opcode, dst.hi, src.hi,
                       (uint32_t)(cval >> WORD_BITS), RV_T3);
        }
    }
    wr_done(ctx, ref, dst.lo, dst.hi);
    return 1;
}

/**
 * divrem64() - i64 div/rem: a2:a3 = t0:t1 op t2:t3.
 * @ctx: Emitter state.
 * @opcode: IR_OP_DIV_S, _DIV_U, _REM_S or _REM_U.
 *
 * The WASM traps inline (division by zero, INT64_MIN / -1), x % -1 = 0 inline,
 * then the runtime intrinsic.
 */
static void divrem64(em_t *ctx, unsigned opcode)
{
    static const uint8_t sym[] = {WAMR_SYM_I64_DIV_S, WAMR_SYM_I64_DIV_U,
                                  WAMR_SYM_I64_REM_S, WAMR_SYM_I64_REM_U};
    emit(ctx, rv_or(RV_T4, RV_T2, RV_T3));
    br_trap(ctx, rv_beq(RV_T4, RV_ZERO, rv_skip(1)),
            WAMR_EXCE_INTEGER_DIVIDE_BY_ZERO);
    uint32_t skip_call = 0;
    if (opcode == IR_OP_DIV_S || opcode == IR_OP_REM_S) {
        emit(ctx, rv_and(RV_T4, RV_T2, RV_T3));
        emit(ctx, rv_addi(RV_T4, RV_T4, 1)); /* 0 iff b == -1 */
        uint32_t not_m1 = fwd_br(ctx, rv_bne(RV_T4, RV_ZERO, 0));
        if (opcode == IR_OP_DIV_S) { /* INT64_MIN / -1 overflows */
            emit(ctx, rv_lui(RV_T5, SIGN_BIT_HI20));
            uint32_t hi_ok = fwd_br(ctx, rv_bne(RV_T1, RV_T5, 0));
            uint32_t lo_ok = fwd_br(ctx, rv_bne(RV_T0, RV_ZERO, 0));
            jump_trap(ctx, WAMR_EXCE_INTEGER_OVERFLOW);
            bind_br(ctx, hi_ok);
            bind_br(ctx, lo_ok);
        } else { /* x % -1 = 0: the C helper must not see INT64_MIN % -1 */
            emit(ctx, rv_mv(RV_A2, RV_ZERO));
            emit(ctx, rv_mv(RV_A3, RV_ZERO));
            skip_call = fwd_j(ctx);
        }
        bind_br(ctx, not_m1);
    }
    emit(ctx, rv_mv(RV_A0, RV_T0));
    emit(ctx, rv_mv(RV_A1, RV_T1));
    emit(ctx, rv_mv(RV_A2, RV_T2));
    emit(ctx, rv_mv(RV_A3, RV_T3));
    call_rt(ctx, sym[opcode - IR_OP_DIV_S]);
    emit(ctx, rv_mv(RV_A2, RV_A0));
    emit(ctx, rv_mv(RV_A3, RV_A1));
    if (skip_call) {
        bind_j(ctx, skip_call);
    }
}

/* @dst = @lhs add, sub or mul (@opcode) @rhs, both words. Uses t4-t6. */
static void arith64(em_t *ctx, unsigned opcode, reg_pair dst, reg_pair lhs,
                    reg_pair rhs)
{
    if (opcode == IR_OP_SUB) {
        emit(ctx, rv_sltu(RV_T4, lhs.lo, rhs.lo)); /* borrow */
        emit(ctx, rv_sub(dst.lo, lhs.lo, rhs.lo));
        emit(ctx, rv_sub(dst.hi, lhs.hi, rhs.hi));
        emit(ctx, rv_sub(dst.hi, dst.hi, RV_T4));
        return;
    }
    if (opcode == IR_OP_MUL) {
        /* hi = mulhu(lo, lo) + lo * hi + hi * lo, without the terms of
         * a high word known zero: (u64)a * b of 32-bit values is mul +
         * mulhu */
        emit(ctx, rv_mulhu(dst.hi, lhs.lo, rhs.lo));
        if (rhs.hi != RV_ZERO) {
            emit(ctx, rv_mul(RV_T5, lhs.lo, rhs.hi));
            emit(ctx, rv_add(dst.hi, dst.hi, RV_T5));
        }
        if (lhs.hi != RV_ZERO) {
            emit(ctx, rv_mul(RV_T6, lhs.hi, rhs.lo));
            emit(ctx, rv_add(dst.hi, dst.hi, RV_T6));
        }
        emit(ctx, rv_mul(dst.lo, lhs.lo, rhs.lo));
        return;
    }
    emit(ctx, rv_add(dst.lo, lhs.lo, rhs.lo));
    emit(ctx, rv_sltu(RV_T4, dst.lo, lhs.lo)); /* carry */
    emit(ctx, rv_add(dst.hi, lhs.hi, rhs.hi));
    emit(ctx, rv_add(dst.hi, dst.hi, RV_T4));
}

/* @dst = @lhs == @rhs (@is_eq) or @lhs != @rhs. Uses t4, t5. */
static void eq64(em_t *ctx, bool is_eq, unsigned dst, reg_pair lhs,
                 reg_pair rhs)
{
    emit(ctx, rv_xor(RV_T4, lhs.lo, rhs.lo));
    if (lhs.hi != RV_ZERO || rhs.hi != RV_ZERO) { /* else one word */
        emit(ctx, rv_xor(RV_T5, lhs.hi, rhs.hi));
        emit(ctx, rv_or(RV_T4, RV_T4, RV_T5));
    }
    emit(ctx, is_eq ? rv_seqz(dst, RV_T4) : rv_snez(dst, RV_T4));
}

/* @dst = the ordered comparison @cmp of @lhs and @rhs */
static void cmp64(em_t *ctx, cmp_t cmp, unsigned dst, reg_pair lhs,
                  reg_pair rhs)
{
    if (lhs.hi == RV_ZERO && rhs.hi == RV_ZERO) {
        /* both in [0, 2^32): signed or not, the low words decide */
        emit(ctx, cmp.swap ? rv_sltu(dst, rhs.lo, lhs.lo)
                           : rv_sltu(dst, lhs.lo, rhs.lo));
    } else if (cmp.swap) {
        lt64(ctx, dst, rhs, lhs, !cmp.uns);
    } else {
        lt64(ctx, dst, lhs, rhs, !cmp.uns);
    }
    if (cmp.inv) {
        emit(ctx, rv_xori(dst, dst, 1));
    }
}

/**
 * emit_binary_i64() - Emit an i64 binary operation or comparison.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * Shifts, rotations, div and rem work on copies in t0:t1 and t2:t3; the others
 * read the operands and write the result in place (the result never shares a
 * register with an operand, see ra.h).
 */
static void emit_binary_i64(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    unsigned opcode = ir_op(insn);
    ir_ref op_a = ir_a(insn);
    ir_ref op_b = ir_b(insn);
    if (emit_i64_imm(ctx, ref)) {
        return;
    }
    if (in_range(opcode, IR_OP_DIV_S, IR_OP_REM_U) ||
        in_range(opcode, IR_OP_SHL, IR_OP_ROTR)) {
        /* longer sequences, on copies of the operands */
        load_val(ctx, op_a, RV_T0, RV_T1);
        load_val(ctx, op_b, RV_T2, RV_T3);
        if (opcode == IR_OP_ROTL || opcode == IR_OP_ROTR) {
            rot64(ctx, opcode);
        } else if (in_range(opcode, IR_OP_SHL, IR_OP_SHR_U)) {
            shift64(ctx, opcode);
        } else {
            divrem64(ctx, opcode);
        }
        store_val(ctx, ref, RV_A2, RV_A3);
        return;
    }

    /* operands read in place, result written in place (it never shares a
     * register with an operand, see ra.h) */
    reg_pair lhs;
    reg_pair rhs;
    reg_pair dst;
    lhs.lo = rd_lo(ctx, op_a, RV_T0);
    lhs.hi = rd_hi(ctx, op_a, RV_T1);
    rhs.lo = rd_lo(ctx, op_b, RV_T2);
    rhs.hi = rd_hi(ctx, op_b, RV_T3);
    dst.lo = wr_lo(ctx, ref);
    dst.hi = wr_hi(ctx, ref);
    switch (opcode) {
    case IR_OP_ADD:
    case IR_OP_SUB:
    case IR_OP_MUL:
        if (is_narrow(ctx, ref)) { /* the high word is not read */
            emit(ctx, alu_rr[opcode](dst.lo, lhs.lo, rhs.lo));
        } else {
            arith64(ctx, opcode, dst, lhs, rhs);
        }
        break;
    case IR_OP_AND:
    case IR_OP_OR:
    case IR_OP_XOR:
        emit(ctx, alu_rr[opcode](dst.lo, lhs.lo, rhs.lo));
        if (!is_narrow(ctx, ref)) {
            emit(ctx, alu_rr[opcode](dst.hi, lhs.hi, rhs.hi));
        }
        break;
    case IR_OP_EQ: /* comparisons: i32 result */
    case IR_OP_NE:
        eq64(ctx, opcode == IR_OP_EQ, dst.lo, lhs, rhs);
        break;
    default:
        if (!is_ordered_cmp(opcode)) {
            unsupported(ctx);
            return;
        }
        cmp64(ctx, cmp_form(opcode), dst.lo, lhs, rhs);
        break;
    }
    wr_done(ctx, ref, dst.lo, dst.hi);
}

/**
 * emit_unary_i64() - Emit i64 clz, ctz or popcnt.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 */
static void emit_unary_i64(em_t *ctx, ir_ref ref)
{
    unsigned opcode = ir_op(ctx->f->insn[ref]);
    switch (opcode) {
    case IR_OP_CLZ: { /* hi != 0 ? clz(hi) : 32 + clz(lo) */
        emit(ctx, rv_mv(RV_T5, RV_T0));
        uint32_t to_lo = fwd_br(ctx, rv_beq(RV_T1, RV_ZERO, 0));
        emit(ctx, rv_mv(RV_T0, RV_T1));
        clz32(ctx);
        uint32_t done = fwd_j(ctx);
        bind_br(ctx, to_lo);
        emit(ctx, rv_mv(RV_T0, RV_T5));
        clz32(ctx);
        emit(ctx, rv_addi(RV_A2, RV_A2, WORD_BITS));
        bind_j(ctx, done);
        break;
    }
    case IR_OP_CTZ: { /* lo != 0 ? ctz(lo) : 32 + ctz(hi) */
        emit(ctx, rv_mv(RV_T5, RV_T1));
        uint32_t to_hi = fwd_br(ctx, rv_beq(RV_T0, RV_ZERO, 0));
        ctz32(ctx);
        uint32_t done = fwd_j(ctx);
        bind_br(ctx, to_hi);
        emit(ctx, rv_mv(RV_T0, RV_T5));
        ctz32(ctx);
        emit(ctx, rv_addi(RV_A2, RV_A2, WORD_BITS));
        bind_j(ctx, done);
        break;
    }
    case IR_OP_POPCNT:
        emit(ctx, rv_mv(RV_T5, RV_T1));
        popcnt32(ctx);
        emit(ctx, rv_mv(RV_A4, RV_A2));
        emit(ctx, rv_mv(RV_T0, RV_T5));
        popcnt32(ctx);
        emit(ctx, rv_add(RV_A2, RV_A2, RV_A4));
        break;
    default:
        unsupported(ctx);
        return;
    }
    emit(ctx, rv_mv(RV_A3, RV_ZERO));
    store_val(ctx, ref, RV_A2, RV_A3);
}

/* ------------------------------------------------------------------------ */
/* Soft-float: every operation is a call to an aot_intrinsic_* function     */
/* except the sign-bit operations.                                         */
/* ------------------------------------------------------------------------ */

/**
 * load_args() - Load a value as C arguments.
 * @ctx: Emitter state.
 * @val: The value.
 * @reg: First argument register.
 *
 * ILP32: a 64-bit value takes two registers.
 *
 * Return: The next free argument register.
 */
static unsigned load_args(em_t *ctx, ir_ref val, unsigned reg)
{
    load_val(ctx, val, reg, reg + 1U);
    return reg + (is_wide(ty_of(ctx, val)) ? 2U : 1U);
}

/**
 * emit_float_binary() - Emit a float binary operation or comparison.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * copysign inline on the sign bit; the others call the aot_intrinsic_*
 * functions.
 */
static void emit_float_binary(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    static const uint8_t cond[] = {WAMR_FLOAT_EQ, WAMR_FLOAT_NE, WAMR_FLOAT_LT,
                                   WAMR_FLOAT_GT, WAMR_FLOAT_LE, WAMR_FLOAT_GE};
    unsigned opcode = ir_op(insn);
    ir_ref op_a = ir_a(insn);
    ir_ref op_b = ir_b(insn);
    int is_f64 = is_wide(ty_of(ctx, op_a));

    if (opcode ==
        IR_OP_FCOPYSIGN) { /* (a & ~sign) | (b & sign) on the high word */
        load_val(ctx, op_a, RV_T0, RV_T1);
        load_val(ctx, op_b, RV_T2, RV_T3);
        unsigned lhs_hi = is_f64 ? RV_T1 : RV_T0;
        unsigned rhs_hi = is_f64 ? RV_T3 : RV_T2;
        emit(ctx, rv_slli(lhs_hi, lhs_hi, 1));
        emit(ctx, rv_srli(lhs_hi, lhs_hi, 1));
        emit(ctx, rv_srli(rhs_hi, rhs_hi, SIGN_SHIFT));
        emit(ctx, rv_slli(rhs_hi, rhs_hi, SIGN_SHIFT));
        emit(ctx, rv_or(is_f64 ? RV_A3 : RV_A2, lhs_hi, rhs_hi));
        if (is_f64) {
            emit(ctx, rv_mv(RV_A2, RV_T0));
        }
        store_val(ctx, ref, RV_A2, RV_A3);
        return;
    }
    if (in_range(opcode, IR_OP_FEQ, IR_OP_FGE)) { /* fXX_cmp(cond, a, b) */
        load_args(ctx, op_b, load_args(ctx, op_a, RV_A1));
        emit(ctx, rv_addi(RV_A0, RV_ZERO, cond[opcode - IR_OP_FEQ]));
        call_rt(ctx, is_f64 ? WAMR_SYM_F64_CMP : WAMR_SYM_F32_CMP);
    } else if (float_sym[opcode][is_f64]) {
        load_args(ctx, op_b, load_args(ctx, op_a, RV_A0));
        call_rt(ctx, float_sym[opcode][is_f64]);
    } else {
        unsupported(ctx);
        return;
    }
    store_val(ctx, ref, RV_A0, RV_A1);
}

/**
 * emit_trunc() - Emit a float to integer truncation.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * Inline checks on the bit pattern: NaN raises trap 5, a
 * value out of range trap 3; then the intrinsic.
 */
static void emit_trunc(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    /* exclusive limits of |x|'s bits for x >= 0 and x < 0, by [v]
     * (i32 s, i32 u, i64 s, i64 u); verified against a reference on every
     * f32 and on 1e8 f64 around the limits */
    static const uint32_t L32[4][2] = {{0x4f000000U, 0x4f000001U},
                                       {0x4f800000U, 0x3f800000U},
                                       {0x5f000000U, 0x5f000001U},
                                       {0x5f800000U, 0x3f800000U}};
    static const uint32_t L64H[4][2] = {{0x41e00000U, 0x41e00000U},
                                        {0x41f00000U, 0x3ff00000U},
                                        {0x43e00000U, 0x43e00000U},
                                        {0x43f00000U, 0x3ff00000U}};
    static const uint32_t L64L[4][2] = {
        {0U, 0x00200000U}, {0U, 0U}, {0U, 1U}, {0U, 0U}};
    static const uint8_t sym[2][4] = {
        {WAMR_SYM_F32_TO_I32, WAMR_SYM_F32_TO_U32, WAMR_SYM_F32_TO_I64,
         WAMR_SYM_F32_TO_U64},
        {WAMR_SYM_F64_TO_I32, WAMR_SYM_F64_TO_U32, WAMR_SYM_F64_TO_I64,
         WAMR_SYM_F64_TO_U64}};
    const unsigned nan = WAMR_EXCE_INVALID_CONVERSION_TO_INTEGER;
    const unsigned ovf = WAMR_EXCE_INTEGER_OVERFLOW;
    unsigned val =
        (ir_ty(insn) == IR_T_I64 ? 2U : 0U) + (ir_op(insn) == IR_OP_TRUNC_U);
    int is_f64 = is_wide(ty_of(ctx, ir_a(insn)));

    load_val(ctx, ir_a(insn), RV_T0, RV_T1);
    emit(ctx, rv_slli(RV_T4, is_f64 ? RV_T1 : RV_T0, 1)); /* |x|, high word */
    emit(ctx, rv_srli(RV_T4, RV_T4, 1));
    if (!is_f64) {
        const uint32_t f32_nan_min = 0x7F800001U; /* |x| above +inf */
        li(ctx, RV_T5, f32_nan_min);
        br_trap(ctx, rv_bgeu(RV_T4, RV_T5, rv_skip(1)), nan);
        uint32_t neg = fwd_br(ctx, rv_blt(RV_T0, RV_ZERO, 0));
        li(ctx, RV_T5, L32[val][0]);
        uint32_t cmp = fwd_j(ctx);
        bind_br(ctx, neg);
        li(ctx, RV_T5, L32[val][1]);
        bind_j(ctx, cmp);
        br_trap(ctx, rv_bgeu(RV_T4, RV_T5, rv_skip(1)), ovf);
    } else {
        const uint32_t f64_inf_hi = 0x7FF00000U; /* NaN: |x| > +inf */
        li(ctx, RV_T5, f64_inf_hi);
        br_trap(ctx, rv_bltu(RV_T5, RV_T4, rv_skip(1)), nan);
        uint32_t not_nan = fwd_br(ctx, rv_bne(RV_T4, RV_T5, 0));
        br_trap(ctx, rv_bne(RV_T0, RV_ZERO, rv_skip(1)), nan);
        bind_br(ctx, not_nan);
        uint32_t neg = fwd_br(ctx, rv_blt(RV_T1, RV_ZERO, 0));
        li(ctx, RV_T5, L64H[val][0]);
        li(ctx, RV_T6, L64L[val][0]);
        uint32_t cmp = fwd_j(ctx);
        bind_br(ctx, neg);
        li(ctx, RV_T5, L64H[val][1]);
        li(ctx, RV_T6, L64L[val][1]);
        bind_j(ctx, cmp);
        /* valid iff |x| < L: hi < Lhi, or hi == Lhi and lo < Llo */
        uint32_t good = fwd_br(ctx, rv_bltu(RV_T4, RV_T5, 0));
        br_trap(ctx, rv_bne(RV_T4, RV_T5, rv_skip(1)), ovf);
        br_trap(ctx, rv_bgeu(RV_T0, RV_T6, rv_skip(1)), ovf);
        bind_br(ctx, good);
    }
    emit(ctx, rv_mv(RV_A0, RV_T0));
    emit(ctx, rv_mv(RV_A1, RV_T1));
    call_rt(ctx, sym[is_f64][val]);
    store_val(ctx, ref, RV_A0, RV_A1);
}

/**
 * emit_float_unary() - Emit a float unary operation or conversion.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * neg and abs inline on the sign bit; the others, and int to float conversions,
 * call the intrinsics.
 */
static void emit_float_unary(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    /* int -> float, by [source i64][to f64][unsigned] */
    static const uint8_t convert[2][2][2] = {
        {{WAMR_SYM_I32_TO_F32, WAMR_SYM_U32_TO_F32},
         {WAMR_SYM_I32_TO_F64, WAMR_SYM_U32_TO_F64}},
        {{WAMR_SYM_I64_TO_F32, WAMR_SYM_U64_TO_F32},
         {WAMR_SYM_I64_TO_F64, WAMR_SYM_U64_TO_F64}}};
    unsigned opcode = ir_op(insn);
    ir_ref op_a = ir_a(insn);
    unsigned aty = ty_of(ctx, op_a);
    int is_f64 = is_wide(aty);
    unsigned sym;

    if (opcode == IR_OP_FNEG ||
        opcode == IR_OP_FABS) { /* flip / clear the sign */
        load_val(ctx, op_a, RV_T0, RV_T1);
        unsigned high_reg = is_f64 ? RV_T1 : RV_T0;
        if (opcode == IR_OP_FNEG) {
            emit(ctx, rv_lui(RV_T4, SIGN_BIT_HI20));
            emit(ctx, rv_xor(high_reg, high_reg, RV_T4));
        } else {
            emit(ctx, rv_slli(high_reg, high_reg, 1));
            emit(ctx, rv_srli(high_reg, high_reg, 1));
        }
        store_val(ctx, ref, RV_T0, RV_T1);
        return;
    }
    if (opcode == IR_OP_CONVERT_S || opcode == IR_OP_CONVERT_U) {
        sym = convert[aty == IR_T_I64][ir_ty(insn) != IR_T_F32]
                     [opcode == IR_OP_CONVERT_U];
    } else {
        sym = float_sym[opcode][is_f64];
    }
    if (!sym) {
        unsupported(ctx);
        return;
    }
    load_args(ctx, op_a, RV_A0);
    call_rt(ctx, sym);
    store_val(ctx, ref, RV_A0, RV_A1);
}

/* ------------------------------------------------------------------------ */
/* Unary and binary operations                                              */
/* ------------------------------------------------------------------------ */

/**
 * emit_unary() - Emit a unary operation.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * Conversions, eqz, float operations, clz, ctz, popcnt.
 */
static void emit_unary(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    unsigned opcode = ir_op(insn);
    ir_ref op_a = ir_a(insn);
    unsigned aty = ty_of(ctx, op_a);

    switch (opcode) {
    case IR_OP_WRAP: /* i64 -> i32: low half */
        store_val(ctx, ref, rd_lo(ctx, op_a, RV_T0), RV_ZERO);
        return;
    case IR_OP_EXTEND_S:
    case IR_OP_EXTEND_U: {
        unsigned src = rd_lo(ctx, op_a, RV_T0);
        unsigned dst_lo = wr_lo(ctx, ref);
        unsigned dst_hi = wr_hi(ctx, ref);
        emit(ctx, rv_mv(dst_lo, src));
        if (!is_narrow(ctx, ref)) { /* extend_u always is */
            emit(ctx, opcode == IR_OP_EXTEND_S
                          ? rv_srai(dst_hi, src, SIGN_SHIFT)
                          : rv_mv(dst_hi, RV_ZERO));
        }
        wr_done(ctx, ref, dst_lo, dst_hi);
        return;
    }
    case IR_OP_REINTERPRET: /* same bits */
        load_val(ctx, op_a, RV_T0, RV_T1);
        store_val(ctx, ref, RV_T0, RV_T1);
        return;
    case IR_OP_TRUNC_S:
    case IR_OP_TRUNC_U:
        emit_trunc(ctx, ref);
        return;
    case IR_OP_EQZ: {
        unsigned dst = wr_lo(ctx, ref);
        if (aty == IR_T_I64 && !hi_zero(ctx, op_a)) {
            unsigned reg_lo = rd_lo(ctx, op_a, RV_T0);
            emit(ctx, rv_or(RV_T4, reg_lo, rd_hi(ctx, op_a, RV_T1)));
            emit(ctx, rv_seqz(dst, RV_T4));
        } else {
            emit(ctx, rv_seqz(dst, rd_lo(ctx, op_a, RV_T0)));
        }
        wr_done(ctx, ref, dst, dst);
        return;
    }
    default:
        break;
    }
    if (in_range(opcode, IR_OP_FABS, IR_OP_FSQRT) ||
        in_range(opcode, IR_OP_CONVERT_S, IR_OP_PROMOTE)) {
        emit_float_unary(ctx, ref);
        return;
    }

    /* clz, ctz, popcnt */
    load_val(ctx, op_a, RV_T0, RV_T1);
    if (aty == IR_T_I64) {
        emit_unary_i64(ctx, ref);
        return;
    }
    if (aty != IR_T_I32) {
        unsupported(ctx);
        return;
    }
    switch (opcode) {
    case IR_OP_CLZ:
        clz32(ctx);
        break;
    case IR_OP_CTZ:
        ctz32(ctx);
        break;
    case IR_OP_POPCNT:
        popcnt32(ctx);
        break;
    default:
        unsupported(ctx);
        return;
    }
    store_val(ctx, ref, RV_A2, RV_A3);
}

/**
 * const32_of() - Get the value of a 32-bit constant.
 * @ctx: Emitter state.
 * @ref: The value, or IR_NIL.
 * @out: Set to the constant.
 *
 * Return: 1 if @ref is a CONST32, else 0.
 */
static int const32_of(em_t *ctx, ir_ref ref, uint32_t *out)
{
    return ref != IR_NIL && ir_op(ctx->f->insn[ref]) == IR_OP_CONST32 &&
           const_word(ctx, ref, false, out);
}

/**
 * emit_shift_imm() - Emit an i32 shift or rotation by a constant.
 * @ctx: Emitter state.
 * @opcode: The opcode.
 * @regs: Destination and operand registers.
 * @amount: The shift amount (taken modulo 32).
 */
static void emit_shift_imm(em_t *ctx, unsigned opcode, reg_op regs,
                           uint32_t amount)
{
    const unsigned dst = regs.dst;
    const unsigned src = regs.src;
    unsigned shamt = amount & I32_SHIFT_MASK;
    if (opcode == IR_OP_SHL) {
        emit(ctx, rv_slli(dst, src, shamt));
    } else if (opcode == IR_OP_SHR_S) {
        emit(ctx, rv_srai(dst, src, shamt));
    } else if (opcode == IR_OP_SHR_U) {
        emit(ctx, rv_srli(dst, src, shamt));
    } else { /* rotate left by len */
        unsigned len =
            opcode == IR_OP_ROTL ? shamt : (WORD_BITS - shamt) & I32_SHIFT_MASK;
        if (!len) {
            emit(ctx, rv_mv(dst, src));
            return;
        }
        emit(ctx, rv_slli(RV_T3, src, len));
        emit(ctx, rv_srli(dst, src, WORD_BITS - len));
        emit(ctx, rv_or(dst, dst, RV_T3));
    }
}

/**
 * emit_binary_imm() - Emit an i32 operation with an immediate operand.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * For a constant operand that fits an immediate form (constant first: only for
 * commutative operations).
 *
 * Return: 1 if emitted, 0 if not applicable (nothing emitted).
 */
static int emit_binary_imm(em_t *ctx, ir_ref ref)
{
    ir_insn word = ctx->f->insn[ref];
    unsigned opcode = ir_op(word);
    ir_ref op_a = ir_a(word);
    ir_ref op_b = ir_b(word);
    uint32_t cval;
    if (!const32_of(ctx, op_b, &cval)) {
        int comm = alu_ri[opcode] || opcode == IR_OP_EQ || opcode == IR_OP_NE;
        if (!comm || !const32_of(ctx, op_a, &cval)) {
            return 0;
        }
        op_a = op_b;
    }
    if (in_range(opcode, IR_OP_SHL, IR_OP_ROTR)) {
        unsigned src = rd_lo(ctx, op_a, RV_T0);
        unsigned dst = wr_lo(ctx, ref);
        emit_shift_imm(ctx, opcode, (reg_op){.dst = dst, .src = src}, cval);
        wr_done(ctx, ref, dst, dst);
        return 1;
    }
    if (!cval) {
        return 0; /* x0 as the second operand is as good */
    }

    /* one immediate instruction, then maybe one fix-up of the result */
    enum { THEN_NONE, THEN_SEQZ, THEN_SNEZ, THEN_NOT } then = THEN_NONE;
    itype_fn insn = alu_ri[opcode];
    int32_t imm = (int32_t)cval;
    if (opcode == IR_OP_SUB) {
        insn = rv_addi;
        imm = (int32_t)(0U - cval);
    } else if (opcode == IR_OP_EQ || opcode == IR_OP_NE) {
        insn = rv_xori;
        then = opcode == IR_OP_EQ ? THEN_SEQZ : THEN_SNEZ;
    } else if (is_ordered_cmp(opcode)) {
        /* x > k = !(x < k + 1), x <= k = x < k + 1 */
        cmp_t cmpf = cmp_form(opcode);
        uint32_t cval_adj = cval + cmpf.swap;
        if (cmpf.uns && cmpf.swap && cval_adj == 0) {
            return 0; /* k + 1 wraps */
        }
        insn = cmpf.uns ? rv_sltiu : rv_slti;
        imm = (int32_t)cval_adj;
        then = cmpf.inv != cmpf.swap ? THEN_NOT : THEN_NONE;
    }
    if (!insn || !rv_fits12(imm)) {
        return 0;
    }
    unsigned src = rd_lo(ctx, op_a, RV_T0);
    unsigned dst = wr_lo(ctx, ref);
    emit(ctx, insn(dst, src, imm));
    if (then == THEN_SEQZ) {
        emit(ctx, rv_seqz(dst, dst));
    } else if (then == THEN_SNEZ) {
        emit(ctx, rv_snez(dst, dst));
    } else if (then == THEN_NOT) {
        emit(ctx, rv_xori(dst, dst, 1));
    }
    wr_done(ctx, ref, dst, dst);
    return 1;
}

/**
 * emit_binary_i32() - Emit an i32 binary operation on two registers.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 */
static void emit_binary_i32(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    unsigned opcode = ir_op(insn);
    unsigned src = rd_lo(ctx, ir_a(insn), RV_T0);
    unsigned val2 = rd_lo(ctx, ir_b(insn), RV_T2);
    unsigned dst = wr_lo(ctx, ref);
    if (in_range(opcode, IR_OP_DIV_S, IR_OP_REM_U)) {
        br_trap(ctx, rv_beq(val2, RV_ZERO, rv_skip(1)),
                WAMR_EXCE_INTEGER_DIVIDE_BY_ZERO);
    }
    if (opcode == IR_OP_DIV_S) { /* INT_MIN / -1 overflows */
        emit(ctx, rv_addi(RV_T3, RV_ZERO, -1));
        emit(ctx, rv_bne(val2, RV_T3, rv_skip(3)));
        emit(ctx, rv_lui(RV_T4, SIGN_BIT_HI20));
        emit(ctx, rv_bne(src, RV_T4, rv_skip(1)));
        jump_trap(ctx, WAMR_EXCE_INTEGER_OVERFLOW);
    }
    /* REM_S: INT_MIN % -1 = 0 on RISC-V, as WASM requires */
    if (alu_rr[opcode]) {
        emit(ctx, alu_rr[opcode](dst, src, val2));
    } else if (opcode == IR_OP_ROTL || opcode == IR_OP_ROTR) {
        rtype_fn fwd = opcode == IR_OP_ROTL ? rv_sll : rv_srl;
        rtype_fn back = opcode == IR_OP_ROTL ? rv_srl : rv_sll;
        emit(ctx, fwd(dst, src, val2));
        emit(ctx, rv_neg(RV_T3, val2));
        emit(ctx, back(RV_T3, src, RV_T3));
        emit(ctx, rv_or(dst, dst, RV_T3));
    } else if (opcode == IR_OP_EQ || opcode == IR_OP_NE) {
        emit(ctx, rv_xor(dst, src, val2));
        emit(ctx, opcode == IR_OP_EQ ? rv_seqz(dst, dst) : rv_snez(dst, dst));
    } else if (is_ordered_cmp(opcode)) {
        cmp_t cmp = cmp_form(opcode);
        rtype_fn slt = cmp.uns ? rv_sltu : rv_slt;
        emit(ctx, cmp.swap ? slt(dst, val2, src) : slt(dst, src, val2));
        if (cmp.inv) {
            emit(ctx, rv_xori(dst, dst, 1));
        }
    } else {
        unsupported(ctx);
        return;
    }
    wr_done(ctx, ref, dst, dst);
}

/**
 * emit_binary() - Emit a binary operation or comparison.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * i32 inline, with the WASM traps of div and rem; dispatches i64 and float
 * operations.
 */
static void emit_binary(em_t *ctx, ir_ref ref)
{
    unsigned aty = ty_of(ctx, ir_a(ctx->f->insn[ref]));
    if (aty == IR_T_F32 || aty == IR_T_F64) {
        emit_float_binary(ctx, ref);
        return;
    }
    if (aty == IR_T_I64) {
        emit_binary_i64(ctx, ref);
        return;
    }
    if (aty != IR_T_I32) {
        unsupported(ctx);
        return;
    }
    if (!emit_binary_imm(ctx, ref)) {
        emit_binary_i32(ctx, ref);
    }
}

/**
 * emit_select() - Emit select.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * r = b ^ ((a ^ b) & mask), mask = -(cond != 0), per word: no branch.
 */
static void emit_select(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    ir_ref op_a = ir_a(insn);
    ir_ref op_b = ir_b(insn);
    emit(ctx, rv_snez(RV_A4, rd_lo(ctx, ir_c(insn), RV_A4)));
    emit(ctx, rv_neg(RV_A4, RV_A4));
    unsigned dst_lo = wr_lo(ctx, ref);
    unsigned dst_hi = wr_hi(ctx, ref);
    int words = is_wide(ir_ty(insn)) && !is_narrow(ctx, ref) ? 2 : 1;
    for (int hi = 0; hi < words; hi++) {
        unsigned src = rd_word(ctx, op_a, hi, hi ? RV_T1 : RV_T0);
        unsigned val2 = rd_word(ctx, op_b, hi, hi ? RV_T3 : RV_T2);
        emit(ctx, rv_xor(RV_T4, src, val2));
        emit(ctx, rv_and(RV_T4, RV_T4, RV_A4));
        emit(ctx, rv_xor(hi ? dst_hi : dst_lo, RV_T4, val2));
    }
    wr_done(ctx, ref, dst_lo, dst_hi);
}

/* ------------------------------------------------------------------------ */
/* Memory and globals                                                       */
/* ------------------------------------------------------------------------ */

/**
 * bounds_check() - Emit the bounds check of a memory access.
 * @ctx: Emitter state.
 * @addr: Register holding the address.
 * @offset: Offset of the last byte accessed from @addr.
 *
 * Return: 0 if the access always traps (then only the jump to the trap is
 *   emitted), else 1.
 */
static int bounds_check(em_t *ctx, unsigned addr, uint64_t offset)
{
    const unsigned oob = WAMR_EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS;
    if (offset > UINT32_MAX) {
        jump_trap(ctx, oob);
        return 0;
    }
    if (ctx->w->init_pages_zero) { /* the bound wraps when memory is empty */
        emit(ctx, rv_lw(RV_T5, RV_S2, WAMR_INST_MEM_CUR_PAGE_COUNT));
        br_trap(ctx, rv_beq(RV_T5, RV_ZERO, rv_skip(1)), oob);
    }
    if (offset == 0) {
        br_trap(ctx, rv_bltu(RV_S4, addr, rv_skip(1)), oob);
    } else if (offset <= (uint64_t)-RV_IMM12_MIN &&
               offset <= ctx->w->mem_bound_min) {
        /* a non-empty memory has bound = size - 1 >= mem_bound_min >= offset
         * (65535 or more with 64 KiB pages; a shrunk memory has a fixed size;
         * the memory never shrinks at run time; empty memory is caught
         * above), so addr + offset <= bound without wrapping iff
         * addr <= bound - offset */
        emit(ctx, rv_addi(RV_T4, RV_S4, -(int32_t)offset));
        br_trap(ctx, rv_bltu(RV_T4, addr, rv_skip(1)), oob);
    } else {
        if (rv_fits12((int32_t)offset)) { /* offset >= 2^32 - 2048 */
            emit(ctx, rv_addi(RV_T4, addr, (int32_t)offset));
        } else {
            li(ctx, RV_T4, (uint32_t)offset);
            emit(ctx, rv_add(RV_T4, addr, RV_T4));
        }
        br_trap(ctx, rv_bltu(RV_T4, addr, rv_skip(1)),
                oob); /* addr + offset wrapped */
        br_trap(ctx, rv_bltu(RV_S4, RV_T4, rv_skip(1)), oob);
    }
    return 1;
}

/**
 * struct mem_access - A load or store of linear memory.
 * @addr: The address value.
 * @offset: The static offset.
 * @size: Bytes accessed.
 */
typedef struct {
    ir_ref addr;
    uint32_t offset;
    uint32_t size;
} mem_access;

/**
 * mem_address() - Compute the native address of a memory access.
 * @ctx: Emitter state.
 * @access: The address value, the static offset and the bytes accessed.
 *
 * Leaves the address in t5, after the bounds check.
 *
 * Return: The offset to use in the load/store (12 bits, with room for a +4), or
 *   SLOT_NONE if the access always traps.
 */
static uint32_t mem_address(em_t *ctx, mem_access access)
{
    const ir_ref addr = access.addr;
    const uint32_t nbytes = access.size;
    uint32_t off = access.offset;
    unsigned addr_reg = rd_lo(ctx, addr, RV_T0);
    if (ctx->bounds &&
        !bounds_check(ctx, addr_reg, (uint64_t)off + nbytes - 1U)) {
        return SLOT_NONE;
    }
    emit(ctx, rv_add(RV_T5, RV_S3, addr_reg));
    if (off > RV_IMM12_MAX - 4U) { /* room for the high word at +4 */
        li(ctx, RV_T4, off);
        emit(ctx, rv_add(RV_T5, RV_T5, RV_T4));
        off = 0;
    }
    return off;
}

/**
 * mem_offset() - Static offset of a load or store.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 * @insn: The instruction word.
 *
 * Return: The offset, from an extension slot if wide.
 */
static uint32_t mem_offset(em_t *ctx, ir_ref ref, ir_insn insn)
{
    return (ir_fl(insn) & IR_FL_WIDE) ? (uint32_t)ctx->f->insn[ref + 1]
                                      : ir_c(insn);
}

/**
 * emit_load() - Emit a load.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * A narrow load into an i64 is sign- or zero-extended.
 */
static void emit_load(em_t *ctx, ir_ref ref)
{
    ir_insn word = ctx->f->insn[ref];
    static const struct {
        uint8_t size, sext;
        itype_fn insn;
    } form[] = {
        /* LOAD (8 bytes for i64/f64), 8_S, 8_U, 16_S, 16_U, 32_S, 32_U */
        {4, 0, rv_lw},  {1, 1, rv_lb}, {1, 0, rv_lbu}, {2, 1, rv_lh},
        {2, 0, rv_lhu}, {4, 1, rv_lw}, {4, 0, rv_lw},
    };
    unsigned opcode = ir_op(word);
    unsigned type = ir_ty(word);
    int full = opcode == IR_OP_LOAD;
    uint32_t count = full ? val_size(type) : form[opcode - IR_OP_LOAD].size;
    uint32_t off =
        mem_address(ctx, (mem_access){.addr = ir_a(word),
                                      .offset = mem_offset(ctx, ref, word),
                                      .size = count});
    if (off == SLOT_NONE) {
        return;
    }
    unsigned dst_lo = wr_lo(ctx, ref);
    unsigned dst_hi = wr_hi(ctx, ref);
    emit(ctx, form[opcode - IR_OP_LOAD].insn(dst_lo, RV_T5, (int32_t)off));
    if (full && is_wide(type)) {
        if (!is_narrow(ctx, ref)) {
            emit(ctx, rv_lw(dst_hi, RV_T5, (int32_t)off + 4));
        }
    } else if (!full && type == IR_T_I64 && !is_narrow(ctx, ref)) {
        /* sign-extend the loaded value (zero-extended ones are narrow) */
        emit(ctx, form[opcode - IR_OP_LOAD].sext
                      ? rv_srai(dst_hi, dst_lo, SIGN_SHIFT)
                      : rv_mv(dst_hi, RV_ZERO));
    }
    wr_done(ctx, ref, dst_lo, dst_hi);
}

/**
 * emit_store() - Emit a store.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 */
static void emit_store(em_t *ctx, ir_ref ref)
{
    ir_insn word = ctx->f->insn[ref];
    static const struct {
        uint8_t size;
        itype_fn insn;
    } form[] = {{4, rv_sw}, {1, rv_sb}, {2, rv_sh}, {4, rv_sw}};
    unsigned opcode = ir_op(word);
    ir_ref val = ir_b(word);
    unsigned vty = ty_of(ctx, val);
    int full = opcode == IR_OP_STORE;
    uint32_t count = full ? val_size(vty) : form[opcode - IR_OP_STORE].size;
    uint32_t off =
        mem_address(ctx, (mem_access){.addr = ir_a(word),
                                      .offset = mem_offset(ctx, ref, word),
                                      .size = count});
    if (off == SLOT_NONE) {
        return;
    }
    emit(ctx, form[opcode - IR_OP_STORE].insn(rd_lo(ctx, val, RV_T2), RV_T5,
                                              (int32_t)off));
    if (full && is_wide(vty)) {
        emit(ctx, rv_sw(rd_hi(ctx, val, RV_T3), RV_T5, (int32_t)off + 4));
    }
}

/**
 * emit_global() - Emit global.get or global.set.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * Setting the aux stack global checks the new value (aotfmt.tex "Auxiliary
 * stack"): trap 13 if it is <= the boundary, 14 if above the bottom.
 */
static void emit_global(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    uint32_t idx = ir_bc32(insn);
    if (idx >= ctx->w->nglobals) {
        unsupported(ctx);
        return;
    }
    uint32_t off = ctx->w->global_off[idx];

    if (ir_op(insn) == IR_OP_GLOBAL_GET) {
        unsigned dst_lo = wr_lo(ctx, ref);
        unsigned dst_hi = wr_hi(ctx, ref);
        lw_at(ctx, dst_lo, RV_S2, off);
        if (is_wide(ir_ty(insn)) && !is_narrow(ctx, ref)) {
            lw_at(ctx, dst_hi, RV_S2, off + 4U);
        }
        wr_done(ctx, ref, dst_lo, dst_hi);
        return;
    }
    ir_ref val = ir_a(insn);
    unsigned src = rd_lo(ctx, val, RV_T0);
    if (ctx->aux_check && idx == ctx->w->aux_stack_global) {
        /* aux stack check (aotfmt.tex "Auxiliary stack"): trap 13 if the
         * new stack pointer is <= the boundary, 14 if above the bottom */
        emit(ctx, rv_lw(RV_T2, RV_S1, WAMR_EXEC_ENV_AUX_STACK_BOUNDARY));
        br_trap(ctx, rv_bgeu(RV_T2, src, rv_skip(1)),
                WAMR_EXCE_AUX_STACK_OVERFLOW);
        emit(ctx, rv_lw(RV_T2, RV_S1, WAMR_EXEC_ENV_AUX_STACK_BOTTOM));
        br_trap(ctx, rv_bltu(RV_T2, src, rv_skip(1)),
                WAMR_EXCE_AUX_STACK_UNDERFLOW);
    }
    sw_at(ctx, src, RV_S2, off);
    if (is_wide(ty_of(ctx, val))) {
        sw_at(ctx, rd_hi(ctx, val, RV_T1), RV_S2, off + 4U);
    }
}

/**
 * emit_mem_grow() - Emit memory.grow.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * aot_enlarge_memory(inst, delta) ? new_page_count - delta : -1. The result is
 * written only after the call (it may live in a caller-saved register); the
 * memory base and bound are reloaded.
 */
static void emit_mem_grow(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    load_val(ctx, ir_a(insn), RV_A1, RV_A2);
    emit(ctx, rv_mv(RV_A0, RV_S2));
    call_rt(ctx, WAMR_SYM_ENLARGE_MEMORY);
    emit(ctx, rv_andi(RV_A0, RV_A0, UINT8_MAX));
    emit(ctx, rv_addi(RV_A2, RV_ZERO, -1));
    uint32_t failed = fwd_br(ctx, rv_beq(RV_A0, RV_ZERO, 0));
    emit(ctx, rv_lw(RV_A2, RV_S2, WAMR_INST_MEM_CUR_PAGE_COUNT));
    /* the delta lives in a callee-saved register or a slot */
    load_val(ctx, ir_a(insn), RV_T0, RV_T1);
    emit(ctx, rv_sub(RV_A2, RV_A2, RV_T0));
    bind_br(ctx, failed);
    store_val(ctx, ref, RV_A2, RV_A3);
    load_mem_regs(ctx);
}

/* ------------------------------------------------------------------------ */
/* Calls                                                                    */
/* ------------------------------------------------------------------------ */

/**
 * callee_type() - Type of the callee, checked against the call.
 * @ctx: Emitter state.
 * @insn: The call instruction.
 * @ftype: Set to the type.
 *
 * Return: 1 on success, 0 on error (recorded in ctx->err).
 */
static int callee_type(em_t *ctx, ir_insn insn, wdec_functype_t *ftype)
{
    wasmc_err_t err = call_type(ctx, insn, ftype);
    if (!err && ir_a(insn) != ftype->param_count) {
        err = WASMC_ERR_IR_STATE;
    }
    if (err) {
        fail(ctx, err);
        return 0;
    }
    return 1;
}

/**
 * check_exception() - Return at once if the callee raised an exception.
 * @ctx: Emitter state.
 *
 * Emitted after a call to WASM code.
 */
static void check_exception(em_t *ctx)
{
    emit(ctx, rv_lbu(RV_T0, RV_S2, WAMR_INST_CUR_EXCEPTION));
    cond_jump(ctx, rv_bne(RV_T0, RV_ZERO, rv_skip(1)), &ctx->ret0);
}

/**
 * load_call_args() - Load the arguments of a call for the C convention.
 * @ctx: Emitter state.
 * @ref: The call instruction.
 * @ftype: Type of the callee.
 *
 * a1..a7, then the stack (aotfmt.tex "Function Calling Convention"). The
 * sources are stack slots or constants, so loading them straight into their
 * argument registers cannot clobber each other; uses t0, t1 and t6 only.
 */
static void load_call_args(em_t *ctx, ir_ref ref, const wdec_functype_t *ftype)
{
    abi_cursor next = ABI_CURSOR_START;
    for (unsigned k = 0; k < ftype->param_count; k++) {
        place_t place = abi_place(ftype->params[k], &next);
        unsigned reg_lo = place_lo(place);
        unsigned reg_hi = place_hi(place);
        load_val(ctx, ir_call_arg(ctx->f, ref, k), reg_lo, reg_hi);
        if (place.reg_lo < 0) {
            sw_at(ctx, reg_lo, RV_SP, place.stk_lo);
        }
        if (is_wide_param(ftype->params[k]) && place.reg_hi < 0) {
            sw_at(ctx, reg_hi, RV_SP, place.stk_hi);
        }
    }
}

/**
 * argv_cells() - Cells of argv_buf needed by a call through the runtime.
 * @ftype: Type of the callee.
 *
 * The arguments, and the result, which comes back in the same cells.
 *
 * Return: The number of 32-bit cells.
 */
static uint32_t argv_cells(const wdec_functype_t *ftype)
{
    uint32_t count = 0;
    for (uint32_t k = 0; k < ftype->param_count; k++) {
        count += is_wide_param(ftype->params[k]) ? 2U : 1U;
    }
    return ftype->result_count && count < 2U ? 2U : count;
}

/**
 * store_argv() - Store the arguments of a call into argv_buf.
 * @ctx: Emitter state.
 * @ref: The call instruction.
 * @ftype: Type of the callee.
 *
 * Leaves the address of argv_buf in t5.
 *
 * Return: The number of cells used.
 */
static uint32_t store_argv(em_t *ctx, ir_ref ref, const wdec_functype_t *ftype)
{
    uint32_t cell = 0;
    emit(ctx, rv_lw(RV_T5, RV_S1, WAMR_EXEC_ENV_ARGV_BUF));
    for (unsigned k = 0; k < ftype->param_count; k++) {
        load_val(ctx, ir_call_arg(ctx->f, ref, k), RV_T0, RV_T1);
        emit(ctx, rv_sw(RV_T0, RV_T5, (int32_t)(cell * 4U)));
        if (is_wide_param(ftype->params[k])) {
            emit(ctx, rv_sw(RV_T1, RV_T5, (int32_t)((cell * 4U) + 4U)));
        }
        cell += is_wide_param(ftype->params[k]) ? 2U : 1U;
    }
    return cell;
}

/**
 * finish_argv_call() - Finish a call through the runtime.
 * @ctx: Emitter state.
 * @ref: The call instruction.
 * @ftype: Type of the callee.
 *
 * After aot_invoke_native or aot_call_indirect: a0 is false if an exception was
 * raised, then return; else fetch the result from argv_buf.
 */
static void finish_argv_call(em_t *ctx, ir_ref ref,
                             const wdec_functype_t *ftype)
{
    emit(ctx, rv_andi(RV_A0, RV_A0, UINT8_MAX));
    cond_jump(ctx, rv_beq(RV_A0, RV_ZERO, rv_skip(1)), &ctx->ret0);
    if (ftype->result_count) {
        emit(ctx, rv_lw(RV_T5, RV_S1, WAMR_EXEC_ENV_ARGV_BUF));
        emit(ctx, rv_lw(RV_A2, RV_T5, 0));
        emit(ctx, rv_lw(RV_A3, RV_T5, 4));
        store_val(ctx, ref, RV_A2, RV_A3);
    }
}

/**
 * finish_wasm_call() - Finish a direct call to WASM code.
 * @ctx: Emitter state.
 * @ref: The call instruction.
 * @ftype: Type of the callee.
 *
 * Stores the result, then checks for an exception.
 */
static void finish_wasm_call(em_t *ctx, ir_ref ref,
                             const wdec_functype_t *ftype)
{
    if (ftype->result_count) {
        store_val(ctx, ref, RV_A0, RV_A1);
    }
    check_exception(ctx);
}

/**
 * emit_call() - Emit call.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * An imported function goes through aot_invoke_native(exec_env, idx, argc,
 * argv); a defined one is called directly.
 */
static void emit_call(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    wdec_functype_t ftype;
    if (!callee_type(ctx, insn, &ftype)) {
        return;
    }
    uint32_t fidx = ir_bc32(insn);
    if (fidx < ctx->w->nimported_funcs) {
        /* imported function: aot_invoke_native(exec_env, idx, argc, argv) */
        if (argv_cells(&ftype) > WAMR_ARGV_BUF_CELLS) {
            unsupported(ctx);
            return;
        }
        uint32_t cells = store_argv(ctx, ref, &ftype);
        emit(ctx, rv_mv(RV_A0, RV_S1));
        li(ctx, RV_A1, fidx);
        li(ctx, RV_A2, cells);
        emit(ctx, rv_mv(RV_A3, RV_T5));
        call_rt(ctx, WAMR_SYM_INVOKE_NATIVE);
        finish_argv_call(ctx, ref, &ftype);
    } else {
        load_call_args(ctx, ref, &ftype);
        emit(ctx, rv_mv(RV_A0, RV_S1));
        aotw_call(ctx->w, fidx - ctx->w->nimported_funcs);
        finish_wasm_call(ctx, ref, &ftype);
    }
    reload_mem(ctx);
}

/**
 * load_func_entry() - Load an entry of a module instance array.
 * @ctx: Emitter state.
 * @field: Offset of the array pointer in the module instance.
 *
 * t4 = field[t3], t3 being a function index; uses t5.
 */
static void load_func_entry(em_t *ctx, uint32_t field)
{
    emit(ctx, rv_lw(RV_T4, RV_S2, (int32_t)field));
    emit(ctx, rv_slli(RV_T5, RV_T3, 2));
    emit(ctx, rv_add(RV_T4, RV_T4, RV_T5));
    emit(ctx, rv_lw(RV_T4, RV_T4, 0));
}

/**
 * emit_call_indirect() - Emit call_indirect.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * Table 0 is inline in the module instance; its elements are function indices,
 * 0xFFFFFFFF = empty (aotfmt.tex "Indirect calls"). Checks the index, the
 * element and the type, then calls func_ptrs[f] for a defined function or
 * aot_call_indirect for an imported one.
 */
static void emit_call_indirect(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    wdec_functype_t ftype;
    if (!callee_type(ctx, insn, &ftype)) {
        return;
    }
    uint32_t table = ctx->w->table0_off;

    /* t3 = the function index of element t0, with the WASM checks */
    load_val(ctx, ir_b(insn), RV_T0, RV_T1);
    lw_at(ctx, RV_T1, RV_S2, table + WAMR_TABLE_CUR_SIZE);
    br_trap(ctx, rv_bgeu(RV_T0, RV_T1, rv_skip(1)),
            WAMR_EXCE_UNDEFINED_ELEMENT);
    emit(ctx, rv_slli(RV_T2, RV_T0, 2));
    emit(ctx, rv_add(RV_T2, RV_T2, RV_S2));
    lw_at(ctx, RV_T3, RV_T2, table + WAMR_TABLE_ELEMS);
    emit(ctx, rv_addi(RV_T4, RV_ZERO, -1));
    br_trap(ctx, rv_beq(RV_T3, RV_T4, rv_skip(1)),
            WAMR_EXCE_UNINITIALIZED_ELEMENT);
    load_func_entry(ctx, WAMR_INST_FUNC_TYPE_INDEXES);
    li(ctx, RV_T5, ctx->w->canon_type[ir_c(insn)]);
    br_trap(ctx, rv_bne(RV_T4, RV_T5, rv_skip(1)),
            WAMR_EXCE_INVALID_FUNCTION_TYPE_INDEX);

    uint32_t to_import = 0;
    if (ctx->w->nimported_funcs) {
        li(ctx, RV_T5, ctx->w->nimported_funcs);
        to_import = fwd_br(ctx, rv_bltu(RV_T3, RV_T5, 0));
    }
    /* defined function: func_ptrs[f](exec_env, args...) */
    load_func_entry(ctx, WAMR_INST_FUNC_PTRS); /* t4 survives load_call_args */
    load_call_args(ctx, ref, &ftype);
    emit(ctx, rv_mv(RV_A0, RV_S1));
    emit(ctx, rv_jalr(RV_RA, RV_T4, 0));
    finish_wasm_call(ctx, ref, &ftype);

    if (to_import) {
        /* imported function: aot_call_indirect(exec_env, 0, elem, argc,
         * argv) */
        uint32_t done = fwd_j(ctx);
        bind_br(ctx, to_import);
        if (argv_cells(&ftype) > WAMR_ARGV_BUF_CELLS) {
            unsupported(ctx);
            return;
        }
        uint32_t cells = store_argv(ctx, ref, &ftype);
        emit(ctx, rv_mv(RV_A4, RV_T5));
        load_val(ctx, ir_b(insn), RV_A2, RV_A3);
        emit(ctx, rv_mv(RV_A0, RV_S1));
        emit(ctx, rv_mv(RV_A1, RV_ZERO));
        li(ctx, RV_A3, cells);
        call_rt(ctx, WAMR_SYM_CALL_INDIRECT);
        finish_argv_call(ctx, ref, &ftype);
        bind_j(ctx, done);
    }
    reload_mem(ctx);
}

/* ------------------------------------------------------------------------ */
/* Control flow                                                             */
/* ------------------------------------------------------------------------ */

/**
 * emit_return() - Emit the return that ends block ctx->cur.
 * @ctx: Emitter state.
 * @ref: Reference of the return.
 *
 * All returns share one epilogue: inline at the end of the last block if that
 * is a return, otherwise in the trailer.
 */
static void emit_return(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    if (ir_a(insn) != IR_NIL) {
        load_val(ctx, ir_a(insn), RV_A0, RV_A1);
    }
    if (ctx->next != IR_NIL) {
        jump(ctx, &ctx->ret);
        return;
    }
    aotw_bind(ctx->w, &ctx->ret);
    epilogue(ctx);
}

/**
 * jump_target() - Final target of a jump to a block.
 * @ctx: Emitter state.
 * @blk: The block.
 *
 * Follows the blocks that only jump on without any copy (thread_blocks()).
 *
 * Return: The block to jump to.
 */
static ir_blk jump_target(em_t *ctx, ir_blk blk)
{
    for (unsigned count = 0; ctx->thread[blk] != blk && count < ctx->f->nblocks;
         count++) {
        blk = ctx->thread[blk];
    }
    return blk;
}

/**
 * blk_chain() - Fix-up chain of the final target of a jump.
 * @ctx: Emitter state.
 * @blk: The block.
 *
 * Return: The chain of jump_target(@blk).
 */
static uint32_t *blk_chain(em_t *ctx, ir_blk blk)
{
    return &ctx->blk[jump_target(ctx, blk)];
}

/**
 * goto_block() - Jump from one block to another, with the phi copies.
 * @ctx: Emitter state.
 * @from: Source block.
 * @dest: Target block.
 * @next: The block whose code follows, or IR_NIL.
 *
 * No jump if the target is the next block.
 */
static void goto_block(em_t *ctx, ir_blk from, ir_blk dest, ir_blk next)
{
    edge_copies(ctx, (cfg_edge){.from = from, .to = dest});
    ir_blk target = jump_target(ctx, dest);
    if (dest != next && target != next) {
        jump(ctx, &ctx->blk[target]);
    }
}

/**
 * cond_branch() - Branch for the condition of a br_if.
 * @ctx: Emitter state.
 * @val: The i32 condition value.
 *
 * The comparison itself if it was fused into the br_if (scan_func()), else
 * @val != 0.
 *
 * Return: A B-type branch over the next instruction, taken when @val is true.
 */
static uint32_t cond_branch(em_t *ctx, ir_ref val)
{
    if (val == IR_NIL || !(ctx->flags[val] & RA_FOLDED)) {
        return rv_bne(rd_lo(ctx, val, RV_T0), RV_ZERO, rv_skip(1));
    }
    ir_insn insn = ctx->f->insn[val];
    unsigned opcode = ir_op(insn);
    unsigned src = rd_lo(ctx, ir_a(insn), RV_T0);
    if (opcode == IR_OP_EQZ) {
        return rv_beq(src, RV_ZERO, rv_skip(1));
    }
    unsigned val2 = rd_lo(ctx, ir_b(insn), RV_T2);
    if (opcode == IR_OP_EQ || opcode == IR_OP_NE) {
        return opcode == IR_OP_EQ ? rv_beq(src, val2, rv_skip(1))
                                  : rv_bne(src, val2, rv_skip(1));
    }
    cmp_t cmp = cmp_form(opcode);
    unsigned len = cmp.swap ? val2 : src;
    unsigned reg = cmp.swap ? src : val2;
    if (cmp.inv) {
        return cmp.uns ? rv_bgeu(len, reg, rv_skip(1))
                       : rv_bge(len, reg, rv_skip(1));
    }
    return cmp.uns ? rv_bltu(len, reg, rv_skip(1))
                   : rv_blt(len, reg, rv_skip(1));
}

/**
 * ifstub_target() - Target of the copy stub of a block's br_if.
 * @ctx: Emitter state.
 * @blk: The block.
 *
 * See emit_br_if().
 *
 * Return: The successor reached through the stub.
 */
static ir_blk ifstub_target(em_t *ctx, ir_blk blk)
{
    ir_insn insn = ctx->f->insn[ctx->f->blocks[blk].term];
    return needs_copies(ctx, (cfg_edge){.from = blk, .to = ir_b(insn)})
               ? ir_b(insn)
               : ir_c(insn);
}

/**
 * emit_br_if() - Emit the br_if that ends block ctx->cur.
 * @ctx: Emitter state.
 * @ref: Reference of the br_if.
 *
 * A conditional jump for the taken edge, the fall-through edge inline. A taken
 * edge with phi copies jumps to a stub in the trailer (critical edge).
 */
static void emit_br_if(em_t *ctx, ir_ref ref)
{
    const ir_blk cur = ctx->cur;
    const ir_blk next = ctx->next;
    ir_insn insn = ctx->f->insn[ref];
    ir_blk taken = ir_b(insn);
    const cfg_edge taken_edge = {.from = cur, .to = taken};
    ir_blk other = ir_c(insn);
    uint32_t branch = cond_branch(ctx, ir_a(insn));
    if ((taken == next || jump_target(ctx, taken) == next) &&
        !needs_copies(ctx, taken_edge)) { /* inverse branch, fall into it */
        branch = rv_branch_invert(branch);
        taken = ir_c(insn);
        other = ir_b(insn);
    }
    cond_jump(ctx, branch,
              needs_copies(ctx, taken_edge) ? &ctx->ifstub[cur]
                                            : blk_chain(ctx, taken));
    goto_block(ctx, cur, other, next);
}

/**
 * table_chain() - Fix-up chain of a br_table edge.
 * @ctx: Emitter state.
 * @cur: The block being emitted.
 * @target: The target block.
 *
 * Return: The chain of its copy stub, or of the block.
 */
static uint32_t *table_chain(em_t *ctx, ir_blk cur, ir_blk target)
{
    return needs_copies(ctx, (cfg_edge){.from = cur, .to = target})
               ? &ctx->edge_stub[target]
               : blk_chain(ctx, target);
}

/**
 * emit_br_table() - Emit the br_table that ends block ctx->cur.
 * @ctx: Emitter state.
 * @ref: Reference of the br_table.
 *
 * Bounds check to the default, then a jump table of jals (auipc-relative), one
 * per label. Edges with phi copies go through stubs, one per distinct target.
 */
static void emit_br_table(em_t *ctx, ir_ref ref)
{
    const ir_blk cur = ctx->cur;
    ir_insn insn = ctx->f->insn[ref];
    ir_func *func = ctx->f;
    unsigned count = (unsigned)ir_c(insn) + 1U; /* targets, default last */

    unsigned src = rd_lo(ctx, ir_a(insn), RV_T0);
    li(ctx, RV_T1, count - 1U);
    cond_jump(ctx, rv_bgeu(src, RV_T1, rv_skip(1)),
              table_chain(ctx, cur, ir_succ(func, cur, count - 1U)));
    emit(ctx, rv_slli(RV_T0, src, 2));
    emit(ctx, rv_auipc(RV_T1, 0));
    emit(ctx, rv_add(RV_T1, RV_T1, RV_T0));
    /* the table starts after this auipc, add and jalr */
    emit(ctx, rv_jalr(RV_ZERO, RV_T1, 3 * RV_INSN_BYTES));
    for (unsigned k = 0; k + 1U < count; k++) {
        jump(ctx, table_chain(ctx, cur, ir_succ(func, cur, k)));
    }
    for (unsigned k = 0; k < count; k++) {
        ir_blk target = ir_succ(func, cur, k);
        if (needs_copies(ctx, (cfg_edge){.from = cur, .to = target}) &&
            chain_pending(ctx->edge_stub[target])) {
            aotw_bind(ctx->w, &ctx->edge_stub[target]);
            goto_block(ctx, cur, target, IR_NIL);
        }
    }
    for (unsigned k = 0; k < count; k++) {
        aotw_chain_init(&ctx->edge_stub[ir_succ(func, cur, k)]);
    }
}

/**
 * emit_param() - Store a parameter to its location on entry.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * All PARAMs are at the start of the entry block, before anything clobbers
 * a1-a7.
 */
static void emit_param(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    abi_cursor next = ABI_CURSOR_START;
    place_t place = {-1, -1, 0, 0};
    for (unsigned k = 0; k <= ir_a(insn) && k < ctx->ft.param_count; k++) {
        place = abi_place(ctx->ft.params[k], &next);
    }
    unsigned reg_lo = place_lo(place);
    unsigned reg_hi = place_hi(place);
    if (place.reg_lo < 0) {
        lw_at(ctx, reg_lo, RV_SP, ctx->frame + place.stk_lo);
    }
    if (is_wide(ir_ty(insn)) && place.reg_hi < 0 && !is_narrow(ctx, ref)) {
        lw_at(ctx, reg_hi, RV_SP, ctx->frame + place.stk_hi);
    }
    store_val(ctx, ref, reg_lo, reg_hi);
}

/**
 * emit_insn() - Emit one instruction of block ctx->cur.
 * @ctx: Emitter state.
 * @ref: Reference of the instruction.
 *
 * Constants and comparisons fused into a br_if emit nothing here.
 */
static void emit_insn(em_t *ctx, ir_ref ref)
{
    ir_insn insn = ctx->f->insn[ref];
    unsigned opcode = ir_op(insn);
    if (!emits_code(opcode) || (ctx->flags[ref] & RA_FOLDED)) {
        return; /* a folded comparison is emitted by its br_if */
    }
    switch (opcode) {
    case IR_OP_PARAM:
        emit_param(ctx, ref);
        return;
    case IR_OP_SELECT:
        emit_select(ctx, ref);
        return;
    case IR_OP_MEM_SIZE:
        emit(ctx, rv_lw(RV_A2, RV_S2, WAMR_INST_MEM_CUR_PAGE_COUNT));
        store_val(ctx, ref, RV_A2, RV_A3);
        return;
    case IR_OP_MEM_GROW:
        emit_mem_grow(ctx, ref);
        return;
    case IR_OP_GLOBAL_GET:
    case IR_OP_GLOBAL_SET:
        emit_global(ctx, ref);
        return;
    case IR_OP_CALL:
        emit_call(ctx, ref);
        return;
    case IR_OP_CALL_INDIRECT:
        emit_call_indirect(ctx, ref);
        return;
    case IR_OP_BR:
        goto_block(ctx, ctx->cur, ir_b(insn), ctx->next);
        return;
    case IR_OP_BR_IF:
        emit_br_if(ctx, ref);
        return;
    case IR_OP_BR_TABLE:
        emit_br_table(ctx, ref);
        return;
    case IR_OP_RETURN:
        emit_return(ctx, ref);
        return;
    case IR_OP_UNREACHABLE:
        jump_trap(ctx, WAMR_EXCE_UNREACHABLE);
        return;
    default:
        break;
    }
    if (in_range(opcode, IR_OP_LOAD, IR_OP_LOAD32_U)) {
        emit_load(ctx, ref);
    } else if (in_range(opcode, IR_OP_STORE, IR_OP_STORE32)) {
        emit_store(ctx, ref);
    } else if (ir_op_vmask(opcode) == IR_M_A) {
        emit_unary(ctx, ref);
    } else if (ir_op_vmask(opcode) == IR_M_AB) {
        emit_binary(ctx, ref);
    } else {
        unsupported(ctx);
    }
}

/* ------------------------------------------------------------------------ */
/* Function body                                                            */
/* ------------------------------------------------------------------------ */

/**
 * emit_trailer() - Emit the code shared by the whole function.
 * @ctx: Emitter state.
 *
 * After the blocks: the br_if copy stubs, the trap stubs (li a1, id; j raise),
 * raise, the return-zero path and the shared epilogue.
 */
static void emit_trailer(em_t *ctx)
{
    ir_func *func = ctx->f;
    for (unsigned k = 0; k < func->nlayout; k++) {
        ir_blk blk = func->layout[k];
        if (ctx->ifstub[blk] != AOTW_CHAIN_EMPTY) {
            aotw_bind(ctx->w, &ctx->ifstub[blk]);
            goto_block(ctx, blk, ifstub_target(ctx, blk), IR_NIL);
        }
    }
    int any_trap = 0;
    for (unsigned id = 0; id < NTRAPS; id++) {
        if (ctx->trap[id] != AOTW_CHAIN_EMPTY) {
            any_trap = 1;
            aotw_bind(ctx->w, &ctx->trap[id]);
            emit(ctx, rv_addi(RV_A1, RV_ZERO, (int32_t)id));
            jump(ctx, &ctx->raise);
        }
    }
    if (any_trap) { /* falls into ret0 */
        aotw_bind(ctx->w, &ctx->raise);
        emit(ctx, rv_mv(RV_A0, RV_S2));
        call_rt(ctx, WAMR_SYM_SET_EXCEPTION_WITH_ID);
    }
    if (any_trap || ctx->ret0 != AOTW_CHAIN_EMPTY) {
        aotw_bind(ctx->w, &ctx->ret0);
        emit(ctx, rv_mv(RV_A0, RV_ZERO));
        emit(ctx, rv_mv(RV_A1, RV_ZERO));
        if (!(ctx->ret &
              AOTW_CHAIN_PENDING)) { /* inline after the last block */
            jump(ctx, &ctx->ret);
            return;
        }
    } else if (!chain_pending(ctx->ret)) {
        return; /* no jump to a shared epilogue */
    }
    aotw_bind(ctx->w, &ctx->ret);
    epilogue(ctx);
}

/**
 * has_body() - Check whether a block emits code before its terminator.
 * @func: The function.
 * @blk: The block.
 *
 * Return: 1 if it does, else 0.
 */
static int has_body(const ir_func *func, ir_blk blk)
{
    for (ir_ref i = func->blocks[blk].first; i < func->blocks[blk].term;
         i += (ir_ref)ir_insn_len(func->insn[i])) {
        if (emits_code(ir_op(func->insn[i]))) {
            return 1;
        }
    }
    return 0;
}

/**
 * thread_blocks() - Jump threading after register allocation.
 * @ctx: Emitter state.
 *
 * A block without phis whose code is a plain br T with no copies can be jumped
 * over (thread[b] = T); if T is also the next block with code, b emits nothing
 * (skip[b]).
 */
static void thread_blocks(em_t *ctx)
{
    ir_func *func = ctx->f;
    for (unsigned blk_id = 0; blk_id < func->nblocks; blk_id++) {
        ctx->thread[blk_id] = (uint16_t)blk_id;
        ctx->skip[blk_id] = 0;
    }
    for (unsigned k = 0; k < func->nlayout; k++) {
        ir_blk blk_id = func->layout[k];
        const ir_block *blk = &func->blocks[blk_id];
        if (blk->phi_head != IR_NIL || blk->term == IR_NIL ||
            ir_op(func->insn[blk->term]) != IR_OP_BR ||
            has_body(func, blk_id)) {
            continue;
        }
        ir_blk target = ir_b(func->insn[blk->term]);
        if (target != blk_id &&
            edge_is_nop(ctx, (cfg_edge){.from = blk_id, .to = target})) {
            ctx->thread[blk_id] = target;
        }
    }
    ir_blk nxt = IR_NIL; /* first block with code after the current one */
    for (unsigned k = func->nlayout; k-- > 0;) {
        ir_blk blk_id = func->layout[k];
        if (ctx->thread[blk_id] != blk_id && ctx->thread[blk_id] == nxt) {
            ctx->skip[blk_id] = 1;
        } else {
            nxt = blk_id;
        }
    }
}

/**
 * emit_body() - Emit the prologue, the blocks and the trailer.
 * @ctx: Emitter state.
 *
 * Run twice: a sizing pass, then for real.
 */
static void emit_body(em_t *ctx)
{
    ir_func *func = ctx->f;
    for (unsigned blk = 0; blk < func->nblocks; blk++) {
        aotw_chain_init(&ctx->blk[blk]);
        aotw_chain_init(&ctx->edge_stub[blk]);
        aotw_chain_init(&ctx->ifstub[blk]);
    }
    for (unsigned id = 0; id < NTRAPS; id++) {
        aotw_chain_init(&ctx->trap[id]);
    }
    aotw_chain_init(&ctx->raise);
    aotw_chain_init(&ctx->ret0);
    aotw_chain_init(&ctx->ret);

    prologue(ctx);
    for (unsigned k = 0; k < func->nlayout && !ctx->err && !ctx->w->err; k++) {
        ir_blk blk = func->layout[k];
        unsigned next_pos = k + 1U;
        while (next_pos < func->nlayout && ctx->skip[func->layout[next_pos]]) {
            next_pos++;
        }
        ctx->cur = blk;
        ctx->next = next_pos < func->nlayout ? func->layout[next_pos] : IR_NIL;
        aotw_bind(ctx->w, &ctx->blk[blk]);
        if (ctx->skip[blk]) {
            continue; /* its code would be a jump to the next block */
        }
        for (ir_ref i = func->blocks[blk].first;
             i < func->blocks[blk].end && !ctx->err;
             i += (ir_ref)ir_insn_len(func->insn[i])) {
            emit_insn(ctx, i);
        }
    }
    if (!ctx->err) {
        emit_trailer(ctx);
    }
}

/**
 * emit_func() - Emit the code of a defined function.
 * @func: Its finalized IR.
 * @mod: The decoded module.
 * @funcidx: Index of the function.
 * @ftype: Its type.
 * @writer: The AOT writer; the code is its next function.
 * @opt: Compile options.
 *
 * Uses the scratch region of @func, released first (the builder's data is no
 * longer needed). Two passes: a sizing pass with long branches, then the real
 * one, with short branches if the function is below 4 KiB.
 *
 * Return: WASMC_OK, WASMC_ERR_UNSUPPORTED for constructs not implemented yet,
 *   WASMC_ERR_OUT_OF_MEMORY if a scratch chunk could not be allocated, or an
 *   output error from the writer.
 */
wasmc_err_t emit_func(ir_func *func, const wdec_module_t *mod, uint32_t funcidx,
                      const wdec_functype_t *ftype, aotw_t *writer,
                      const wasmc_options *opt)
{
    em_t ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.f = func;
    ctx.w = writer;
    ctx.m = mod;
    ctx.funcidx = funcidx;
    ctx.bounds = !opt->no_bounds_checks;
    ctx.aux_check = !opt->no_aux_stack_check;
    ctx.ft = *ftype;

    /* the builder's scratch data is dead: reuse the whole scratch region */
    ir_scratch_release(func, 0);
    ir_scratch_reserve(func, ir_nslots(func));
    ctx.flags = ir_scratch_alloc(func, IR_ARRAY(uint8_t, ir_nslots(func)));
    if (!ctx.flags) {
        return ir_scratch_err(func);
    }
    wasmc_err_t err = scan_func(&ctx);
    if (err) {
        return err;
    }

    /* one chunk for what stays allocated while the code is emitted (and
     * the output grows): the allocator's results and the emitter's arrays.
     * The allocator's temporaries use the arrays' room if they fit, or a
     * chunk of their own, freed before the code is emitted. */
    const uint32_t nblocks = func->nblocks + 1U;
    enum {
        ARR_BLK,
        ARR_EDGE_STUB,
        ARR_MOVES,
        ARR_THREAD,
        ARR_IFSTUB,
        ARR_SKIP,
        ARR_COUNT
    };
    const ir_layout arrays[ARR_COUNT] = {
        [ARR_BLK] = IR_ARRAY(uint32_t, nblocks),
        [ARR_EDGE_STUB] = IR_ARRAY(uint32_t, nblocks),
        [ARR_MOVES] = {.size = (ctx.max_moves * (uint32_t)sizeof(pmove_t)) + 4U,
                       .align = 4},
        [ARR_THREAD] = IR_ARRAY(uint16_t, nblocks),
        [ARR_IFSTUB] = IR_ARRAY(uint32_t, nblocks),
        [ARR_SKIP] = IR_ARRAY(uint8_t, nblocks),
    };
    ir_scratch_reserve(func, ra_result_bytes(func) +
                                 ir_scratch_bytes(arrays, ARR_COUNT) + MEM_PAD);
    err = layout_frame(&ctx);
    if (err) {
        return err;
    }
    ctx.blk = ir_scratch_alloc(func, arrays[ARR_BLK]);
    ctx.edge_stub = ir_scratch_alloc(func, arrays[ARR_EDGE_STUB]);
    ctx.moves = ir_scratch_alloc(func, arrays[ARR_MOVES]);
    ctx.thread = ir_scratch_alloc(func, arrays[ARR_THREAD]);
    ctx.ifstub = ir_scratch_alloc(func, arrays[ARR_IFSTUB]);
    ctx.skip = ir_scratch_alloc(func, arrays[ARR_SKIP]);
    if (!ctx.blk || !ctx.edge_stub || !ctx.moves || !ctx.thread ||
        !ctx.ifstub || !ctx.skip) {
        return ir_scratch_err(func);
    }
    thread_blocks(&ctx);

    /* sizing pass with long branches: below 4 KiB every
     * conditional branch of the function reaches its target */
    aotw_dry_begin(writer);
    emit_body(&ctx);
    uint32_t size = aotw_dry_end(writer);
    if (ctx.err) {
        return ctx.err;
    }
    ctx.short_br = size < RV_BRANCH_REACH && !opt->long_branches;

    aotw_func_begin(writer);
    emit_body(&ctx);
    if (ctx.err) {
        return ctx.err;
    }
    return writer->err;
}
