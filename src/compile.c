/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * compile.c - WASM function body -> IR (interface and contract: compile.h).
 *
 * The body is walked once, in order, with a model of the WASM operand
 * stack holding IR values (ir_ref) and a control stack of frames for the
 * open block/loop/if constructs. Overview:
 *
 *   - Blocks: a new IR block is made only where control forks or merges:
 *     the join of a `block`, the header of a `loop`, and the then/else/join
 *     of an `if`. A loop's `end` needs no block (nothing branches there,
 *     only to the loop's start), and entering a `block` is not a branch.
 *
 *   - Dead code: after br/br_table/return/unreachable (and after an `end`
 *     nothing reaches) &wc_ctx.dead is set until the next reachable point.
 *     Dead code is still walked, to keep the stacks in step, but emits
 *     nothing: popping below the frame's entry height gives IR_NIL, as
 *     WASM's polymorphic stack rule allows in unreachable code.
 *
 *   - Locals become SSA values on the fly (ssa_builder.h, Braun et al.):
 *     local.set/tee write a local in the current block, local.get reads it,
 *     a `loop` opens its header and the loop's `end` seals it. A local no
 *     local.set/tee writes keeps its entry value and skips the builder.
 *
 *   - Block/if results do not go through the locals machinery: every
 *     branch records on its CFG edges the value it carries (edge_val), and
 *     the join makes the phi of those values at its `end`, once every
 *     predecessor is known.
 *
 * All bookkeeping lives in the scratch of the output ir_func: fixed arrays
 * allocated once, the builder's included, then only the builder's
 * definition nodes, all in scratch chunks as they are needed (ir_init()).
 */
#include "compile.h"
#include "bitmap.h"
#include "ir.h"
#include "ssa_builder.h"
#include "wasm_decode.h"
#include "wasmc/error.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Types                                                                    */
/* ------------------------------------------------------------------------ */

/**
 * struct wc_frame - An open block, loop or if.
 * @opcode: %WDEC_OP_BLOCK, %WDEC_OP_LOOP or %WDEC_OP_IF.
 * @end_type: the construct's result type: %WDEC_TYPE_EMPTY or a valtype.
 * @else_seen: IF: its `else` has been translated.
 * @live: the construct was entered from reachable code (for an IF: its
 *     br_if was emitted, so its else arm is reachable too).
 * @height: operand-stack height at entry (after popping an if's
 *     condition); popping below it gives IR_NIL.
 * @body: LOOP: the header, target of its branches; IF: the then block;
 *     BLOCK: unused (IR_NIL), the body continues the current block.
 * @else_body: IF: the else block; otherwise unused (IR_NIL).
 * @join: BLOCK/IF: the merge block, target of its branches; LOOP: unused
 *     (IR_NIL), nothing branches to a loop's end.
 */
typedef struct wc_frame {
    uint8_t opcode;
    uint8_t end_type;
    bool else_seen;
    bool live;
    uint32_t height;
    ir_blk body;
    ir_blk else_body;
    ir_blk join;
} wc_frame;

/**
 * struct wc_region - The SSA region (ssa_region_open()) of an open block
 * or if.
 * @head: the block it was entered from.
 * @begin: its begin event.
 */
typedef struct wc_region {
    ir_blk head;
    uint32_t begin;
} wc_region;

/**
 * define REGIONS_MIN_WRITTEN - The fewest written locals that make the SSA
 * builder's regions worth their memory.
 *
 * Without regions a read leaves a definition in every join it goes
 * through, at most one per written local: with few of them that is less
 * than what the regions take (a write log and a record per block/if).
 */
#define REGIONS_MIN_WRITTEN 8U

/**
 * struct wc_ctx - The state of one function's translation.
 * @mod: the module.
 * @ir: the IR being built.
 * @err: the first error; once set, translation stops.
 * @dead: the current point is unreachable: nothing is emitted and no local
 *     is defined.
 * @result_ty: IR type of the function's result (IR_T_VOID if none), for
 *     `return` and falling off the end.
 * @opd: the WASM operand stack, holding IR values.
 * @opd_top: its height.
 * @opd_cap: its capacity.
 * @ctrl: the stack of open blocks/loops/ifs (the function body itself has
 *     no frame).
 * @ctrl_top: its depth.
 * @ctrl_cap: its capacity.
 * @nlocals: parameters + declared locals.
 * @local_ty: [nlocals] the IR type (IR_T_*) of each local.
 * @written: bitmap of the locals some local.set/tee writes; only those go
 *     through the SSA builder.
 * @init_val: [nlocals] the entry value of each local (its param, or zero).
 * @ssa: the SSA construction of the written locals.
 * @edge_val: [max_edges] the block/if result each edge carries (IR_NIL:
 *     none), or NULL if no block/if of the function has a result.
 * @zero: per IR type, the shared zero constant, IR_NIL until made.
 * @rgn: [ctrl capacity] per open frame, its block/if's SSA region; NULL
 *     if the builder has no regions (setup_regions()).
 */
typedef struct wc_ctx {
    const wdec_module_t *mod;
    ir_func *ir;
    wasmc_err_t err;
    bool dead;
    unsigned result_ty;

    ir_ref *opd;
    uint32_t opd_top, opd_cap;
    wc_frame *ctrl;
    uint32_t ctrl_top, ctrl_cap;

    uint32_t nlocals;
    uint8_t *local_ty;
    uint8_t *written;
    ir_ref *init_val;

    ssa_builder ssa;
    ir_ref *edge_val;

    ir_ref zero[IR_T_COUNT];
    wc_region *rgn;
} wc_ctx;

/* ------------------------------------------------------------------------ */
/* Small helpers                                                            */
/* ------------------------------------------------------------------------ */

/**
 * read_le() - Read a little-endian unsigned integer.
 * @ptr: its bytes (wdec hands back the raw bytes of f32/f64 constants).
 * @size: its size in bytes, at most 8.
 *
 * Return: the integer.
 */
static uint64_t read_le(const uint8_t *ptr, unsigned size)
{
    uint64_t val = 0;
    while (size--) {
        val = val << CHAR_BIT | ptr[size];
    }
    return val;
}

/**
 * ir_type_of() - The IR type of a WASM value type.
 * @type: a valtype, or %WDEC_TYPE_EMPTY.
 *
 * Return: the IR_T_* type; IR_T_VOID for %WDEC_TYPE_EMPTY (or anything
 * unexpected).
 */
static unsigned ir_type_of(wdec_valtype_t type)
{
    switch (type) {
    case WDEC_TYPE_I32:
        return IR_T_I32;
    case WDEC_TYPE_I64:
        return IR_T_I64;
    case WDEC_TYPE_F32:
        return IR_T_F32;
    case WDEC_TYPE_F64:
        return IR_T_F64;
    default:
        return IR_T_VOID;
    }
}

/**
 * func_result_type() - The IR type of a function type's result.
 * @ftype: the function type (WASM 1.0: at most one result).
 *
 * Return: the IR_T_* type of the result, IR_T_VOID if there is none.
 */
static unsigned func_result_type(const wdec_functype_t *ftype)
{
    return ftype->result_count ? ir_type_of(ftype->results[0]) : IR_T_VOID;
}

/* ------------------------------------------------------------------------ */
/* Operand stack                                                            */
/* ------------------------------------------------------------------------ */

/**
 * cur_floor() - The lowest operand-stack height the current frame may pop.
 * @ctx: the translation.
 *
 * Return: the entry height of the innermost frame, 0 in the function body.
 */
static uint32_t cur_floor(const wc_ctx *ctx)
{
    return ctx->ctrl_top ? ctx->ctrl[ctx->ctrl_top - 1].height : 0;
}

/**
 * push_opd() - Push a value on the operand stack.
 * @ctx: the translation.
 * @val: the IR value (IR_NIL in dead code).
 *
 * Sets %WASMC_ERR_OPD_STACK_OVERFLOW if the stack is full.
 */
static void push_opd(wc_ctx *ctx, ir_ref val)
{
    if (ctx->opd_top >= ctx->opd_cap) {
        ctx->err = WASMC_ERR_OPD_STACK_OVERFLOW;
        return;
    }
    ctx->opd[ctx->opd_top++] = val;
}

/**
 * pop_opd() - Pop a value from the operand stack.
 * @ctx: the translation.
 *
 * Popping below the current frame's entry height only happens in dead
 * code, where WASM's stack is polymorphic: it gives IR_NIL rather than
 * underflowing, and no real value is ever needed there.
 *
 * Return: the value, or IR_NIL.
 */
static ir_ref pop_opd(wc_ctx *ctx)
{
    if (ctx->opd_top > cur_floor(ctx)) {
        return ctx->opd[--ctx->opd_top];
    }
    return IR_NIL;
}

/**
 * get_zero() - The zero constant of a type, shared by the whole function.
 * @ctx: the translation.
 * @type: an IR_T_* value type.
 *
 * Return: the constant, emitted on first use.
 */
