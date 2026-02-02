#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "wasmc.h"
#include "wasm.h"
#include "ir.h"
#include "aot.h"

typedef struct OperandStackEntry {
    struct OperandStackEntry *next;
    IRReference ref;
    WASMValtype type;
} OperandStackEntry;

typedef struct ControlStackEntry {
    struct ControlStackEntry *next;
    enum {
        CTRL_STACK_ENTRY_DUMMY,
        CTRL_STACK_ENTRY_IF,
        CTRL_STACK_ENTRY_ELSE,
        CTRL_STACK_ENTRY_BLOCK,
        CTRL_STACK_ENTRY_LOOP,
    } kind;
    /* Type of the associated label (used to type-check branches) */
    WASMBlocktype label_type;
    /* The result type of the block (used to check its result) */
    WASMBlocktype end_type;
    /* Blk of the associated label (used as a target of jmp/jnz on braches) */
    IRBlock *label_blk;
    /* If/block/loop blk header */
    IRBlock *start_blk;
    /* List containing the result of a if/block/loop if end_type is not
     * equal to WASM_BLOCKTYPE_NONE. end_results is NULL is end_type is
     * equal to WASM_BLOCKTYPE_NONE */
    struct list_head end_results;
    uint32_t end_results_count;
    /* The height of the operand stack at the start of the block
     * (used to check that operands do not underflow the current block) */
    uint32_t height;
    /* Flag recording whether the remainder of the block is unreachable
     * (used to handle code after a br or return) */
    bool unreachable;
} ControlStackEntry;

typedef struct CompileCtx {
    WASMModule *m;
    uint32_t funcidx;
    WASMFunction *wasm_func;
    IRFunction *ir_func;
    uint8_t *offset;
    OperandStackEntry *opd_stack;
    uint32_t opd_size;
    ControlStackEntry  *ctrl_stack;
    uint32_t ctrl_size;
    IRBlock *curr_block;
    IRReference mem0;
    IRReference globals_start;
} CompileCtx;

#define WASMC_OK 0

/* rega.c */
extern int register_allocation(IRFunction *f);

/* rv32/emit.c */
extern int rv32_emit_text(AOTModule *m, IRFunction *fn);


static void push_opd(CompileCtx *ctx, OperandStackEntry *opd) {
    opd->next = ctx->opd_stack;
    ctx->opd_stack = opd;
    ctx->opd_size++;
}

static OperandStackEntry *pop_opd(CompileCtx *ctx) {
    assert(ctx->ctrl_stack != NULL);
    assert(!ctx->ctrl_stack->unreachable);
    assert(ctx->opd_size > ctx->ctrl_stack->height);
    OperandStackEntry *opd = ctx->opd_stack;
    assert(opd != NULL);
    ctx->opd_stack = opd->next;
    ctx->opd_size--;
    return opd;
}

static OperandStackEntry *top_opd(CompileCtx *ctx) {
    OperandStackEntry *opd = ctx->opd_stack;
    assert(opd != NULL);
    return opd;
}

static void push_ctrl(CompileCtx *ctx, ControlStackEntry *ctrl) {
    ctrl->height = ctx->opd_size;
    ctrl->next = ctx->ctrl_stack;
    ctx->ctrl_stack = ctrl;
    ctx->ctrl_size++;
}


static ControlStackEntry *top_ctrl(CompileCtx *ctx) {
    ControlStackEntry *top = ctx->ctrl_stack;
    assert(top != NULL);
    return top;
}

static ControlStackEntry *pop_ctrl(CompileCtx *ctx) {
    ControlStackEntry *top = top_ctrl(ctx);
    if (top->end_type != WASM_BLOCKTYPE_NONE && !top->unreachable) {
        IRPhiArg *pa = malloc(sizeof(struct IRPhiArg));
        if (pa == NULL) return NULL;
        OperandStackEntry *opd = pop_opd(ctx);
        pa->value = opd->ref;
        pa->predecessor = ctx->curr_block;
        list_add_tail(&pa->link, &top->end_results);
        top->end_results_count++;
        free(opd);
    }
    assert(ctx->opd_size == top->height);
    ctx->ctrl_stack = top->next;
    ctx->ctrl_size--;
    return top;
}

static void unreachable(CompileCtx *ctx) {
    ControlStackEntry *top = top_ctrl(ctx);
    while (ctx->opd_size != top->height) {
        free(pop_opd(ctx));
    }
    top->unreachable = true;
    ctx->curr_block = NULL;
}

static ControlStackEntry *get_ctrl(CompileCtx *ctx, uint32_t index) {
    assert(index < ctx->ctrl_size - 1);
    ControlStackEntry *iter = ctx->ctrl_stack;
    for (uint32_t i = 0; i < index; i++) {
        iter = iter->next;
    }
    return iter;
}

static void free_ctrl(ControlStackEntry *ctrl) {
    if (ctrl->kind != CTRL_STACK_ENTRY_DUMMY) {
        list_release(&ctrl->end_results, free, IRPhiArg, link);
    }
    free(ctrl);
}

static int compile_local_get(CompileCtx *ctx) {
    WASMFunction *f = ctx->wasm_func;
    IRBlock *b = ctx->curr_block;
    uint32_t localidx;
    readULEB128_u32(&ctx->offset, f->code_end, &localidx);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);

    WASMValtype valtype = wasm_valtype_of(f, localidx);
    IRReference local;
    int err = ir_read_local(ctx->ir_func, b, localidx, &local);
    if (err) return err;

    OperandStackEntry *opd = malloc(sizeof(struct OperandStackEntry));
    if (opd == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    opd->ref = local;
    opd->type = valtype;
    push_opd(ctx, opd);

    return WASMC_OK;
}