static ir_ref get_zero(wc_ctx *ctx, unsigned type)
{
    if (ctx->zero[type] == IR_NIL) {
        if (type == IR_T_I64) {
            ctx->zero[type] = ir_emit_i64(ctx->ir, 0);
        } else if (type == IR_T_F64) {
            ctx->zero[type] = ir_emit_f64(ctx->ir, 0);
        } else {
            ctx->zero[type] = ir_emit_const32(ctx->ir, type, 0);
        }
    }
    return ctx->zero[type];
}

/* ------------------------------------------------------------------------ */
/* Locals                                                                   */
/* ------------------------------------------------------------------------ */

/**
 * is_written() - Whether some local.set/tee of the function writes a local.
 * @ctx: the translation.
 * @var: index of the local.
 *
 * Return: true if written; a local never written keeps its entry value.
 */
static bool is_written(const wc_ctx *ctx, uint16_t var)
{
    return bitmap_test(ctx->written, var);
}

/**
 * local_site() - The local of a local.get/set/tee, in the block being built.
 * @ctx: the translation.
 * @ins: the instruction.
 *
 * Return: the site.
 */
static ssa_site local_site(const wc_ctx *ctx, const wdec_instr_t *ins)
{
    return (ssa_site){.var = (uint16_t)ins->imm.index, .blk = ctx->ir->cur};
}

/**
 * read_local() - The value of a local in the block being built.
 * @ctx: the translation.
 * @site: the local and the block.
 *
 * A local that is never written keeps its entry value everywhere: no
 * walk, no definitions (measure.c counts phis for written locals only).
 * The others are read through the SSA builder, which remembers the result
 * in the block for later reads there.
 *
 * Return: the value, or IR_NIL if the IR ran out of capacity
 * (&wc_ctx.err is set).
 */
static ir_ref read_local(wc_ctx *ctx, ssa_site site)
{
    if (!is_written(ctx, site.var)) {
        return ctx->init_val[site.var];
    }
    return ssa_read(&ctx->ssa, site, true);
}

/* ------------------------------------------------------------------------ */
/* Branches and block/if results                                            */
/* ------------------------------------------------------------------------ */

/**
 * tag_edges() - Record the value the edges of a terminator carry.
 * @ctx: the translation.
 * @first: the IR's edge count before the terminator was emitted.
 * @value: the block/if result carried, IR_NIL if none.
 *
 * In WASM 1.0 all the targets of one branch that take a value take the
 * same one; an edge into a block without a result (a loop header, a br_if
 * continuation, a return trampoline) carries a value nobody reads.
 */
static void tag_edges(wc_ctx *ctx, uint32_t first, ir_ref value)
{
    if (!ctx->edge_val) { /* no block/if of the function has a result */
        return;
    }
    for (uint32_t edge = first; edge < ctx->ir->nedges; edge++) {
        ctx->edge_val[edge] = value;
    }
}

/**
 * resolve_result() - The result of a block/if at its join.
 * @ctx: the translation.
 * @join: the join, whose predecessors are all known.
 * @type: IR type of the result.
 *
 * Return: the value on the join's only incoming edge, or a phi of the
 * values on all of them, in predecessor order (the phi's trivial value if
 * it has one).
 */
static ir_ref resolve_result(wc_ctx *ctx, ir_blk join, unsigned type)
{
    if (ctx->ir->blocks[join].npreds == 1) {
        return ctx->edge_val[ctx->ir->blocks[join].pred_head];
    }
    ir_ref phi = ir_phi_new(ctx->ir, join, type);
    ir_phi_alloc_args(ctx->ir, (ir_block_phi){.blk = join, .phi = phi});
    unsigned pred = 0;
    uint16_t edge;
    IR_FOR_EACH_PRED(ctx->ir, join, edge)
    {
        ir_phi_set_arg(ctx->ir, (ir_phi_slot){.phi = phi, .pred = pred++},
                       ctx->edge_val[edge]);
    }
    ir_ref trivial = ir_phi_try_trivial_reclaim(ctx->ir, phi, join);
    return trivial != IR_NIL ? trivial : phi;
}

/* ------------------------------------------------------------------------ */
/* Control frames                                                           */
/* ------------------------------------------------------------------------ */

/**
 * push_frame() - Enter a block, loop or if.
 * @ctx: the translation.
 * @frame: its opcode, result type and blocks, copied onto the control
 *     stack; the entry height and liveness are taken from the current point.
 */
static void push_frame(wc_ctx *ctx, const wc_frame *frame)
{
    if (ctx->ctrl_top >= ctx->ctrl_cap) {
        ctx->err = WASMC_ERR_CTRL_STACK_OVERFLOW;
        return;
    }
    wc_frame *top = &ctx->ctrl[ctx->ctrl_top++];
    *top = *frame;
    top->else_seen = false;
    top->live = !ctx->dead;
    top->height = ctx->opd_top;
}

/**
 * top_frame() - The innermost open frame.
 * @ctx: the translation; at least one frame is open.
 *
 * Return: the frame.
 */
static wc_frame *top_frame(wc_ctx *ctx)
{
    return &ctx->ctrl[ctx->ctrl_top - 1];
}

/**
 * label_frame() - The frame a branch label names.
 * @ctx: the translation.
 * @label: 0 for the innermost frame, 1 for the next, ...; the depth of the
 *     control stack names the function itself.
 *
 * Return: the frame, or NULL for the function itself (there is no frame
 * for it: a branch to it is a return).
 */
static wc_frame *label_frame(wc_ctx *ctx, uint32_t label)
{
    return label < ctx->ctrl_top ? &ctx->ctrl[ctx->ctrl_top - 1 - label] : NULL;
}

/**
 * frame_target() - Where a branch to a frame goes.
 * @frame: the frame.
 *
 * Return: a loop's header, a block's or if's join.
 */
static ir_blk frame_target(const wc_frame *frame)
{
    return frame->opcode == WDEC_OP_LOOP ? frame->body : frame->join;
}

/**
 * br_to_frame() - Branch from the current block to a frame's target.
 * @ctx: the translation.
 * @frame: the frame: a loop's header, a block's or if's join.
 * @value: the block/if result carried, IR_NIL if none.
 */
static void br_to_frame(wc_ctx *ctx, const wc_frame *frame, ir_ref value)
{
    const uint32_t first = ctx->ir->nedges;
    ir_emit_br(ctx->ir, frame_target(frame));
    tag_edges(ctx, first, value);
}

/**
 * frame_has_result() - Whether a construct has a result.
 * @frame: the frame.
 *
 * Return: true if its result type is not empty.
 */
static bool frame_has_result(const wc_frame *frame)
{
    return frame->end_type != WDEC_TYPE_EMPTY;
}

/**
 * branch_takes_value() - Whether a branch to a label carries a value.
 * @ctx: the translation.
 * @frame: the label's frame, or NULL for the function itself.
 *
 * A branch to a loop goes to its start and takes no value; to a block or
 * if, it takes the construct's result; out of the function (a return),
 * the function's result.
 *
 * Return: true if a value is carried.
 */
static bool branch_takes_value(const wc_ctx *ctx, const wc_frame *frame)
{
    if (!frame) {
        return ctx->result_ty != IR_T_VOID;
    }
    return frame->opcode != WDEC_OP_LOOP && frame_has_result(frame);
}

/**
 * pop_result() - Pop the result an arm of a construct ends with.
 * @ctx: the translation.
 * @frame: the construct.
 *
 * Return: the result, IR_NIL if the construct has none.
 */
static ir_ref pop_result(wc_ctx *ctx, const wc_frame *frame)
{
    return frame_has_result(frame) ? pop_opd(ctx) : IR_NIL;
}

/* ------------------------------------------------------------------------ */
/* Control instructions                                                     */
/* ------------------------------------------------------------------------ */

/**
 * open_region() - Open the SSA region of the block or if just pushed.
 * @ctx: the translation.
 * @head: the block it is entered from (unused if entered from dead code).
 */
static void open_region(wc_ctx *ctx, ir_blk head)
{
    if (ctx->rgn && !ctx->err) {
        ctx->rgn[ctx->ctrl_top - 1U] = (wc_region){
            .head = head,
            .begin = ssa_region_open(&ctx->ssa),
        };
    }
}

/**
 * do_block() - Translate `block`: its body continues the current block.
 * @ctx: the translation.
 * @blocktype: its result type.
 */
static void do_block(wc_ctx *ctx, uint8_t blocktype)
{
    ir_blk join = ir_block_new(ctx->ir);
    const wc_frame frame = {
        .opcode = WDEC_OP_BLOCK,
        .end_type = blocktype,
        .body = IR_NIL,
        .else_body = IR_NIL,
        .join = join,
    };
    push_frame(ctx, &frame);
    open_region(ctx, ctx->ir->cur);
}

/**
 * scan_loop_writes() - The locals a loop's body writes.
 * @ctx: the translation.
 * @iter: the body's instructions, from just after the `loop` opcode (a
 *     copy: the caller's iterator does not move).
 * @wset: the cleared bitset to fill, one bit per local.
 *
 * Scans to the matching `end`. On a malformed body every local is assumed
 * written.
 */