static int compile_local_set(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    uint32_t localidx;
    readULEB128_u32(&ctx->offset, f->code_end, &localidx);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *opd = pop_opd(ctx);
    ir_write_local(ctx->curr_block, localidx, opd->ref);
    free(opd);

    return WASMC_OK;
}

static int compile_local_tee(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    uint32_t localidx;
    readULEB128_u32(&ctx->offset, f->code_end, &localidx);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *opd = top_opd(ctx);
    ir_write_local(ctx->curr_block, localidx, opd->ref);

    return WASMC_OK;
}

static int compile_global_get(CompileCtx *ctx) {

    int err;
    uint32_t globalidx;
    WASMFunction *f = ctx->wasm_func;
    readULEB128_u32(&ctx->offset, f->code_end, &globalidx);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);

    WASMGlobal *g = &ctx->m->globals[globalidx];
    IRReference global;
    if (g->is_mutable) {
        global = IR_REF_TMP(ctx->ir_func);
        IRReference offset = IR_REF_I32(sizeof(uint32_t) * globalidx);
        err = ir_append_load(
            ctx->curr_block,
            ir_cast(g->type),
            global,
            offset,
            ctx->globals_start);
        if (err) return err;
    } else {
        global = IR_REF_I32(g->as.i32);
    }

    OperandStackEntry *opd = malloc(sizeof(struct OperandStackEntry));
    if (opd == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    opd->type = g->type;
    opd->ref = global;
    push_opd(ctx, opd);

    return WASMC_OK;
}

static int compile_global_set(CompileCtx *ctx) {

    int err;
    WASMFunction *f = ctx->wasm_func;
    uint32_t globalidx;
    readULEB128_u32(&ctx->offset, f->code_end, &globalidx);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);

    WASMGlobal *g = &ctx->m->globals[globalidx];
    OperandStackEntry *opd = pop_opd(ctx);
    IRReference value = opd->ref;
    free(opd);
    IRReference offset = IR_REF_I32(sizeof(uint32_t) * globalidx);
    err = ir_append_store(
        ctx->curr_block,
        ir_cast(g->type),
        value,
        offset,
        ctx->globals_start);
    if (err) return err;

    return WASMC_OK;
}


static int compile_call(CompileCtx *ctx) {

    int err;
    WASMFunction *f = ctx->wasm_func;
    uint32_t funcidx;
    readULEB128_u32(&ctx->offset, f->code_end, &funcidx);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);

    WASMFunction *callee = &ctx->m->functions[funcidx];
    WASMFuncType *t = callee->type;

    /* Initialize the function call arguments, the first argument (a0)
     * is always a pointer to a WASMExecEnv struct of the WAMR runtime */
    err = ir_append_func_call_arg(
        ctx->curr_block,
        IR_TYPE_I32,
        ctx->ir_func->WASMExecEnv);
    if (err) return err;

    /* Initialize the other function call arguments */
    uint32_t n = t->param_count;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t argidx = n-i-1;
        OperandStackEntry *iter = ctx->opd_stack;
        for (uint32_t j = 0; j < argidx; j++) {
            iter = iter->next;
        }
        err = ir_append_func_call_arg(
            ctx->curr_block,
            ir_cast(t->param_types[argidx]),
            iter->ref);
        if (err) return err;
    }
    for (uint32_t i = 0; i < n; i++) {
        free(pop_opd(ctx));
    }

    if (t->result_count == 0) {
        /* do a call instruction to a void function */
        err = ir_append_void_func_call(
            ctx->curr_block,
            IR_REF_FUNC(callee));
        if (err) return err;

        return WASMC_OK;
    }

    /* do a call instruction to a non void function */
    IRReference ret_value = IR_REF_TMP(ctx->ir_func);
    err = ir_append_func_call(
        ctx->curr_block,
        ir_cast(t->result_type),
        ret_value,
        IR_REF_FUNC(callee));
    if (err) return err;

    OperandStackEntry *opd = malloc(sizeof(struct OperandStackEntry));
    if (opd == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    opd->ref = ret_value;
    opd->type = t->result_type;
    push_opd(ctx, opd);

    return WASMC_OK;
}

static int compile_br(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    uint32_t labelidx;
    readULEB128_u32(&ctx->offset, f->code_end, &labelidx);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);

    ControlStackEntry *ctrl = get_ctrl(ctx, labelidx);

    if (ctrl->label_type != WASM_BLOCKTYPE_NONE) {
        IRPhiArg *phi_arg = malloc(sizeof(struct IRPhiArg));
        if (phi_arg == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
        OperandStackEntry *opd = pop_opd(ctx);
        phi_arg->value = opd->ref;
        phi_arg->predecessor = ctx->curr_block;
        list_add_tail(&phi_arg->link, &ctrl->end_results);
        ctrl->end_results_count++;
        free(opd);
    }

    int err;
    if (ctrl->label_blk->is_loop_header) {
        err = ir_add_loop_end(ctrl->label_blk, ctx->curr_block);
        if (err) return err;
    }
    err = ir_jmp(ctx->ir_func, ctx->curr_block, ctrl->label_blk);
    if (err) return err;
    unreachable(ctx);

    return WASMC_OK;
}