static void scan_loop_writes(const wc_ctx *ctx, wdec_instr_iter_t iter,
                             uint8_t *wset)
{
    wdec_instr_t ins;
    uint32_t depth = 0;
    while (wdec_instr_iter_next(&iter, &ins)) {
        switch (ins.opcode) {
        case WDEC_OP_BLOCK:
        case WDEC_OP_LOOP:
        case WDEC_OP_IF:
            depth++;
            break;
        case WDEC_OP_END:
            if (!depth--) {
                return;
            }
            break;
        case WDEC_OP_LOCAL_SET:
        case WDEC_OP_LOCAL_TEE:
            if (ins.imm.index < ctx->nlocals) {
                bitmap_set(wset, ins.imm.index);
            }
            break;
        default:
            break;
        }
    }
    for (uint32_t var = 0; var < ctx->nlocals; var++) {
        bitmap_set(wset, var);
    }
}

/**
 * do_loop() - Translate `loop`: branch into a new header, left open.
 * @ctx: the translation.
 * @blocktype: its result type.
 * @iter: the instructions, just after the `loop` opcode.
 *
 * The header stays unsealed until the loop's `end`. If the SSA builder
 * hands out a write set for it, it gets the locals the body writes.
 */
static void do_loop(wc_ctx *ctx, uint8_t blocktype,
                    const wdec_instr_iter_t *iter)
{
    ir_blk header = ir_block_new(ctx->ir);
    if (!ctx->dead) {
        ir_emit_br(ctx->ir, header);
        ir_block_begin(ctx->ir, header);
    }
    uint8_t *wset = ssa_open_loop(&ctx->ssa, header);
    const wc_frame frame = {
        .opcode = WDEC_OP_LOOP,
        .end_type = blocktype,
        .body = header,
        .else_body = IR_NIL,
        .join = IR_NIL,
    };
    push_frame(ctx, &frame);
    if (wset) {
        scan_loop_writes(ctx, *iter, wset);
    }
}

/**
 * do_if() - Translate `if`: branch on the condition to then/else blocks.
 * @ctx: the translation.
 * @blocktype: its result type.
 */
static void do_if(wc_ctx *ctx, uint8_t blocktype)
{
    ir_ref cond = pop_opd(ctx);
    ir_blk then_blk = ir_block_new(ctx->ir);
    ir_blk else_blk = ir_block_new(ctx->ir);
    ir_blk join = ir_block_new(ctx->ir);
    const ir_blk head = ctx->ir->cur;
    if (!ctx->dead) {
        ir_emit_br_if(ctx->ir, cond, then_blk, else_blk);
        ir_block_begin(ctx->ir, then_blk);
    }
    const wc_frame frame = {
        .opcode = WDEC_OP_IF,
        .end_type = blocktype,
        .body = then_blk,
        .else_body = else_blk,
        .join = join,
    };
    push_frame(ctx, &frame);
    open_region(ctx, head);
}

/**
 * do_else() - Translate `else`: close the then arm, open the else arm.
 * @ctx: the translation.
 *
 * The else arm of an unreachable if is unreachable too: no block, no code.
 */
static void do_else(wc_ctx *ctx)
{
    wc_frame *frame = top_frame(ctx);
    if (!ctx->dead) {
        br_to_frame(ctx, frame, pop_result(ctx, frame));
    }
    if (frame->live) {
        ir_block_begin(ctx->ir, frame->else_body);
    }
    ctx->dead = !frame->live;
    frame->else_seen = true;
    ctx->opd_top = frame->height;
}

/**
 * end_loop() - Translate the `end` of a loop.
 * @ctx: the translation.
 * @frame: the loop.
 *
 * The header is sealed even if the end itself is unreachable (a loop that
 * ends in an unconditional br, very common): every branch to it has been
 * emitted by now. Code after the end continues in the current block.
 */
static void end_loop(wc_ctx *ctx, const wc_frame *frame)
{
    ssa_seal_loop(&ctx->ssa, frame->body);
    if (ctx->dead) {
        /* drop what dead code left; the result is never used */
        ctx->opd_top = frame->height;
        if (frame_has_result(frame)) {
            push_opd(ctx, IR_NIL);
        }
    }
}

/**
 * end_block() - Translate the `end` of a block or if.
 * @ctx: the translation.
 * @frame: the block or if.
 *
 * Closes the arm being translated with a branch to the join; an if without
 * `else` also gets its empty else arm (valid only without a result). The
 * join is reachable if anything branched to it; then the result is the
 * value its edges carry.
 */
static void end_block(wc_ctx *ctx, const wc_frame *frame)
{
    if (frame->opcode == WDEC_OP_IF && !frame->else_seen) {
        if (!ctx->dead) {
            br_to_frame(ctx, frame, IR_NIL);
        }
        if (frame->live) {
            ir_block_begin(ctx->ir, frame->else_body);
            br_to_frame(ctx, frame, IR_NIL);
        }
    } else if (!ctx->dead) {
        br_to_frame(ctx, frame, pop_result(ctx, frame));
    }
    ctx->opd_top = frame->height; /* dead code may have left values behind */

    ctx->dead = ctx->ir->blocks[frame->join].npreds == 0;
    if (!ctx->dead) {
        ir_block_begin(ctx->ir, frame->join);
    }
    if (frame_has_result(frame)) {
        push_opd(ctx, ctx->dead ? IR_NIL
                                : resolve_result(ctx, frame->join,
                                                 ir_type_of(frame->end_type)));
    }
}

/**
 * do_end() - Translate the `end` of a block, loop or if.
 * @ctx: the translation; at least one frame is open.
 */
static void do_end(wc_ctx *ctx)
{
    const wc_frame *frame = top_frame(ctx);
    if (frame->opcode == WDEC_OP_LOOP) {
        end_loop(ctx, frame);
    } else {
        end_block(ctx, frame);
        if (ctx->rgn) {
            /* a join nothing reaches is never read */
            const wc_region *region = &ctx->rgn[ctx->ctrl_top - 1U];
            ssa_region_close(&ctx->ssa, frame->join, region->head,
                             region->begin, frame->live && !ctx->dead);
        }
    }
    ctx->ctrl_top--;
}

/**
 * do_return() - Translate `return`, or the function body's own `end`.
 * @ctx: the translation.
 */
static void do_return(wc_ctx *ctx)
{
    ir_ref val = ctx->result_ty != IR_T_VOID ? pop_opd(ctx) : IR_NIL;
    if (!ctx->dead) {
        ir_emit_return(ctx->ir, val);
    }
    ctx->dead = true;
}

/**
 * do_br() - Translate `br`.
 * @ctx: the translation.
 * @label: the target label; out of the function, it is a return.
 */
static void do_br(wc_ctx *ctx, uint32_t label)
{
    const wc_frame *frame = label_frame(ctx, label);
    if (!frame) {
        do_return(ctx);
        return;
    }
    ir_ref val = branch_takes_value(ctx, frame) ? pop_opd(ctx) : IR_NIL;
    if (!ctx->dead) {
        br_to_frame(ctx, frame, val);
    }
    ctx->dead = true;
}

/**
 * do_br_if() - Translate `br_if`.
 * @ctx: the translation.
 * @label: the target label; out of the function, it is a conditional
 *     return, through a trampoline block that just returns.
 *
 * The value a taken branch carries stays on the stack for the fallthrough.
 */
static void do_br_if(wc_ctx *ctx, uint32_t label)
{
    const wc_frame *frame = label_frame(ctx, label);
    ir_ref cond = pop_opd(ctx);
    ir_ref val = IR_NIL;
    if (branch_takes_value(ctx, frame)) { /* peek */
        val = pop_opd(ctx);
        push_opd(ctx, val);
    }
    if (ctx->dead) {
        return;
    }

    if (frame) {
        ir_blk cont = ir_block_new(ctx->ir);
        const uint32_t first = ctx->ir->nedges;
        ir_emit_br_if(ctx->ir, cond, frame_target(frame), cont);
        tag_edges(ctx, first, val);
        ir_block_begin(ctx->ir, cont);
    } else {
        ir_blk ret_blk = ir_block_new(ctx->ir);
        ir_blk cont = ir_block_new(ctx->ir);
        ir_emit_br_if(ctx->ir, cond, ret_blk, cont);
        ir_block_begin(ctx->ir, ret_blk);
        ir_emit_return(ctx->ir, val);
        ir_block_begin(ctx->ir, cont);
    }
}

/**
 * label_target() - Where a branch to a label goes.
 * @ctx: the translation.
 * @label: the label.
 *
 * Return: the target block, or IR_NIL for the function itself.
 */
static ir_blk label_target(wc_ctx *ctx, uint32_t label)
{
    const wc_frame *frame = label_frame(ctx, label);
    return frame ? frame_target(frame) : IR_NIL;
}

/**
 * do_br_table() - Translate `br_table`.
 * @ctx: the translation.
 * @tbl: its labels and default label.
 *
 * The target list is built in the SSA builder's temporary buffer
 * (ssa_temp_refs()), which is free between its calls and long enough (each
 * target adds an edge, and it has max_edges + 1 entries). The labels that
 * leave the function share one trampoline block that returns.
 */
static void do_br_table(wc_ctx *ctx, const wdec_br_table_t *tbl)
{
    ir_ref idx = pop_opd(ctx);
    const wc_frame *dfr = label_frame(ctx, tbl->default_label);
    ir_ref val = branch_takes_value(ctx, dfr) ? pop_opd(ctx) : IR_NIL;
    if (ctx->dead) {
        return;
    }

    const unsigned count = tbl->count + 1U; /* the default is last */
    uint32_t cap;
    ir_blk *targets = ssa_temp_refs(&ctx->ssa, &cap);
    if (count > cap) {
        ctx->err = WASMC_ERR_IR_EDGES;
        return;
    }
    wdec_br_table_label_iter_t label_it;
    uint32_t lab;
    unsigned pos = 0;
    WDEC_FOR_EACH_BR_TABLE_LABEL(label_it, lab, tbl) {
        if (pos == count - 1U) {
            break; /* targets[] has room for tbl->count labels */
        }
        targets[pos++] = label_target(ctx, lab);
    }
    wasmc_err_t err = wdec_br_table_label_iter_err(&label_it);
    if (err != WASMC_OK) {
        ctx->err = err;
        return;
    }
    targets[count - 1U] = label_target(ctx, tbl->default_label);

    ir_blk trampoline = IR_NIL;
    for (unsigned k = 0; k < count; k++) {
        if (targets[k] == IR_NIL) {
            if (trampoline == IR_NIL) {
                trampoline = ir_block_new(ctx->ir);
            }
            targets[k] = trampoline;
        }
    }

    const uint32_t first = ctx->ir->nedges;
    ir_emit_br_table(ctx->ir, idx, targets, count);
    tag_edges(ctx, first, val);
    if (trampoline != IR_NIL) {
        ir_block_begin(ctx->ir, trampoline);
        ir_emit_return(ctx->ir, val);
    }
    ctx->dead = true;
}

/* ------------------------------------------------------------------------ */
/* Other instructions                                                       */
/* ------------------------------------------------------------------------ */

/**
 * do_select() - Translate `select`.
 * @ctx: the translation.
 */
static void do_select(wc_ctx *ctx)
{
    ir_ref cond = pop_opd(ctx);
    ir_ref val2 = pop_opd(ctx);
    ir_ref val1 = pop_opd(ctx);
    if (ctx->dead) {
        push_opd(ctx, IR_NIL);
        return;
    }
    push_opd(ctx, ir_emit_select(ctx->ir, ir_ty(ir_get(ctx->ir, val1)), val1,
                                 val2, cond));
}

/**
 * unary_op() - Translate a numeric instruction with one operand.
 * @ctx: the translation.
 * @opcode: the IR operation.
 * @type: IR type of its result.
 */
static void unary_op(wc_ctx *ctx, unsigned opcode, unsigned type)
{
    ir_ref op_a = pop_opd(ctx);
    if (ctx->dead) {
        push_opd(ctx, IR_NIL);
        return;
    }
    push_opd(ctx, ir_emit_unary(ctx->ir, opcode, type, op_a));
}

/**
 * binary_op() - Translate a numeric instruction with two operands.
 * @ctx: the translation.
 * @opcode: the IR operation.
 * @type: IR type of its result.
 */
static void binary_op(wc_ctx *ctx, unsigned opcode, unsigned type)
{
    ir_ref op_b = pop_opd(ctx);
    ir_ref op_a = pop_opd(ctx);
    if (ctx->dead) {
        push_opd(ctx, IR_NIL);
        return;
    }
    push_opd(ctx, ir_emit_binary(ctx->ir, opcode, type, op_a, op_b));
}

/**
 * do_load() - Translate a load.
 * @ctx: the translation.
 * @opcode: the IR load operation.
 * @type: IR type of the loaded value.
 * @offset: the memarg offset.
 */
static void do_load(wc_ctx *ctx, unsigned opcode, unsigned type,
                    uint32_t offset)
{
    ir_ref addr = pop_opd(ctx);
    if (ctx->dead) {
        push_opd(ctx, IR_NIL);
        return;
    }
    push_opd(ctx, ir_emit_load(ctx->ir, opcode, type, addr, offset));
}

/**
 * do_store() - Translate a store.
 * @ctx: the translation.
 * @opcode: the IR store operation.
 * @offset: the memarg offset.
 */
static void do_store(wc_ctx *ctx, unsigned opcode, uint32_t offset)
{
    ir_ref val = pop_opd(ctx); /* the value is on top of the address */
    ir_ref addr = pop_opd(ctx);
    if (!ctx->dead) {
        ir_emit_store(ctx->ir, opcode, addr, val, offset);
    }
}

/**
 * pop_args() - Pop the arguments of a call.
 * @ctx: the translation.
 * @nargs: how many.
 *
 * In live code they are the top operands, in order, and are left where
 * they are: ir_emit_call*() copies them into the IR before anything is
 * pushed over them.
 *
 * Return: the arguments; NULL in dead code (nothing is emitted), or if the
 * frame holds fewer operands (only possible on an invalid module;
 * &wc_ctx.err is set).
 */
static const ir_ref *pop_args(wc_ctx *ctx, uint32_t nargs)
{
    if (ctx->dead) {
        for (uint32_t i = 0; i < nargs; i++) {
            pop_opd(ctx);
        }
        return NULL;
    }
    if (ctx->opd_top - cur_floor(ctx) < nargs) {
        ctx->err = WASMC_ERR_INVALID_ARG;
        return NULL;
    }
    ctx->opd_top -= nargs;
    return &ctx->opd[ctx->opd_top];
}

/**
 * do_call() - Translate `call`.
 * @ctx: the translation.
 * @funcidx: the called function.
 */
static void do_call(wc_ctx *ctx, uint32_t funcidx)
{
    wdec_functype_t ftype;
    wasmc_err_t err = wdec_func_type(ctx->mod, funcidx, &ftype);
    if (err != WASMC_OK) {
        ctx->err = err;
        return;
    }
    const ir_ref *args = pop_args(ctx, ftype.param_count);
    if (ctx->err) {
        return;
    }
    ir_ref ret = IR_NIL;
    if (!ctx->dead) {
        ret = ir_emit_call(ctx->ir, func_result_type(&ftype), funcidx, args,
                           ftype.param_count);
    }
    if (ftype.result_count) {
        push_opd(ctx, ret);
    }
}

/**
 * do_call_indirect() - Translate `call_indirect`.
 * @ctx: the translation.
 * @typeidx: the expected function type.
 */
static void do_call_indirect(wc_ctx *ctx, uint32_t typeidx)
{
    if (typeidx > UINT16_MAX) { /* the decoder rejects such modules */
        ctx->err = WASMC_ERR_TOO_MANY_TYPES;
        return;
    }
    wdec_functype_t ftype;
    wasmc_err_t err = wdec_get_type(ctx->mod, typeidx, &ftype);
    if (err != WASMC_OK) {
        ctx->err = err;
        return;
    }
    ir_ref tidx = pop_opd(ctx); /* the table index is on top of the args */
    const ir_ref *args = pop_args(ctx, ftype.param_count);
    if (ctx->err) {
        return;
    }
    ir_ref ret = IR_NIL;
    if (!ctx->dead) {
        ret = ir_emit_call_indirect(ctx->ir, func_result_type(&ftype),
                                    (uint16_t)typeidx, tidx, args,
                                    ftype.param_count);
    }
    if (ftype.result_count) {
        push_opd(ctx, ret);
    }
}

/**
 * do_global_get() - Translate `global.get`.
 * @ctx: the translation.
 * @index: the global.
 */
static void do_global_get(wc_ctx *ctx, uint32_t index)
{
    if (ctx->dead) {
        push_opd(ctx, IR_NIL);
        return;
    }
    wdec_globaltype_t glob;
    wasmc_err_t err = wdec_global_type(ctx->mod, index, &glob);
    if (err != WASMC_OK) {
        ctx->err = err;
        return;
    }
    push_opd(ctx, ir_emit_global_get(ctx->ir, ir_type_of(glob.valtype), index));
}

/**
 * emit_const() - Emit the constant of an i32/i64/f32/f64.const.
 * @out: the IR.
 * @ins: the instruction.
 *
 * Return: the constant, IR_NIL if @ins is no constant.
 */
static ir_ref emit_const(ir_func *out, const wdec_instr_t *ins)
{
    switch (ins->opcode) {
    case WDEC_OP_I32_CONST:
        return ir_emit_const32(out, IR_T_I32, (uint32_t)ins->imm.i32);
    case WDEC_OP_I64_CONST:
        return ir_emit_i64(out, (uint64_t)ins->imm.i64);
    case WDEC_OP_F32_CONST:
        return ir_emit_const32(
            out, IR_T_F32,
            (uint32_t)read_le(ins->imm.f32_bytes, sizeof(uint32_t)));
    case WDEC_OP_F64_CONST:
        return ir_emit_f64(out, read_le(ins->imm.f64_bytes, sizeof(uint64_t)));
    default:
        return IR_NIL;
    }
}

/* ------------------------------------------------------------------------ */
/* Setup: the fixed arrays, the pre-scan, the entry block                   */
/* ------------------------------------------------------------------------ */