static int compile_br_if(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    uint32_t labelidx;
    readULEB128_u32(&ctx->offset, f->code_end, &labelidx);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *opd = pop_opd(ctx);
    IRReference ifcond = opd->ref;
    free(opd);
    ControlStackEntry *ctrl = get_ctrl(ctx, labelidx);

    if (ctrl->label_type != WASM_BLOCKTYPE_NONE) {
        IRPhiArg *phi_arg = malloc(sizeof(struct IRPhiArg));
        if (phi_arg == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
        OperandStackEntry *opd = top_opd(ctx);
        phi_arg->value = opd->ref;
        phi_arg->predecessor = ctx->curr_block;
        list_add_tail(&phi_arg->link, &ctrl->end_results);
        ctrl->end_results_count++;
    }

    int err;
    if (ctrl->label_blk->is_loop_header) {
        err = ir_add_loop_end(ctrl->label_blk, ctx->curr_block);
        if (err) return err;
    }

    IRBlock *continue_blk = ir_create_sealed_block(ctx->ir_func);
    if (continue_blk == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    err = ir_jnz(
        ctx->ir_func,
        ctx->curr_block,
        ifcond,
        ctrl->label_blk,
        continue_blk);
    if (err) return err;

    ctx->curr_block = continue_blk;
    return WASMC_OK;
}

static int compile_return(CompileCtx *ctx) {

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);

    WASMFuncType *t = ctx->wasm_func->type;
    if (t->result_count != 0) {
        OperandStackEntry *opd = pop_opd(ctx);
        IRReference return_value = opd->ref;
        free(opd);
        int err = ir_ret1(ctx->ir_func, ctx->curr_block, return_value);
        if (err) return err;
    } else {
        ir_ret0(ctx->ir_func, ctx->curr_block);
    }
    unreachable(ctx);
    return WASMC_OK;
}

static int compile_if(CompileCtx *ctx) {

    int err;
    WASMBlocktype end_type;
    read_u8(&ctx->offset, ctx->wasm_func->code_end, &end_type);

    ControlStackEntry *ctrl = malloc(sizeof(struct ControlStackEntry));
    if (ctrl == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;

    /* if the top of the control stack is in a unreachable state
     * (e.g. if after a br) we want to skip the compilation of the if */
    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) {
        memset(ctrl, 0, sizeof(struct ControlStackEntry));
        ctrl->kind = CTRL_STACK_ENTRY_DUMMY;
        INIT_LIST_HEAD(&ctrl->end_results);
        ctrl->unreachable = true;
        push_ctrl(ctx, ctrl);
        return WASMC_OK;
    }
    /* if top->unreachable is false, ctx->curr_block must be not NULL */
    assert(ctx->curr_block != NULL);

    OperandStackEntry *opd = pop_opd(ctx);
    IRReference ifcond = opd->ref;
    free(opd);

    IRBlock *then_block = ir_create_sealed_block(ctx->ir_func);
    if (then_block == NULL) {
        err = WASMC_ERR_OUT_OF_HEAP_MEMORY;
        goto ERROR;
    }

    IRBlock *end_block  = ir_create_sealed_block(ctx->ir_func);
    if (end_block == NULL) {
        err = WASMC_ERR_OUT_OF_HEAP_MEMORY;
        goto ERROR;
    }

    /* at the moment we don't know if there a matching else branch,
     * hence we put NULL as a else branch block. We will fix the NULL
     * later with the else branch block if there is a matching else,
     * otherwise we will fix the NULL with the if end block */
    err = ir_jnz(ctx->ir_func, ctx->curr_block, ifcond, then_block, NULL);
    if (err) {
        err = WASMC_ERR_OUT_OF_HEAP_MEMORY;
        goto ERROR;
    }

    ctrl->kind = CTRL_STACK_ENTRY_IF;
    INIT_LIST_HEAD(&ctrl->end_results);
    ctrl->end_results_count = 0;
    ctrl->label_type = end_type;
    ctrl->end_type = end_type;
    ctrl->label_blk = end_block;
    ctrl->start_blk = ctx->curr_block;
    ctrl->unreachable = false;
    push_ctrl(ctx, ctrl);
    ctx->curr_block = then_block;

    return WASMC_OK;

ERROR:
    free_ctrl(ctrl);
    return err;
}

static int compile_else(CompileCtx *ctx) {

    int err;
    /* top is the ControlStackEntry pushed by the matching compile_if */
    ControlStackEntry *top = pop_ctrl(ctx);

    /* if we had skipped the compilation of a then branch because
     * the top of the control stack was in a unreachable state at
     * the start of the if compilation, skip also the compilation
     * of the else branch */
    if (top->kind == CTRL_STACK_ENTRY_DUMMY) {
        push_ctrl(ctx, top);
        return WASMC_OK;
    }

    if (!top->unreachable) {
        assert(ctx->curr_block != NULL);
        /* add a jump from then current block to the if end block */
        err = ir_jmp(ctx->ir_func, ctx->curr_block, top->label_blk);
        if (err) goto ERROR;
    }

    /* fix the NULL in the jnz */
    IRBlock *else_block = ir_create_sealed_block(ctx->ir_func);
    if (else_block == NULL) {
        err = WASMC_ERR_OUT_OF_HEAP_MEMORY;
        goto ERROR;
    }
    IRBlock *if_start_block = top->start_blk;
    if_start_block->succ[1] = else_block;

    err = ir_add_predecessor(else_block, if_start_block);
    if (err) goto ERROR;

    /* update top with else branch data */
    top->kind = CTRL_STACK_ENTRY_ELSE;
    top->unreachable = false;
    push_ctrl(ctx, top);

    /* update the current block */
    ctx->curr_block = else_block;

    return WASMC_OK;

ERROR:
    free_ctrl(top);
    return err;

}


static int get_block_result_value(CompileCtx *ctx, ControlStackEntry *ctrl,
                                   IRReference *out) {

    uint32_t n = ctrl->end_results_count;
    if (n == 1) {
        struct list_head *head = list_next(&ctrl->end_results);
        list_del(head);
        IRPhiArg *phi_arg = container_of(head, IRPhiArg, link);
        *out = phi_arg->value;
        free(phi_arg);
    } else if (n > 1) {
        IRBlock *b = ctx->curr_block;
        IRType t = ir_cast((WASMValtype)ctrl->end_type);
        IRPhi *phi = ir_create_phi(ctx->ir_func, b, t);
        if (phi == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
        while (!list_empty(&ctrl->end_results)) {
            struct list_head *head = list_next(&ctrl->end_results);
            list_del(head);
            list_add_tail(head, &phi->phi_arg_list);
            IRPhiArg *phi_arg = container_of(head, IRPhiArg, link);
            int err = ir_add_usage(&phi_arg->value);
            if (err) return err;
        }
        *out = IR_REF_PHI(phi);
    } else {
     /* There are some situation in which there is a if-else/block that
        must produce a value, there some other code after the
        if-else/block and inside the if-else/block the control is
        transfered in a part of the code that is not the code after
        the if-else/block. In these situations the code after the
        if-else/block is death code. Here is an example:

        i32.const 1
        (if (result i32)
         (then
           i32.const 9
           (block (result i32)
               i32.const 22
               i32.const 20
               i32.add
               br 1)
           i32.sub)
         (else i32.const 3))

        This snippet produce the value 42.
        the 'i32.sub' statement is death code.
        When the compilation arrives at the matching end opcode of the
        block the 'end_results' list associated with the block is empty
        but the block must push on the operand stack a i32 value. We push
        a default value to continue the compilation, the choice of the
        value to push is influential because the code after the block is
        death code. */
        *out = ir_get_default((WASMValtype)ctrl->end_type);
    }

    return WASMC_OK;
}

static int push_block_result_value(CompileCtx *ctx, ControlStackEntry *ctrl) {

    WASMBlocktype result_type = ctrl->end_type;
    if (result_type == WASM_BLOCKTYPE_NONE) {
        return WASMC_OK;
    }
    OperandStackEntry *block_result = malloc(sizeof(struct OperandStackEntry));
    if (block_result == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    block_result->type = (WASMValtype)result_type;
    int err = get_block_result_value(ctx, ctrl, &block_result->ref);
    if (err) {
        free(block_result);
        return err;
    }
    push_opd(ctx, block_result);

    return WASMC_OK;
}

static int compile_end_if(CompileCtx *ctx, ControlStackEntry *ctrl) {

    /* This is the end of a if without a matching else */
    int err;
    IRBlock *end_block = ctrl->label_blk;
    IRBlock *start_block = ctrl->start_blk;

    if (ctx->curr_block != NULL) {
        err = ir_jmp(ctx->ir_func, ctx->curr_block, end_block);
        if (err) return err;
    }
    ctx->curr_block = end_block;

    /* fix the NULL in the jnz */
    start_block->succ[1] = end_block;
    /* Add 'start_block' between 'end_block' predecessors */
    err = ir_add_predecessor(end_block, start_block);
    if (err) return err;

    err = push_block_result_value(ctx, ctrl);
    if (err) return err;

    return WASMC_OK;
}

static int compile_end_block(CompileCtx *ctx, ControlStackEntry *ctrl) {

    /* This is the end of a else branch of a if-else statement
     * or this is the end of block statement */
    int err;
    IRBlock *end_block = ctrl->label_blk;

    if (ctx->curr_block != NULL) {
        err = ir_jmp(ctx->ir_func, ctx->curr_block, end_block);
        if (err) return err;
    }
    ctx->curr_block = end_block;

    err = push_block_result_value(ctx, ctrl);
    if (err) return err;

    return WASMC_OK;
}

static int compile_end_loop(CompileCtx *ctx, ControlStackEntry *ctrl) {

    /* This is the end of a loop statement */
    int err;
    IRBlock *loop_header = ctrl->label_blk;

    err = ir_seal_block(ctx->ir_func, loop_header);
    if (err) return err;

    err = push_block_result_value(ctx, ctrl);
    if (err) return err;

    /* When a 'loop' statement is compiled a new block is created
     * and marked as loop header, see compile_loop(). In WASM a 'loop'
     * statement dont create always a loop in the control flow of the
     * program. In order to create a loop in the control flow of the
     * program you need an explict 'br' of 'br_if' that jump back at
     * the 'loop' statement. So if the 'loop' statement does not
     * really generate a loop in the control flow delete the loop
     * header mark. */
    if (list_empty(&loop_header->loop_end_block_list)) {
        loop_header->is_loop_header = false;
    }

    /* When the body of loop end with a br or return statement
     * ctx->curr_block is NULL. If there are some code after the
     * loop but in the same scope of the loop statement we need
     * a new blk to continue the compilation. The code after the
     * loop is most probably death code. */
    if (ctx->curr_block == NULL) {
         if (ctx->offset[0] != WASM_OPCODE_END ||
            ctx->offset == ctx->wasm_func->code_end) {
            ctx->curr_block = ir_create_sealed_block(ctx->ir_func);
        }
    }

    return WASMC_OK;
}

static int compile_end(CompileCtx *ctx) {

    int err;
    ControlStackEntry *top = pop_ctrl(ctx);
    switch (top->kind) {
    /* if we had skipped the compilation of a if/block/loop because
     * the top of the control stack was in a unreachable state at
     * the start of the if/block/loop compilation, just return */
    case CTRL_STACK_ENTRY_DUMMY:
        err = WASMC_OK;
        break;
    case CTRL_STACK_ENTRY_IF:
        err = compile_end_if(ctx, top);
        break;
    case CTRL_STACK_ENTRY_ELSE:
        /* compile_end_else() is equal to compile_end_block() */
        err = compile_end_block(ctx, top);
        break;
    case CTRL_STACK_ENTRY_BLOCK:
        err = compile_end_block(ctx, top);
        break;
    case CTRL_STACK_ENTRY_LOOP:
        err = compile_end_loop(ctx, top);
        break;
    }

    free_ctrl(top);
    return err;
}

static int compile_block(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    WASMBlocktype end_type;
    read_u8(&ctx->offset, f->code_end, &end_type);

    ControlStackEntry *ctrl = malloc(sizeof(struct ControlStackEntry));
    if (ctrl == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;

    /* if the top of the control stack is in a unreachable state
     * (e.g. block after a br) we want to skip the compilation of the block */
    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) {
        memset(ctrl, 0, sizeof(struct ControlStackEntry));
        ctrl->kind = CTRL_STACK_ENTRY_DUMMY;
        INIT_LIST_HEAD(&ctrl->end_results);
        ctrl->unreachable = true;
        push_ctrl(ctx, ctrl);
        return WASMC_OK;
    }
    /* if top->unreachable is false, ctx->curr_block must be not NULL */
    assert(ctx->curr_block != NULL);

    IRBlock *end_blk = ir_create_sealed_block(ctx->ir_func);
    if (end_blk == NULL) {
        free(ctrl);
        return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    }

    ctrl->kind = CTRL_STACK_ENTRY_BLOCK;
    INIT_LIST_HEAD(&ctrl->end_results);
    ctrl->end_results_count = 0;
    ctrl->label_type = end_type;
    ctrl->end_type = end_type;
    ctrl->label_blk = end_blk;
    ctrl->start_blk = ctx->curr_block;
    ctrl->unreachable = false;
    push_ctrl(ctx, ctrl);

    return WASMC_OK;
}

static int compile_loop(CompileCtx *ctx) {

    int err;
    WASMFunction *f = ctx->wasm_func;
    WASMBlocktype end_type;
    read_u8(&ctx->offset, f->code_end, &end_type);

    ControlStackEntry *ctrl = malloc(sizeof(struct ControlStackEntry));
    if (ctrl == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;

    /* if the top of the control stack is in a unreachable state
     * (e.g. loop after a br) we want to skip the compilation of the loop */
    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) {
        memset(ctrl, 0, sizeof(struct ControlStackEntry));
        ctrl->kind = CTRL_STACK_ENTRY_DUMMY;
        INIT_LIST_HEAD(&ctrl->end_results);
        ctrl->unreachable = true;
        push_ctrl(ctx, ctrl);
        return WASMC_OK;
    }
    /* if top->unreachable is false, ctx->curr_block must be not NULL */
    assert(ctx->curr_block != NULL);

    IRBlock *loop_header = ir_create_block(ctx->ir_func);
    if (loop_header == NULL) {
        err = WASMC_ERR_OUT_OF_HEAP_MEMORY;
        goto ERROR;
    }

    loop_header->is_loop_header = true;
    err = ir_jmp(ctx->ir_func, ctx->curr_block, loop_header);
    if (err) goto ERROR;
    ctx->curr_block = loop_header;

    ctrl->kind = CTRL_STACK_ENTRY_LOOP;
    INIT_LIST_HEAD(&ctrl->end_results);
    ctrl->end_results_count = 0;
    ctrl->label_type  = WASM_BLOCKTYPE_NONE;
    ctrl->end_type    = end_type;
    ctrl->label_blk   = loop_header;
    ctrl->start_blk   = ctx->curr_block;
    ctrl->unreachable = false;
    push_ctrl(ctx, ctrl);

    return WASMC_OK;

ERROR:
    free_ctrl(ctrl);
    return err;
}

static int compile_drop(CompileCtx *ctx) {

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);

    free(pop_opd(ctx));
    return WASMC_OK;
}

static int compile_select(CompileCtx *ctx) {

    int err;
    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *cond = pop_opd(ctx);
    OperandStackEntry *arg2 = pop_opd(ctx);
    OperandStackEntry *arg1 = pop_opd(ctx);

    IRBlock *select_arg1 = ir_create_sealed_block(ctx->ir_func);
    if (select_arg1 == NULL) {
        err = WASMC_ERR_OUT_OF_HEAP_MEMORY;
        goto ERROR;
    }
    IRBlock *select_arg2 = ctx->curr_block;
    IRBlock *end = ir_create_sealed_block(ctx->ir_func);
    if (end == NULL) {
        err = WASMC_ERR_OUT_OF_HEAP_MEMORY;
        goto ERROR;
    }

    assert(ctx->curr_block != NULL);
    err = ir_jnz(ctx->ir_func, ctx->curr_block, cond->ref, select_arg1, end);
    if (err) goto ERROR;

    err = ir_jmp(ctx->ir_func, select_arg1, end);
    if (err) goto ERROR;
    ctx->curr_block = end;

    IRPhi *phi = ir_create_phi(ctx->ir_func, end, ir_cast(arg1->type));
    if (phi == NULL) {
        err = WASMC_ERR_OUT_OF_HEAP_MEMORY;
        goto ERROR;
    }
    err = ir_append_phi_arg(phi, arg1->ref, select_arg1);
    if (err) goto ERROR;
    err = ir_append_phi_arg(phi, arg2->ref, select_arg2);
    if (err) goto ERROR;

    OperandStackEntry *result = malloc(sizeof(struct OperandStackEntry));
    if (result == NULL) {
        err = WASMC_ERR_OUT_OF_HEAP_MEMORY;
        goto ERROR;
    }
    result->type = arg1->type;
    result->ref = IR_REF_PHI(phi);
    push_opd(ctx, result);

    free(cond);
    free(arg1);
    free(arg2);
    return WASMC_OK;

ERROR:
    free(cond);
    free(arg1);
    free(arg2);
    return err;
}

static int compile_load(CompileCtx *ctx, WASMValtype t, IROpcode op) {

    WASMFunction *f = ctx->wasm_func;
    IRBlock *b = ctx->curr_block;
    //TODO: how to properly use align?
    uint32_t align, offset;
    readULEB128_u32(&ctx->offset, f->code_end, &align);
    readULEB128_u32(&ctx->offset, f->code_end, &offset);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *opd = pop_opd(ctx);
    IRReference memidx = opd->ref;
    free(opd);

    int err;
    IRReference destination = IR_REF_TMP(ctx->ir_func);
    if (memidx.type == IR_REF_TYPE_I32) {
        uint32_t c = memidx.as.i32 + offset;
        err = ir_append_instr(b, op, ir_cast(t), destination, IR_REF_I32(c), ctx->mem0);
        if (err) return err;
    } else {
        IRReference ptr = IR_REF_TMP(ctx->ir_func);
        err = ir_append_add(b, IR_TYPE_I32, ptr, ctx->mem0, memidx);
        if (err) return err;
        err = ir_append_instr(b, op, ir_cast(t), destination, IR_REF_I32(offset), ptr);
        if (err) return err;
    }
    //TODO: out of bound memory check

    OperandStackEntry *result = malloc(sizeof(struct OperandStackEntry));
    if (result == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    result->type = t;
    result->ref = destination;
    push_opd(ctx, result);

    return WASMC_OK;
}

static int compile_store(CompileCtx *ctx, WASMValtype t, IROpcode op) {

    WASMFunction *f = ctx->wasm_func;
    IRBlock *b = ctx->curr_block;
    //TODO: how to properly use align?
    uint32_t align, offset;
    readULEB128_u32(&ctx->offset, f->code_end, &align);
    readULEB128_u32(&ctx->offset, f->code_end, &offset);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *opd = pop_opd(ctx);
    IRReference value  = opd->ref;
    free(opd);

    opd = pop_opd(ctx);
    IRReference memidx = opd->ref;
    free(opd);

    int err;
    if (memidx.type == IR_REF_TYPE_I32) {
        uint32_t c = memidx.as.i32 + offset;
        err = ir_append_instr(b, op, ir_cast(t), value, IR_REF_I32(c), ctx->mem0);
        if (err) return err;
    } else {
        IRReference ptr = IR_REF_TMP(ctx->ir_func);
        err = ir_append_add(b, IR_TYPE_I32, ptr, ctx->mem0, memidx);
        if (err) return err;
        err = ir_append_instr(b, op, ir_cast(t), value, IR_REF_I32(offset), ptr);
        if (err) return err;
    }

    return WASMC_OK;
}

static int compile_binop(CompileCtx *ctx, IROpcode op) {

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);


    OperandStackEntry *opd = pop_opd(ctx);
    IRReference arg2 = opd->ref;
    WASMValtype type = opd->type;
    free(opd);

    opd = pop_opd(ctx);
    IRReference arg1 = opd->ref;
    free(opd);

    IRBlock *b = ctx->curr_block;
    IRReference sum = IR_REF_TMP(ctx->ir_func);
    int err = ir_append_instr(b, op, ir_cast(type), sum, arg1, arg2);
    if (err) return err;

    OperandStackEntry *result = malloc(sizeof(struct OperandStackEntry));
    if (result == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    result->ref = sum;
    result->type = type;
    push_opd(ctx, result);

    return WASMC_OK;
}

static int compile_testop(CompileCtx *ctx, IROpcode op) {

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);

    IRBlock *b = ctx->curr_block;
    OperandStackEntry *opd = pop_opd(ctx);
    IRReference value = opd->ref;
    free(opd);
    IRReference test = IR_REF_TMP(ctx->ir_func);

    int err;
    err = ir_append_instr(b, op, IR_TYPE_I32, test, value, IR_REF_UNDEFINED);
    if (err) return err;

    OperandStackEntry *result = malloc(sizeof(struct OperandStackEntry));
    if (result == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    result->ref = test;
    result->type = WASM_VALTYPE_I32;
    push_opd(ctx, result);

    return WASMC_OK;
}

static int compile_relop(CompileCtx *ctx, IROpcode op) {

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);

    IRBlock *b = ctx->curr_block;
    OperandStackEntry *opd = pop_opd(ctx);
    IRReference arg2 = opd->ref;
    free(opd);

    opd = pop_opd(ctx);
    IRReference arg1 = opd->ref;
    free(opd);

    int err;
    IRReference rel = IR_REF_TMP(ctx->ir_func);
    err = ir_append_instr(b, op, IR_TYPE_I32, rel, arg1, arg2);
    if (err) return err;

    OperandStackEntry *result = malloc(sizeof(struct OperandStackEntry));
    if (result == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    result->type = WASM_VALTYPE_I32;
    result->ref = rel;
    push_opd(ctx, result);

    return WASMC_OK;
}

static int compile_i32const(CompileCtx *ctx) {

    int32_t n;
    readILEB128_i32(&ctx->offset, ctx->wasm_func->code_end, &n);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return WASMC_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *opd = malloc(sizeof(struct OperandStackEntry));
    if (opd == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    opd->type = WASM_VALTYPE_I32;
    opd->ref = IR_REF_I32(n);
    push_opd(ctx, opd);

    return WASMC_OK;
}

static int compile_instr(CompileCtx *ctx, uint8_t opcode) {

    switch (opcode) {
    /* Control instructions */
    case WASM_OPCODE_UNREACHABLE:
        ir_halt(ctx->ir_func, ctx->curr_block);
        return WASMC_OK;
    case WASM_OPCODE_NOP:
        return WASMC_OK;
    case WASM_OPCODE_LOOP:
        return compile_loop(ctx);
    case WASM_OPCODE_BLOCK:
        return compile_block(ctx);
    case WASM_OPCODE_IF:
        return compile_if(ctx);
    case WASM_OPCODE_ELSE:
        return compile_else(ctx);
    case WASM_OPCODE_END:
        return compile_end(ctx);
    case WASM_OPCODE_BRANCH:
        return compile_br(ctx);
    case WASM_OPCODE_BRANCH_IF:
        return compile_br_if(ctx);
    case WASM_OPCODE_CALL:
        return compile_call(ctx);
    case WASM_OPCODE_RETURN:
        return compile_return(ctx);

    /* Parametric instruction */
    case WASM_OPCODE_DROP:
        return compile_drop(ctx);
    case WASM_OPCODE_SELECT:
        return compile_select(ctx);

    /* Variable instructions */
    case WASM_OPCODE_LOCAL_GET:
        return compile_local_get(ctx);
    case WASM_OPCODE_LOCAL_SET:
        return compile_local_set(ctx);
    case WASM_OPCODE_LOCAL_TEE:
        return compile_local_tee(ctx);
    case WASM_OPCODE_GLOBAL_GET:
        return compile_global_get(ctx);
    case WASM_OPCODE_GLOBAL_SET:
        return compile_global_set(ctx);

    /* Memory instructions */
    case WASM_OPCODE_I32_LOAD:
        return compile_load(ctx, WASM_VALTYPE_I32, IR_OPCODE_LOAD);
    case WASM_OPCODE_I32_STORE:
        return compile_store(ctx, WASM_VALTYPE_I32, IR_OPCODE_STORE);
    case WASM_OPCODE_I32_LOAD8_S:
        return compile_load(ctx, WASM_VALTYPE_I32, IR_OPCODE_SLOAD8);
    case WASM_OPCODE_I32_LOAD8_U:
        return compile_load(ctx, WASM_VALTYPE_I32, IR_OPCODE_ULOAD8);
    case WASM_OPCODE_I32_STORE8:
        return compile_store(ctx, WASM_VALTYPE_I32, IR_OPCODE_STORE8);

    /* Numeric instruction */
    case WASM_OPCODE_I32_CONST:
        return compile_i32const(ctx);
    case WASM_OPCODE_I32_EQZ:
        return compile_testop(ctx, IR_OPCODE_EQZ);
    case WASM_OPCODE_I32_EQ:
        return compile_relop(ctx, IR_OPCODE_EQ);
    case WASM_OPCODE_I32_NE:
        return compile_relop(ctx, IR_OPCODE_NE);
    case WASM_OPCODE_I32_LT_S:
        return compile_relop(ctx, IR_OPCODE_SLT);
    case WASM_OPCODE_I32_LT_U:
        return compile_relop(ctx, IR_OPCODE_ULT);
    case WASM_OPCODE_I32_GT_S:
        return compile_relop(ctx, IR_OPCODE_SGT);
    case WASM_OPCODE_I32_GT_U:
        return compile_relop(ctx, IR_OPCODE_UGT);
    case WASM_OPCODE_I32_LE_S:
        return compile_relop(ctx, IR_OPCODE_SLE);
    case WASM_OPCODE_I32_LE_U:
        return compile_relop(ctx, IR_OPCODE_ULE);
    case WASM_OPCODE_I32_GE_S:
        return compile_relop(ctx, IR_OPCODE_SGE);
    case WASM_OPCODE_I32_GE_U:
        return compile_relop(ctx, IR_OPCODE_UGE);
    case WASM_OPCODE_I32_ADD:
        return compile_binop(ctx, IR_OPCODE_ADD);
    case WASM_OPCODE_I32_SUB:
        return compile_binop(ctx, IR_OPCODE_SUB);
    case WASM_OPCODE_I32_MUL:
        return compile_binop(ctx, IR_OPCODE_MUL);
    case WASM_OPCODE_I32_DIV_S:
        return compile_binop(ctx, IR_OPCODE_SDIV);
    case WASM_OPCODE_I32_DIV_U:
        return compile_binop(ctx, IR_OPCODE_UDIV);
    case WASM_OPCODE_I32_REM_S:
        return compile_binop(ctx, IR_OPCODE_SREM);
    case WASM_OPCODE_I32_REM_U:
        return compile_binop(ctx, IR_OPCODE_UREM);
    case WASM_OPCODE_I32_AND:
        return compile_binop(ctx, IR_OPCODE_AND);
    case WASM_OPCODE_I32_OR:
        return compile_binop(ctx, IR_OPCODE_OR);
    case WASM_OPCODE_I32_XOR:
        return compile_binop(ctx, IR_OPCODE_XOR);
    case WASM_OPCODE_I32_SHL:
        return compile_binop(ctx, IR_OPCODE_SHL);
    case WASM_OPCODE_I32_SHR_S:
        return compile_binop(ctx, IR_OPCODE_ASHR);
    case WASM_OPCODE_I32_SHR_U:
        return compile_binop(ctx, IR_OPCODE_ASHR);

    /* unknown opcode */
    default:
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    return WASMC_OK;
}

static void cleanup(CompileCtx *ctx) {

    /* free operand stack */
    OperandStackEntry *opd = ctx->opd_stack;
    while (opd != NULL) {
        OperandStackEntry *next = opd->next;
        free(opd);
        opd = next;
    }

    /* free control stack */
    ControlStackEntry *ctrl = ctx->ctrl_stack;
    while (ctrl != NULL) {
        ControlStackEntry *next = ctrl->next;
        free_ctrl(ctrl);
        ctrl = next;
    }

    ir_free_function(ctx->ir_func);
}

static int compile_fn(WASMModule *m, uint32_t funcidx, IRFunction **fn_out) {

    int err;
    assert(funcidx < m->function_count);
    WASMFunction *wasm_func = &m->functions[funcidx];
    IRFunction *ir_func = ir_create_function(wasm_func);
    if (ir_func == NULL) {
        err = WASMC_ERR_OUT_OF_HEAP_MEMORY;
        goto ERROR;
    }

    IRReference WASMModuleInstance = IR_REF_UNDEFINED;
    if (m->memories_count > 0 || m->global_count > 0) {
        WASMModuleInstance = IR_REF_TMP(ir_func);
        err = ir_append_load(
            ir_func->start,
            IR_TYPE_I32,
            WASMModuleInstance,
            IR_REF_I32(8),
            ir_func->WASMExecEnv);
        if (err) goto ERROR;
    }
    /* mem0 hold the pointer to the memory 0 in the WAMR AOT runtime */
    IRReference mem0 = IR_REF_UNDEFINED;
    if (m->memories_count > 0) {
        mem0 = IR_REF_TMP(ir_func);
        err = ir_append_load(
            ir_func->start,
            IR_TYPE_I32,
            mem0,
            IR_REF_I32(376),
            WASMModuleInstance);
        if (err) goto ERROR;
    }
    /* globals_start hold the pointer to the start of the
     * array of globals variables in the WAMR AOT runtime */
    IRReference globals_start = IR_REF_UNDEFINED;
    if (m->global_count > 0) {
        globals_start = IR_REF_TMP(ir_func);
        err = ir_append_add(
            ir_func->start,
            IR_TYPE_I32,
            globals_start,
            WASMModuleInstance,
            IR_REF_I32(464));
        if (err) goto ERROR;
    }

    CompileCtx ctx = {0};
    ctx.m = m;
    ctx.funcidx = funcidx;
    ctx.wasm_func = wasm_func;
    ctx.ir_func = ir_func;
    ctx.offset = wasm_func->code_start;
    ctx.curr_block = ir_func->start;
    ctx.mem0 = mem0;
    ctx.globals_start = globals_start;

    ControlStackEntry *ctrl = malloc(sizeof(struct ControlStackEntry));
    if (ctrl == NULL) {
        err = WASMC_ERR_OUT_OF_HEAP_MEMORY;
        goto ERROR;
    }
    memset(ctrl, 0, sizeof(struct ControlStackEntry));
    ctrl->end_type = WASM_BLOCKTYPE_NONE;
    push_ctrl(&ctx, ctrl);

    /* compile the body of the function */
    uint8_t opcode;
    while (ctx.offset < wasm_func->code_end) {
        read_u8(&ctx.offset, wasm_func->code_end, &opcode);
        err = compile_instr(&ctx, opcode);
        if (err) goto ERROR;
    }

    /* Add an implicit return if there is
     * no explicit return in the wasm code */
    err = compile_return(&ctx);
    if (err) goto ERROR;
    free(pop_ctrl(&ctx));

    /* Replace the phi references with tmp references */
    IRBlock *block;
    list_for_each_entry(block, &ir_func->block_list, link) {
        IRPhi *phi;
        list_for_each_entry(phi, &block->phi_list, link) {
            IRUse *use, *iter;
            list_for_each_entry_safe(use, iter, &phi->use_list, link) {
                use->ref->type = IR_REF_TYPE_TMP;
                use->ref->as.tmp_id = phi->id;
                list_del(&use->link);
                free(use);
            }
        }
        /* Free block locals */
        if (block->locals != NULL) {
            free(block->locals);
            block->locals = NULL;
        }
    }

    *fn_out = ir_func;
    return WASMC_OK;

ERROR:
    cleanup(&ctx);
    *fn_out = NULL;
    return err;
}

int wasmc_compile(uint8_t *wasm_buf, unsigned int wasm_len,
            uint8_t *aot_buf, unsigned int aot_len) {

    int err;
    IRFunction *fn = NULL;
    WASMModule wasm_mod;
    err = wasm_decode(&wasm_mod, wasm_buf, wasm_len);
    if (err) goto ERROR;

    AOTModule aot_mod;
    err = aot_module_init(&aot_mod, aot_buf, aot_len, &wasm_mod);
    if (err) goto ERROR;
    err = emit_target_info(&aot_mod);
    if (err) goto ERROR;
    err = emit_init_data(&aot_mod);
    if (err) goto ERROR;
    for (uint32_t i = 0; i < wasm_mod.function_count; i++) {
        err = compile_fn(&wasm_mod, i, &fn);
        if (err) goto ERROR;
        ir_print_fn(fn, stdout);
        err = register_allocation(fn);
        if (err) return err;
        err = rv32_emit_text(&aot_mod, fn);
        if (err) goto ERROR;
        ir_free_function(fn);
    }
    err = emit_function(&aot_mod);
    if (err) return err;
    err = emit_export(&aot_mod);
    if (err) return err;
    err = emit_relocation(&aot_mod);
    if (err) return err;

    int len = aot_mod.offset - aot_mod.buf;
    aot_module_cleanup(&aot_mod);
    wasm_free(&wasm_mod);
    return len;

ERROR:
    wasm_free(&wasm_mod);
    if (fn != NULL) ir_free_function(fn);
    return err;

}