/**
 * struct wc_sizes - What the fixed arrays are sized from.
 * @opd: operand-stack capacity.
 * @ctrl: control-stack capacity.
 * @blocks: &ir_func.max_blocks.
 * @edges: &ir_func.max_edges.
 * @nlocals: parameters + declared locals.
 * @loops: `loop` instructions in the body.
 * @results: `block`/`if` instructions with a result.
 * @constructs: `block`/`if` instructions.
 */
typedef struct wc_sizes {
    uint32_t opd, ctrl;
    uint32_t blocks, edges;
    uint32_t nlocals;
    uint32_t loops;
    uint32_t results;
    uint32_t constructs;
} wc_sizes;

/**
 * fixed_array() - Allocate one fixed array from the scratch.
 * @ctx: the translation.
 * @layout: size and alignment of the array.
 *
 * Return: the array, or NULL if out of scratch (&wc_ctx.err is set).
 */
static void *fixed_array(wc_ctx *ctx, ir_layout layout)
{
    void *ptr = ir_scratch_alloc(ctx->ir, layout);
    if (!ptr) {
        ctx->err = (wasmc_err_t)ctx->ir->err;
    }
    return ptr;
}

/**
 * alloc_ctx() - Allocate the fixed arrays known before the pre-scan.
 * @ctx: the translation.
 * @sizes: what they are sized from.
 *
 * All of them but those of alloc_scanned(): the pre-scan needs
 * &wc_ctx.written. The SSA builder is started and allocates its own
 * (ssa_alloc_arrays()). Also clears the arrays that need it.
 *
 * Return: %WASMC_OK, or the scratch allocation error.
 */
static wasmc_err_t alloc_ctx(wc_ctx *ctx, const wc_sizes *sizes)
{
    /* the per-local arrays get at least one entry */
    const uint32_t nlocals = sizes->nlocals ? sizes->nlocals : 1U;
    const uint32_t written_bytes = bitmap_bytes(sizes->nlocals) + 1U;
    const ssa_sizes ssa_dims = {
        .blocks = sizes->blocks,
        .edges = sizes->edges,
        .nvars = sizes->nlocals,
    };

    enum { NCTX = 5 };
    ir_layout layouts[NCTX + SSA_NARRAYS] = {
        IR_ARRAY(ir_ref, sizes->opd), IR_ARRAY(wc_frame, sizes->ctrl),
        IR_ARRAY(uint8_t, nlocals),   IR_ARRAY(uint8_t, written_bytes),
        IR_ARRAY(ir_ref, nlocals),
    };
    const unsigned count = NCTX + ssa_array_layouts(&ssa_dims, &layouts[NCTX]);
    ir_scratch_reserve(ctx->ir, ir_scratch_bytes(layouts, count));

    ctx->opd = fixed_array(ctx, layouts[0]);
    ctx->ctrl = fixed_array(ctx, layouts[1]);
    ctx->local_ty = fixed_array(ctx, layouts[2]);
    ctx->written = fixed_array(ctx, layouts[3]);
    ctx->init_val = fixed_array(ctx, layouts[4]);
    if (ctx->err) {
        return ctx->err;
    }
    /* the builder starts here: it reads the types in &wc_ctx.local_ty */
    ssa_init(&ctx->ssa, ctx->ir, &ctx->err, ctx->local_ty);
    ssa_alloc_arrays(&ctx->ssa, &ssa_dims);
    if (ctx->err) {
        return ctx->err;
    }
    memset(ctx->written, 0, written_bytes);
    ctx->opd_cap = sizes->opd;
    ctx->ctrl_cap = sizes->ctrl;
    return WASMC_OK;
}

/**
 * read_local_types() - Fill &wc_ctx.local_ty with the IR type of each local.
 * @ctx: the translation.
 * @func: the function, for its run-length-encoded declared locals.
 * @ftype: its type, for its parameters (the first locals).
 *
 * Return: %WASMC_OK, or the error decoding the locals.
 */
static wasmc_err_t read_local_types(wc_ctx *ctx, const wdec_func_t *func,
                                    const wdec_functype_t *ftype)
{
    for (uint32_t i = 0; i < ftype->param_count; i++) {
        ctx->local_ty[i] = (uint8_t)ir_type_of(ftype->params[i]);
    }
    uint32_t idx = ftype->param_count;
    wdec_locals_iter_t locals_it;
    uint32_t cnt;
    wdec_valtype_t valtype;
    WDEC_FOR_EACH_LOCALS(locals_it, cnt, valtype, func) {
        for (uint32_t k = 0; k < cnt && idx < ctx->nlocals; k++) {
            ctx->local_ty[idx++] = (uint8_t)ir_type_of(valtype);
        }
    }
    return wdec_locals_iter_err(&locals_it);
}

/**
 * scan_body() - Pre-scan the body.
 * @ctx: the translation; fills &wc_ctx.written.
 * @func: the function.
 * @sizes: its @sizes->loops (for the write sets), @sizes->results (for
 *     &wc_ctx.edge_val) and @sizes->constructs (for the regions) are
 *     counted.
 *
 * The locals never written keep their entry value and never enter the SSA
 * construction.
 *
 * Return: %WASMC_OK, or the error decoding the body.
 */
static wasmc_err_t scan_body(wc_ctx *ctx, const wdec_func_t *func,
                             wc_sizes *sizes)
{
    wdec_instr_iter_t iter;
    wdec_instr_t ins;
    WDEC_FOR_EACH_INSTR(iter, ins, func) {
        switch (ins.opcode) {
        case WDEC_OP_LOCAL_SET:
        case WDEC_OP_LOCAL_TEE:
            if (ins.imm.index < ctx->nlocals) {
                bitmap_set(ctx->written, ins.imm.index);
            }
            break;
        case WDEC_OP_LOOP:
            sizes->loops++;
            break;
        case WDEC_OP_BLOCK:
        case WDEC_OP_IF:
            sizes->constructs++;
            if (ins.imm.blocktype != WDEC_TYPE_EMPTY) {
                sizes->results++;
            }
            break;
        default:
            break;
        }
    }
    return wdec_instr_iter_err(&iter);
}

/**
 * setup_regions() - Give the SSA builder regions, if worth it.
 * @ctx: the translation, after alloc_scanned().
 * @func: the function.
 * @sizes: its sizes, counted by scan_body().
 *
 * Only with REGIONS_MIN_WRITTEN written locals or more, and some block or
 * if. The builder needs a bound of each local's writes: one more pass over
 * the body counts them (the entry definition included).
 *
 * Return: %WASMC_OK, or the error decoding the body or allocating.
 */
static wasmc_err_t setup_regions(wc_ctx *ctx, const wdec_func_t *func,
                                 const wc_sizes *sizes)
{
    uint32_t nwritten = 0;
    for (uint32_t var = 0; var < ctx->nlocals; var++) {
        nwritten += is_written(ctx, (uint16_t)var) ? 1U : 0U;
    }
    if (nwritten < REGIONS_MIN_WRITTEN || !sizes->constructs) {
        return WASMC_OK;
    }
    uint32_t *counts = ssa_region_counts(&ctx->ssa);
    if (!counts) {
        return ctx->err;
    }
    for (uint32_t var = 0; var < ctx->nlocals; var++) {
        counts[var] = is_written(ctx, (uint16_t)var) ? 1U : 0U;
    }
    wdec_instr_iter_t iter;
    wdec_instr_t ins;
    WDEC_FOR_EACH_INSTR(iter, ins, func) {
        if ((ins.opcode == WDEC_OP_LOCAL_SET ||
             ins.opcode == WDEC_OP_LOCAL_TEE) &&
            ins.imm.index < ctx->nlocals) {
            counts[ins.imm.index]++;
        }
    }
    wasmc_err_t err = wdec_instr_iter_err(&iter);
    if (err) {
        return err;
    }
    ssa_alloc_regions(&ctx->ssa, sizes->constructs);
    if (ctx->err) {
        return ctx->err;
    }
    ctx->rgn = fixed_array(ctx, IR_ARRAY(wc_region, sizes->ctrl));
    return ctx->err;
}

/**
 * alloc_scanned() - Allocate the fixed arrays sized from the pre-scan.
 * @ctx: the translation.
 * @sizes: what they are sized from, @sizes->loops and @sizes->results
 *     included.
 *
 * The SSA builder's loop write sets (ssa_alloc_loops()), and
 * &wc_ctx.edge_val, if some block/if has a result.
 *
 * Return: %WASMC_OK, or the scratch allocation error.
 */
static wasmc_err_t alloc_scanned(wc_ctx *ctx, const wc_sizes *sizes)
{
    const ir_layout layouts[2] = {
        ssa_loops_layout(&ctx->ssa, sizes->loops),
        IR_ARRAY(ir_ref, sizes->results ? sizes->edges : 0U),
    };
    ir_scratch_reserve(ctx->ir, ir_scratch_bytes(layouts, 2));
    ssa_alloc_loops(&ctx->ssa, sizes->loops);
    if (sizes->results) {
        ctx->edge_val = fixed_array(ctx, layouts[1]);
    }
    if (ctx->err) {
        return ctx->err;
    }
    if (ctx->edge_val) { /* IR_NIL: an edge no result was recorded on */
        memset(ctx->edge_val, UCHAR_MAX, (size_t)sizes->edges * sizeof(ir_ref));
    }
    return WASMC_OK;
}

/**
 * emit_entry() - Emit the entry block.
 * @ctx: the translation.
 * @ftype: the function's type.
 *
 * The parameters keep their argument value, the declared locals start at
 * zero (WASM's own rule); a local that is written also gets them as its
 * definition in the entry block.
 *
 * Return: %WASMC_OK, or the first error.
 */
static wasmc_err_t emit_entry(wc_ctx *ctx, const wdec_functype_t *ftype)
{
    ir_func *out = ctx->ir;
    ir_blk entry = ir_block_new(out);
    ir_block_begin(out, entry);
    for (uint32_t i = 0; i < ctx->nlocals; i++) {
        ir_ref val = i < ftype->param_count
                         ? ir_emit_param(out, ctx->local_ty[i], i)
                         : get_zero(ctx, ctx->local_ty[i]);
        ctx->init_val[i] = val;
        if (is_written(ctx, (uint16_t)i)) {
            ssa_write(&ctx->ssa, (ssa_site){.var = (uint16_t)i, .blk = entry},
                      val);
        }
    }
    return ctx->err;
}

/**
 * setup_func() - Prepare the translation of a function's body.
 * @ctx: the translation, with &wc_ctx.ir and &wc_ctx.nlocals set.
 * @func: the function.
 * @ftype: its type.
 * @caps: the operand/control stack capacities.
 *
 * Allocates the fixed arrays, reads the local types, pre-scans the body
 * and emits the entry block.
 *
 * Return: %WASMC_OK, or the first error.
 */
static wasmc_err_t setup_func(wc_ctx *ctx, const wdec_func_t *func,
                              const wdec_functype_t *ftype,
                              wdec_stack_depth caps)
{
    wc_sizes sizes = {
        .opd = caps.opd,
        .ctrl = caps.ctrl,
        .blocks = ctx->ir->max_blocks,
        .edges = ctx->ir->max_edges,
        .nlocals = ctx->nlocals,
    };
    wasmc_err_t err = alloc_ctx(ctx, &sizes);
    if (err) {
        return err;
    }
    err = read_local_types(ctx, func, ftype);
    if (err) {
        return err;
    }
    err = scan_body(ctx, func, &sizes);
    if (err) {
        return err;
    }
    err = alloc_scanned(ctx, &sizes);
    if (err) {
        return err;
    }
    err = setup_regions(ctx, func, &sizes);
    if (err) {
        return err;
    }
    err = emit_entry(ctx, ftype);
    if (err) {
        return err;
    }

    return WASMC_OK;
}

/* ------------------------------------------------------------------------ */
/* Dispatch                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * define NUMERIC_FIRST - The first numeric opcode, i32.eqz (0x45).
 *
 * The numeric instructions run from it to f64.reinterpret_i64 (0xBF), and
 * each maps to one IR operation.
 */
#define NUMERIC_FIRST WDEC_OP_I32_EQZ

/* the operand count of a numeric instruction, in NUMERIC[] */
enum { UNARY = 1, BINARY = 2 };

/**
 * struct numeric_op - The IR operation of a numeric instruction.
 * @opcode: the IR_OP_*.
 * @type: the IR_T_* of the result.
 * @arity: %UNARY or %BINARY.
 *
 * NUMERIC[] is indexed by NUMOP(opcode), the opcode's offset from
 * NUMERIC_FIRST. The IR operations are not typed by their operands, so
 * each WASM operator family (spec 3.3.1) is one small macro:
 *
 *   TESTOP(op)      t   -> i32   (eqz)
 *   RELOP(op)       t t -> i32   (eq, lt_s, ge, ...)
 *   UNOP(op, t)     t   -> t     (clz, neg, sqrt, ...)
 *   BINOP(op, t)    t t -> t     (add, shl, copysign, ...)
 *   CVTOP(op, t2)   t1  -> t2    (wrap, trunc, convert, reinterpret, ...)
 */
struct numeric_op {
    uint8_t opcode;
    uint8_t type;
    uint8_t arity;
};
#define NUMOP(op)       (WDEC_OP_##op - NUMERIC_FIRST)
#define TESTOP(op)      {IR_OP_##op, IR_T_I32, UNARY}
#define RELOP(op)       {IR_OP_##op, IR_T_I32, BINARY}
#define UNOP(op, type)  {IR_OP_##op, IR_T_##type, UNARY}
#define BINOP(op, type) {IR_OP_##op, IR_T_##type, BINARY}
#define CVTOP(op, to)   {IR_OP_##op, IR_T_##to, UNARY}
static const struct numeric_op NUMERIC[] = {
    /* clang-format off */
    /* testops and relops */
    [NUMOP(I32_EQZ)]             = TESTOP(EQZ),
    [NUMOP(I32_EQ)]              = RELOP(EQ),
    [NUMOP(I32_NE)]              = RELOP(NE),
    [NUMOP(I32_LT_S)]            = RELOP(LT_S),
    [NUMOP(I32_LT_U)]            = RELOP(LT_U),
    [NUMOP(I32_GT_S)]            = RELOP(GT_S),
    [NUMOP(I32_GT_U)]            = RELOP(GT_U),
    [NUMOP(I32_LE_S)]            = RELOP(LE_S),
    [NUMOP(I32_LE_U)]            = RELOP(LE_U),
    [NUMOP(I32_GE_S)]            = RELOP(GE_S),
    [NUMOP(I32_GE_U)]            = RELOP(GE_U),
    [NUMOP(I64_EQZ)]             = TESTOP(EQZ),
    [NUMOP(I64_EQ)]              = RELOP(EQ),
    [NUMOP(I64_NE)]              = RELOP(NE),
    [NUMOP(I64_LT_S)]            = RELOP(LT_S),
    [NUMOP(I64_LT_U)]            = RELOP(LT_U),
    [NUMOP(I64_GT_S)]            = RELOP(GT_S),
    [NUMOP(I64_GT_U)]            = RELOP(GT_U),
    [NUMOP(I64_LE_S)]            = RELOP(LE_S),
    [NUMOP(I64_LE_U)]            = RELOP(LE_U),
    [NUMOP(I64_GE_S)]            = RELOP(GE_S),
    [NUMOP(I64_GE_U)]            = RELOP(GE_U),
    [NUMOP(F32_EQ)]              = RELOP(FEQ),
    [NUMOP(F32_NE)]              = RELOP(FNE),
    [NUMOP(F32_LT)]              = RELOP(FLT),
    [NUMOP(F32_GT)]              = RELOP(FGT),
    [NUMOP(F32_LE)]              = RELOP(FLE),
    [NUMOP(F32_GE)]              = RELOP(FGE),
    [NUMOP(F64_EQ)]              = RELOP(FEQ),
    [NUMOP(F64_NE)]              = RELOP(FNE),
    [NUMOP(F64_LT)]              = RELOP(FLT),
    [NUMOP(F64_GT)]              = RELOP(FGT),
    [NUMOP(F64_LE)]              = RELOP(FLE),
    [NUMOP(F64_GE)]              = RELOP(FGE),
    /* unops and binops */
    [NUMOP(I32_CLZ)]             = UNOP(CLZ, I32),
    [NUMOP(I32_CTZ)]             = UNOP(CTZ, I32),
    [NUMOP(I32_POPCNT)]          = UNOP(POPCNT, I32),
    [NUMOP(I32_ADD)]             = BINOP(ADD, I32),
    [NUMOP(I32_SUB)]             = BINOP(SUB, I32),
    [NUMOP(I32_MUL)]             = BINOP(MUL, I32),
    [NUMOP(I32_DIV_S)]           = BINOP(DIV_S, I32),
    [NUMOP(I32_DIV_U)]           = BINOP(DIV_U, I32),
    [NUMOP(I32_REM_S)]           = BINOP(REM_S, I32),
    [NUMOP(I32_REM_U)]           = BINOP(REM_U, I32),
    [NUMOP(I32_AND)]             = BINOP(AND, I32),
    [NUMOP(I32_OR)]              = BINOP(OR, I32),
    [NUMOP(I32_XOR)]             = BINOP(XOR, I32),
    [NUMOP(I32_SHL)]             = BINOP(SHL, I32),
    [NUMOP(I32_SHR_S)]           = BINOP(SHR_S, I32),
    [NUMOP(I32_SHR_U)]           = BINOP(SHR_U, I32),
    [NUMOP(I32_ROTL)]            = BINOP(ROTL, I32),
    [NUMOP(I32_ROTR)]            = BINOP(ROTR, I32),
    [NUMOP(I64_CLZ)]             = UNOP(CLZ, I64),
    [NUMOP(I64_CTZ)]             = UNOP(CTZ, I64),
    [NUMOP(I64_POPCNT)]          = UNOP(POPCNT, I64),
    [NUMOP(I64_ADD)]             = BINOP(ADD, I64),
    [NUMOP(I64_SUB)]             = BINOP(SUB, I64),
    [NUMOP(I64_MUL)]             = BINOP(MUL, I64),
    [NUMOP(I64_DIV_S)]           = BINOP(DIV_S, I64),
    [NUMOP(I64_DIV_U)]           = BINOP(DIV_U, I64),
    [NUMOP(I64_REM_S)]           = BINOP(REM_S, I64),
    [NUMOP(I64_REM_U)]           = BINOP(REM_U, I64),
    [NUMOP(I64_AND)]             = BINOP(AND, I64),
    [NUMOP(I64_OR)]              = BINOP(OR, I64),
    [NUMOP(I64_XOR)]             = BINOP(XOR, I64),
    [NUMOP(I64_SHL)]             = BINOP(SHL, I64),
    [NUMOP(I64_SHR_S)]           = BINOP(SHR_S, I64),
    [NUMOP(I64_SHR_U)]           = BINOP(SHR_U, I64),
    [NUMOP(I64_ROTL)]            = BINOP(ROTL, I64),
    [NUMOP(I64_ROTR)]            = BINOP(ROTR, I64),
    [NUMOP(F32_ABS)]             = UNOP(FABS, F32),
    [NUMOP(F32_NEG)]             = UNOP(FNEG, F32),
    [NUMOP(F32_CEIL)]            = UNOP(FCEIL, F32),
    [NUMOP(F32_FLOOR)]           = UNOP(FFLOOR, F32),
    [NUMOP(F32_TRUNC)]           = UNOP(FTRUNC, F32),
    [NUMOP(F32_NEAREST)]         = UNOP(FNEAREST, F32),
    [NUMOP(F32_SQRT)]            = UNOP(FSQRT, F32),
    [NUMOP(F32_ADD)]             = BINOP(FADD, F32),
    [NUMOP(F32_SUB)]             = BINOP(FSUB, F32),
    [NUMOP(F32_MUL)]             = BINOP(FMUL, F32),
    [NUMOP(F32_DIV)]             = BINOP(FDIV, F32),
    [NUMOP(F32_MIN)]             = BINOP(FMIN, F32),
    [NUMOP(F32_MAX)]             = BINOP(FMAX, F32),
    [NUMOP(F32_COPYSIGN)]        = BINOP(FCOPYSIGN, F32),
    [NUMOP(F64_ABS)]             = UNOP(FABS, F64),
    [NUMOP(F64_NEG)]             = UNOP(FNEG, F64),
    [NUMOP(F64_CEIL)]            = UNOP(FCEIL, F64),
    [NUMOP(F64_FLOOR)]           = UNOP(FFLOOR, F64),
    [NUMOP(F64_TRUNC)]           = UNOP(FTRUNC, F64),
    [NUMOP(F64_NEAREST)]         = UNOP(FNEAREST, F64),
    [NUMOP(F64_SQRT)]            = UNOP(FSQRT, F64),
    [NUMOP(F64_ADD)]             = BINOP(FADD, F64),
    [NUMOP(F64_SUB)]             = BINOP(FSUB, F64),
    [NUMOP(F64_MUL)]             = BINOP(FMUL, F64),
    [NUMOP(F64_DIV)]             = BINOP(FDIV, F64),
    [NUMOP(F64_MIN)]             = BINOP(FMIN, F64),
    [NUMOP(F64_MAX)]             = BINOP(FMAX, F64),
    [NUMOP(F64_COPYSIGN)]        = BINOP(FCOPYSIGN, F64),
    /* cvtops */
    [NUMOP(I32_WRAP_I64)]        = CVTOP(WRAP, I32),
    [NUMOP(I32_TRUNC_F32_S)]     = CVTOP(TRUNC_S, I32),
    [NUMOP(I32_TRUNC_F32_U)]     = CVTOP(TRUNC_U, I32),
    [NUMOP(I32_TRUNC_F64_S)]     = CVTOP(TRUNC_S, I32),
    [NUMOP(I32_TRUNC_F64_U)]     = CVTOP(TRUNC_U, I32),
    [NUMOP(I64_EXTEND_I32_S)]    = CVTOP(EXTEND_S, I64),
    [NUMOP(I64_EXTEND_I32_U)]    = CVTOP(EXTEND_U, I64),
    [NUMOP(I64_TRUNC_F32_S)]     = CVTOP(TRUNC_S, I64),
    [NUMOP(I64_TRUNC_F32_U)]     = CVTOP(TRUNC_U, I64),
    [NUMOP(I64_TRUNC_F64_S)]     = CVTOP(TRUNC_S, I64),
    [NUMOP(I64_TRUNC_F64_U)]     = CVTOP(TRUNC_U, I64),
    [NUMOP(F32_CONVERT_I32_S)]   = CVTOP(CONVERT_S, F32),
    [NUMOP(F32_CONVERT_I32_U)]   = CVTOP(CONVERT_U, F32),
    [NUMOP(F32_CONVERT_I64_S)]   = CVTOP(CONVERT_S, F32),
    [NUMOP(F32_CONVERT_I64_U)]   = CVTOP(CONVERT_U, F32),
    [NUMOP(F32_DEMOTE_F64)]      = CVTOP(DEMOTE, F32),
    [NUMOP(F64_CONVERT_I32_S)]   = CVTOP(CONVERT_S, F64),
    [NUMOP(F64_CONVERT_I32_U)]   = CVTOP(CONVERT_U, F64),
    [NUMOP(F64_CONVERT_I64_S)]   = CVTOP(CONVERT_S, F64),
    [NUMOP(F64_CONVERT_I64_U)]   = CVTOP(CONVERT_U, F64),
    [NUMOP(F64_PROMOTE_F32)]     = CVTOP(PROMOTE, F64),
    [NUMOP(I32_REINTERPRET_F32)] = CVTOP(REINTERPRET, I32),
    [NUMOP(I64_REINTERPRET_F64)] = CVTOP(REINTERPRET, I64),
    [NUMOP(F32_REINTERPRET_I32)] = CVTOP(REINTERPRET, F32),
    [NUMOP(F64_REINTERPRET_I64)] = CVTOP(REINTERPRET, F64),
    /* clang-format on */
};
_Static_assert(sizeof NUMERIC / sizeof NUMERIC[0] ==
                   WDEC_OP_F64_REINTERPRET_I64 - NUMERIC_FIRST + 1,
               "NUMERIC must cover every numeric opcode");
#undef NUMOP
#undef TESTOP
#undef RELOP
#undef UNOP
#undef BINOP
#undef CVTOP

/**
 * struct load_op - The IR load of a load instruction.
 * @opcode: the IR_OP_LOAD*.
 * @type: the IR_T_* of the loaded value, once extended.
 *
 * LOADS[] is indexed by LOADOP(opcode), the opcode's offset from i32.load.
 */
struct load_op {
    uint8_t opcode;
    uint8_t type;
};
#define LOADOP(op)     (WDEC_OP_##op - WDEC_OP_I32_LOAD)
#define LOAD(op, type) {IR_OP_##op, IR_T_##type}
static const struct load_op LOADS[] = {
    /* clang-format off */
    [LOADOP(I32_LOAD)]     = LOAD(LOAD, I32),
    [LOADOP(I64_LOAD)]     = LOAD(LOAD, I64),
    [LOADOP(F32_LOAD)]     = LOAD(LOAD, F32),
    [LOADOP(F64_LOAD)]     = LOAD(LOAD, F64),
    [LOADOP(I32_LOAD8_S)]  = LOAD(LOAD8_S, I32),
    [LOADOP(I32_LOAD8_U)]  = LOAD(LOAD8_U, I32),
    [LOADOP(I32_LOAD16_S)] = LOAD(LOAD16_S, I32),
    [LOADOP(I32_LOAD16_U)] = LOAD(LOAD16_U, I32),
    [LOADOP(I64_LOAD8_S)]  = LOAD(LOAD8_S, I64),
    [LOADOP(I64_LOAD8_U)]  = LOAD(LOAD8_U, I64),
    [LOADOP(I64_LOAD16_S)] = LOAD(LOAD16_S, I64),
    [LOADOP(I64_LOAD16_U)] = LOAD(LOAD16_U, I64),
    [LOADOP(I64_LOAD32_S)] = LOAD(LOAD32_S, I64),
    [LOADOP(I64_LOAD32_U)] = LOAD(LOAD32_U, I64),
    /* clang-format on */
};
_Static_assert(sizeof LOADS / sizeof LOADS[0] ==
                   WDEC_OP_I64_LOAD32_U - WDEC_OP_I32_LOAD + 1,
               "LOADS must cover every load opcode");
#undef LOADOP
#undef LOAD

/* the IR store (IR_OP_STORE*) of each store instruction, indexed by
 * STOREOP(opcode), the opcode's offset from i32.store */
#define STOREOP(op) (WDEC_OP_##op - WDEC_OP_I32_STORE)
static const uint8_t STORES[] = {
    /* clang-format off */
    [STOREOP(I32_STORE)]   = IR_OP_STORE,
    [STOREOP(I64_STORE)]   = IR_OP_STORE,
    [STOREOP(F32_STORE)]   = IR_OP_STORE,
    [STOREOP(F64_STORE)]   = IR_OP_STORE,
    [STOREOP(I32_STORE8)]  = IR_OP_STORE8,
    [STOREOP(I32_STORE16)] = IR_OP_STORE16,
    [STOREOP(I64_STORE8)]  = IR_OP_STORE8,
    [STOREOP(I64_STORE16)] = IR_OP_STORE16,
    [STOREOP(I64_STORE32)] = IR_OP_STORE32,
    /* clang-format on */
};
_Static_assert(sizeof STORES / sizeof STORES[0] ==
                   WDEC_OP_I64_STORE32 - WDEC_OP_I32_STORE + 1,
               "STORES must cover every store opcode");
#undef STOREOP

/**
 * translate_control() - Translate a control or parametric instruction.
 * @ctx: the translation.
 * @ins: the instruction.
 * @iter: the instructions, just after @ins.
 *
 * Return: true if @ins is one (block, loop, if, branches, calls, drop,
 * select, ...), false to try another group.
 */
static bool translate_control(wc_ctx *ctx, const wdec_instr_t *ins,
                              const wdec_instr_iter_t *iter)
{
    switch (ins->opcode) {
    case WDEC_OP_UNREACHABLE:
        if (!ctx->dead) {
            ir_emit_unreachable(ctx->ir);
        }
        ctx->dead = true;
        return true;
    case WDEC_OP_NOP:
        return true;
    case WDEC_OP_BLOCK:
        do_block(ctx, ins->imm.blocktype);
        return true;
    case WDEC_OP_LOOP:
        do_loop(ctx, ins->imm.blocktype, iter);
        return true;
    case WDEC_OP_IF:
        do_if(ctx, ins->imm.blocktype);
        return true;
    case WDEC_OP_ELSE:
        do_else(ctx);
        return true;
    case WDEC_OP_END:
        if (ctx->ctrl_top == 0) {
            /* the body's own end: falls off the end */
            do_return(ctx);
        } else {
            do_end(ctx);
        }
        return true;
    case WDEC_OP_BR:
        do_br(ctx, ins->imm.index);
        return true;
    case WDEC_OP_BR_IF:
        do_br_if(ctx, ins->imm.index);
        return true;
    case WDEC_OP_BR_TABLE:
        do_br_table(ctx, &ins->imm.br_table);
        return true;
    case WDEC_OP_RETURN:
        do_return(ctx);
        return true;
    case WDEC_OP_CALL:
        do_call(ctx, ins->imm.index);
        return true;
    case WDEC_OP_CALL_INDIRECT:
        do_call_indirect(ctx, ins->imm.index);
        return true;
    case WDEC_OP_DROP:
        pop_opd(ctx);
        return true;
    case WDEC_OP_SELECT:
        do_select(ctx);
        return true;
    default:
        return false;
    }
}

/**
 * translate_variable() - Translate a local.* or global.* instruction.
 * @ctx: the translation.
 * @ins: the instruction.
 *
 * Return: true if @ins is one, false to try another group.
 */
static bool translate_variable(wc_ctx *ctx, const wdec_instr_t *ins)
{
    switch (ins->opcode) {
    case WDEC_OP_LOCAL_GET: {
        ir_ref val = IR_NIL;
        if (!ctx->dead) {
            val = read_local(ctx, local_site(ctx, ins));
        }
        push_opd(ctx, val);
        return true;
    }
    case WDEC_OP_LOCAL_SET:
    case WDEC_OP_LOCAL_TEE: {
        ir_ref val = pop_opd(ctx);
        if (!ctx->dead) {
            ssa_write(&ctx->ssa, local_site(ctx, ins), val);
        }
        if (ins->opcode == WDEC_OP_LOCAL_TEE) {
            push_opd(ctx, val);
        }
        return true;
    }
    case WDEC_OP_GLOBAL_GET:
        do_global_get(ctx, ins->imm.index);
        return true;
    case WDEC_OP_GLOBAL_SET: {
        ir_ref val = pop_opd(ctx);
        if (!ctx->dead) {
            ir_emit_global_set(ctx->ir, ins->imm.index, val);
        }
        return true;
    }
    default:
        return false;
    }
}

/**
 * translate_memory() - Translate a load, store, memory.size or memory.grow.
 * @ctx: the translation.
 * @ins: the instruction.
 *
 * Return: true if @ins is one, false to try another group.
 */
static bool translate_memory(wc_ctx *ctx, const wdec_instr_t *ins)
{
    const wdec_opcode_t opcode = ins->opcode;
    if (opcode >= WDEC_OP_I32_LOAD && opcode <= WDEC_OP_I64_LOAD32_U) {
        const unsigned idx = opcode - WDEC_OP_I32_LOAD;
        do_load(ctx, LOADS[idx].opcode, LOADS[idx].type,
                ins->imm.memarg.offset);
        return true;
    }
    if (opcode >= WDEC_OP_I32_STORE && opcode <= WDEC_OP_I64_STORE32) {
        do_store(ctx, STORES[opcode - WDEC_OP_I32_STORE],
                 ins->imm.memarg.offset);
        return true;
    }
    if (opcode == WDEC_OP_MEMORY_SIZE) {
        push_opd(ctx, ctx->dead ? IR_NIL : ir_emit_mem_size(ctx->ir));
        return true;
    }
    if (opcode == WDEC_OP_MEMORY_GROW) {
        ir_ref delta = pop_opd(ctx);
        push_opd(ctx, ctx->dead ? IR_NIL : ir_emit_mem_grow(ctx->ir, delta));
        return true;
    }
    return false;
}

/**
 * translate_const() - Translate an i32/i64/f32/f64.const.
 * @ctx: the translation.
 * @ins: the instruction.
 *
 * Return: true if @ins is one, false to try another group.
 */
static bool translate_const(wc_ctx *ctx, const wdec_instr_t *ins)
{
    switch (ins->opcode) {
    case WDEC_OP_I32_CONST:
    case WDEC_OP_I64_CONST:
    case WDEC_OP_F32_CONST:
    case WDEC_OP_F64_CONST:
        push_opd(ctx, ctx->dead ? IR_NIL : emit_const(ctx->ir, ins));
        return true;
    default:
        return false;
    }
}

/**
 * translate_numeric() - Translate a numeric instruction (NUMERIC[]).
 * @ctx: the translation.
 * @opcode: the instruction's opcode.
 *
 * Return: true if @opcode is one, false otherwise.
 */
static bool translate_numeric(wc_ctx *ctx, wdec_opcode_t opcode)
{
    if (opcode < NUMERIC_FIRST || opcode > WDEC_OP_F64_REINTERPRET_I64) {
        return false;
    }
    const unsigned idx = opcode - NUMERIC_FIRST;
    if (NUMERIC[idx].arity == BINARY) {
        binary_op(ctx, NUMERIC[idx].opcode, NUMERIC[idx].type);
    } else {
        unary_op(ctx, NUMERIC[idx].opcode, NUMERIC[idx].type);
    }
    return true;
}

/**
 * translate_instr() - Translate one instruction.
 * @ctx: the translation.
 * @ins: the instruction.
 * @iter: the instructions, just after @ins.
 *
 * An instruction no group knows is only possible on an invalid module:
 * %WASMC_ERR_INVALID_ARG.
 */
static void translate_instr(wc_ctx *ctx, const wdec_instr_t *ins,
                            const wdec_instr_iter_t *iter)
{
    /* each group returns false for an opcode it does not handle */
    /* clang-format off */
    const bool known =
        translate_control(ctx, ins, iter) ||
        translate_variable(ctx, ins) ||
        translate_memory(ctx, ins) ||
        translate_const(ctx, ins) ||
        translate_numeric(ctx, ins->opcode);
    /* clang-format on */
    if (!known) {
        ctx->err = WASMC_ERR_INVALID_ARG;
    }
}

/* ------------------------------------------------------------------------ */
/* Entry point                                                              */
/* ------------------------------------------------------------------------ */

wasmc_err_t wcompile_func(const wdec_module_t *mod, const wdec_func_t *func,
                          const wdec_functype_t *ftype, ir_func *out,
                          wdec_stack_depth caps)
{
    const uint32_t nlocals = ftype->param_count + func->local_count;
    /* local indices are stored in 16 bits */
    if (nlocals > UINT16_MAX) {
        return WASMC_ERR_INVALID_ARG;
    }

    wc_ctx ctx;
    memset(&ctx, 0, sizeof ctx);
    ctx.mod = mod;
    ctx.ir = out;
    ctx.nlocals = nlocals;
    ctx.result_ty = func_result_type(ftype);
    for (unsigned i = 0; i < IR_T_COUNT; i++) {
        ctx.zero[i] = IR_NIL;
    }

    wasmc_err_t err = setup_func(&ctx, func, ftype, caps);
    if (err != WASMC_OK) {
        return err;
    }

    wdec_instr_iter_t iter;
    wdec_instr_t ins;
    WDEC_FOR_EACH_INSTR(iter, ins, func) {
        translate_instr(&ctx, &ins, &iter);
        if (ctx.err || ir_failed(out)) {
            break;
        }
    }
    if (!ctx.err) { /* (the body stops at its first error, in ctx.err) */
        ctx.err = wdec_instr_iter_err(&iter);
    }
    return ctx.err ? ctx.err : ir_finalize(out);
}
