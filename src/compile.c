#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "wasm.h"
#include "libqbe.h"
#include "aot.h"
#include "compile.h"

typedef struct OperandStackEntry {
    struct OperandStackEntry *next;
    Ref r;
    WASMValtype t;
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
    Blk *label_blk;
    /* If/block/loop blk header */
    Blk *start_blk;
    /* List containing the result of a if/block/loop if end_type is not
     * equal to WASM_BLOCKTYPE_NONE. end_results is NULL is end_type is
     * equal to WASM_BLOCKTYPE_NONE */
    list *end_results;
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
    Fn *ir_func;
    uint8_t *offset;
    OperandStackEntry *opd_stack;
    uint32_t opd_size;
    ControlStackEntry  *ctrl_stack;
    uint32_t ctrl_size;
    Blk *curr_block;
    Ref a0;
    Ref mem0_ptr;
    Ref globals_start;
} CompileCtx;

#define ERR_CHECK(x) if (x) return COMPILE_ERR


/* rega.c */
extern void linear_scan(Fn *f, rv32_reg_pool *gpr, rv32_reg_pool *argr);

static CompileErr_t compile_instr(CompileCtx *ctx, uint8_t opcode);
static void write_local(CompileCtx *ctx, Blk *b, uint32_t localidx, Ref value);
static Ref read_local(CompileCtx *ctx, Blk *b, uint32_t index);
static Ref read_local_rec(CompileCtx *ctx, Blk *b, uint32_t index);
static Ref add_phi_operands(CompileCtx *ctx, listNode *phi_node, uint32_t index);
static Ref try_remove_trivial_phi(CompileCtx *ctx, listNode *phi_node);
static void seal_block(CompileCtx *ctx, Blk *b);
static IRType cast(WASMValtype t);
static Ref get_default(WASMValtype t);

static void push_opd(CompileCtx *ctx, OperandStackEntry *opd) {
    opd->next = ctx->opd_stack;
    ctx->opd_stack = opd;
    ctx->opd_size++;
}

static OperandStackEntry *pop_opd(CompileCtx *ctx) {
    ControlStackEntry *ctrl = ctx->ctrl_stack;
    assert(ctrl != NULL);
    assert(!ctrl->unreachable);
    assert(ctx->opd_size > ctrl->height);
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
    if (ctrl->end_results != NULL) {
        listSetFreeMethod(ctrl->end_results, free);
    }
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
        assert(top->end_results != NULL);
        Phi_arg *pa = malloc(sizeof(struct Phi_arg));
        if (pa == NULL) return NULL;
        OperandStackEntry *opd = pop_opd(ctx);
        pa->r = opd->r;
        pa->b = ctx->curr_block;
        listPush(top->end_results, pa);
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

static ControlStackEntry *ctrl_get(CompileCtx *ctx, uint32_t index) {
    assert(index < ctx->ctrl_size - 1);
    ControlStackEntry *iter = ctx->ctrl_stack;
    for (uint32_t i = 0; i < index; i++) {
        iter = iter->next;
    }
    return iter;
}

static CompileErr_t compile_local_get(CompileCtx *ctx) {
    WASMFunction *f = ctx->wasm_func;
    Blk *b = ctx->curr_block;
    uint32_t localidx;
    readULEB128_u32(&ctx->offset, f->code_end, &localidx);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return COMPILE_OK;
    assert(ctx->curr_block != NULL);

    WASMValtype valtype = wasm_valtype_of(f, localidx);
    Ref local = read_local(ctx, b, localidx);
    /* local can be undefind if there is a read before a write */
    if (local.type == REF_TYPE_UNDEFINED) {
        local = get_default(valtype);
    }

    OperandStackEntry *opd = malloc(sizeof(struct OperandStackEntry));
    if (opd == NULL) return COMPILE_ERR;
    opd->r = local;
    opd->t = valtype;
    push_opd(ctx, opd);

    return COMPILE_OK;
}

static CompileErr_t compile_local_set(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    uint32_t localidx;
    readULEB128_u32(&ctx->offset, f->code_end, &localidx);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return COMPILE_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *opd = pop_opd(ctx);
    write_local(ctx, ctx->curr_block, localidx, opd->r);
    free(opd);

    return COMPILE_OK;
}

static CompileErr_t compile_global_get(CompileCtx *ctx) {

    uint32_t globalidx;
    WASMFunction *f = ctx->wasm_func;
    readULEB128_u32(&ctx->offset, f->code_end, &globalidx);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return COMPILE_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *opd = malloc(sizeof(struct OperandStackEntry));
    if (opd == NULL) return COMPILE_ERR;

    WASMGlobal *g = &ctx->m->globals[globalidx];
    Blk *b = ctx->curr_block;
    Ref r;
    if (g->is_mutable) {
        r = newTemp(ctx->ir_func);
        Ref offset = INT32_CONST(sizeof(uint32_t) * globalidx);
        APPEND_LOAD(b, cast(g->type), r, offset, ctx->globals_start);
    } else {
        r = INT32_CONST(g->as.i32);
    }

    opd->t = g->type;
    opd->r = r;
    push_opd(ctx, opd);

    return COMPILE_OK;
}

static CompileErr_t compile_global_set(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    Blk *b = ctx->curr_block;
    uint32_t globalidx;
    readULEB128_u32(&ctx->offset, f->code_end, &globalidx);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return COMPILE_OK;
    assert(ctx->curr_block != NULL);

    WASMGlobal *g = &ctx->m->globals[globalidx];
    if (!g->is_mutable) return COMPILE_ERR;

    OperandStackEntry *opd = pop_opd(ctx);
    Ref offset = INT32_CONST(sizeof(uint32_t) * globalidx);
    APPEND_STORE(b, cast(g->type), opd->r, offset, ctx->globals_start);
    free(opd);

    return COMPILE_OK;
}

static CompileErr_t compile_call(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    Blk *b = ctx->curr_block;
    uint32_t funcidx;
    readULEB128_u32(&ctx->offset, f->code_end, &funcidx);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return COMPILE_OK;
    assert(ctx->curr_block != NULL);

    WASMFunction *callee = &ctx->m->functions[funcidx];
    WASMFuncType *t = callee->type;

    newFuncCallArg(b, IR_TYPE_I32, ctx->a0);
    uint32_t n = t->param_count;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t argidx = n-i-1;
        OperandStackEntry *iter = ctx->opd_stack;
        for (uint32_t j = 0; j < argidx; j++) {
            iter = iter->next;
        }
        newFuncCallArg(b, cast(t->param_types[argidx]), iter->r);
    }
    for (uint32_t i = 0; i < n; i++) {
        free(pop_opd(ctx));
    }

    Ref callee_name = NEW_NAME(callee->name);
    if (t->result_count == 0) {
        APPEND_VOID_CALL(b, callee_name);
        return COMPILE_OK;
    }

    Ref result = newTemp(ctx->ir_func);
    IRType ret_type = cast(t->result_type);
    APPEND_CALL(b, ret_type, result, callee_name);
    OperandStackEntry *opd = malloc(sizeof(struct OperandStackEntry));
    if (opd == NULL) return COMPILE_ERR;
    opd->r = result;
    opd->t = t->result_type;
    push_opd(ctx, opd);

    return COMPILE_OK;
}

static CompileErr_t compile_br(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    uint32_t labelidx;
    readULEB128_u32(&ctx->offset, f->code_end, &labelidx);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return COMPILE_OK;
    assert(ctx->curr_block != NULL);

    ControlStackEntry *ctrl = ctrl_get(ctx, labelidx);

    if (ctrl->label_type != WASM_BLOCKTYPE_NONE) {
        assert(ctrl->end_results != NULL);
        Phi_arg *pa = malloc(sizeof(struct Phi_arg));
        if (pa == NULL) return COMPILE_ERR;
        OperandStackEntry *opd = pop_opd(ctx);
        pa->r = opd->r;
        pa->b = ctx->curr_block;
        listPush(ctrl->end_results, pa);
        free(opd);
    }

    if (ctrl->label_blk->is_loop_header) {
        listAddNodeTail(ctrl->label_blk->loop_end_blk_list, ctx->curr_block);
    }
    jmp(ctx->ir_func, ctx->curr_block, ctrl->label_blk);
    unreachable(ctx);

    return COMPILE_OK;
}

static CompileErr_t compile_br_if(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    uint32_t labelidx;
    readULEB128_u32(&ctx->offset, f->code_end, &labelidx);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return COMPILE_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *ifcond = pop_opd(ctx);
    ControlStackEntry *ctrl = ctrl_get(ctx, labelidx);

    if (ctrl->label_type != WASM_BLOCKTYPE_NONE) {
        assert(ctrl->end_results != NULL);
        Phi_arg *pa = malloc(sizeof(struct Phi_arg));
        if (pa == NULL) return COMPILE_ERR;
        OperandStackEntry *opd = top_opd(ctx);
        pa->r = opd->r;
        pa->b = ctx->curr_block;
        listPush(ctrl->end_results, pa);
    }

    if (ctrl->label_blk->is_loop_header) {
        listAddNodeTail(ctrl->label_blk->loop_end_blk_list, ctx->curr_block);
    }
    Blk *continue_blk = newBlock(ctx->ir_func);
    jnz(ctx->ir_func, ctx->curr_block, ifcond->r, ctrl->label_blk, continue_blk);
    seal_block(ctx, continue_blk);
    ctx->curr_block = continue_blk;
    free(ifcond);

    return COMPILE_OK;
}

static CompileErr_t compile_return(CompileCtx *ctx) {

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return COMPILE_OK;
    assert(ctx->curr_block != NULL);

    WASMFuncType *t = ctx->wasm_func->type;
    if (t->result_count != 0) {
        OperandStackEntry *opd = pop_opd(ctx);
        retRef(ctx->ir_func, ctx->curr_block, opd->r);
        free(opd);
    } else {
        ret(ctx->ir_func, ctx->curr_block);
    }
    unreachable(ctx);
    return COMPILE_OK;
}

static CompileErr_t compile_if(CompileCtx *ctx) {

    WASMBlocktype end_type;
    read_u8(&ctx->offset, ctx->wasm_func->code_end, &end_type);
    ControlStackEntry *top = top_ctrl(ctx);
    ControlStackEntry *ctrl = malloc(sizeof(struct ControlStackEntry));
    if (ctrl == NULL) return COMPILE_ERR;

    /* if the top of the control stack is in a unreachable state
     * (e.g. if after a br) we want to skip the compilation of the if */
    if (top->unreachable) {
        memset(ctrl, 0, sizeof(struct ControlStackEntry));
        ctrl->unreachable = true;
        ctrl->kind = CTRL_STACK_ENTRY_DUMMY;
        push_ctrl(ctx, ctrl);
        return COMPILE_OK;
    }
    /* if top->unreachable is false, ctx->curr_block must be not NULL */
    assert(ctx->curr_block != NULL);


    OperandStackEntry *ifcond = pop_opd(ctx);
    Blk *then_blk = newBlock(ctx->ir_func);
    Blk *end_blk  = newBlock(ctx->ir_func);
    /* at the moment we don't know if there a matching else branch,
     * hence we put NULL as a else branch block. We will fix the NULL
     * later with the else branch block if there is a matching else,
     * otherwise we will fix the NULL with the if end block */
    jnz(ctx->ir_func, ctx->curr_block, ifcond->r, then_blk, NULL);
    seal_block(ctx, then_blk);
    free(ifcond);

    list *end_results = NULL;
    if (end_type != WASM_BLOCKTYPE_NONE) {
        end_results = listCreate();
    }
    ctrl->kind = CTRL_STACK_ENTRY_IF;
    ctrl->label_type = end_type;
    ctrl->end_type = end_type;
    ctrl->label_blk = end_blk;
    ctrl->start_blk = ctx->curr_block;
    ctrl->end_results = end_results;
    ctrl->unreachable = false;
    push_ctrl(ctx, ctrl);
    ctx->curr_block = then_blk;

    return COMPILE_OK;
}

static CompileErr_t compile_else(CompileCtx *ctx) {

    ControlStackEntry *top;
    top = pop_ctrl(ctx);
    if (top == NULL) return COMPILE_ERR;

    /* if we had skipped the compilation of a then branch because
     * the top of the control stack was in a unreachable state at
     * the start of the if compilation, skip also the compilation
     * of the else branch */
    if (top->kind == CTRL_STACK_ENTRY_DUMMY) {
        push_ctrl(ctx, top);
        return COMPILE_OK;
    }

    if (!top->unreachable) {
        assert(ctx->curr_block != NULL);
        jmp(ctx->ir_func, ctx->curr_block, top->label_blk);
    }
    top->kind = CTRL_STACK_ENTRY_ELSE;
    top->unreachable = false;
    push_ctrl(ctx, top);

    /* fix the NULL in the jnz */
    Blk *else_blk  = newBlock(ctx->ir_func);
    Blk *b = top->start_blk;
    b->succ[1] = else_blk;
    listAddNodeTail(else_blk->preds, b);

    seal_block(ctx, else_blk);
    ctx->curr_block = else_blk;

    return COMPILE_OK;
}

static CompileErr_t compile_end(CompileCtx *ctx) {

    ControlStackEntry *top = pop_ctrl(ctx);
    if (top == NULL) return COMPILE_ERR;
    Blk *s = top->start_blk;
    Blk *l = top->label_blk;
    list *res = top->end_results;
    WASMBlocktype t = top->end_type;

    /* if we had skipped the compilation of a if/block/loop because
     * the top of the control stack was in a unreachable state at
     * the start of the if/block/loop compilation, just return */
    if (top->kind == CTRL_STACK_ENTRY_DUMMY) {
        free(top);
        return COMPILE_OK;
    }

    seal_block(ctx, l);
    if (top->kind != CTRL_STACK_ENTRY_LOOP) {
        if (ctx->curr_block != NULL) {
            jmp(ctx->ir_func, ctx->curr_block, l);
        }
        ctx->curr_block = l;
    }

    /* fix the NULL of the jnz if this is the
     * end of a if without a matching else */
    if (top->kind == CTRL_STACK_ENTRY_IF) {
        s->succ[1] = l;
        listAddNodeTail(l->preds, s);
    }

    if (t != WASM_BLOCKTYPE_NONE) {
        assert(res != NULL);
        uint32_t n = listLength(res);
        OperandStackEntry *opd = malloc(sizeof(struct OperandStackEntry));
        if (opd == NULL) return COMPILE_ERR;
        opd->t = (WASMValtype)t;
        if (n == 1) {
            Phi_arg *pa = listNodeValue(listFirst(res));
            opd->r = pa->r;
            listRelease(res);
        } else if (n > 1) {
            opd->r = newTemp(ctx->ir_func);
            listNode *phi_node;
            phi_node = newPhi(ctx->curr_block, opd->r, cast((WASMValtype)t));
            Phi *phi = listNodeValue(phi_node);
            phi->phi_arg_list = res;
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
            opd->r = get_default((WASMValtype)t);
            listRelease(res);
        }
        push_opd(ctx, opd);
    }

    if (top->kind == CTRL_STACK_ENTRY_LOOP) {
        if (listLength(l->loop_end_blk_list) == 0) {
            listRelease(l->loop_end_blk_list);
            l->loop_end_blk_list = NULL;
            l->is_loop_header = false;
        }
        if (ctx->curr_block == NULL) {
         /* When the body of loop end with a br or return statement
            ctx->curr_block is NULL. If there are some code after the
            loop but in the same scope of the loop statement we need
            a new blk to continue the compilation. The code after the
            loop is most probably death code. */
            if (ctx->offset[0] != WASM_OPCODE_END ||
                ctx->offset == ctx->wasm_func->code_end) {
                Blk *continue_blk = newBlock(ctx->ir_func);
                ctx->curr_block = continue_blk;
                seal_block(ctx, continue_blk);
            }
        }
    }

    free(top);
    return COMPILE_OK;
}

static CompileErr_t compile_block(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    WASMBlocktype end_type;
    read_u8(&ctx->offset, f->code_end, &end_type);
    ControlStackEntry *top = top_ctrl(ctx);
    ControlStackEntry *ctrl = malloc(sizeof(struct ControlStackEntry));
    if (ctrl == NULL) return COMPILE_ERR;

    /* if the top of the control stack is in a unreachable state
     * (e.g. block after a br) we want to skip the compilation of the block */
    if (top->unreachable) {
        memset(ctrl, 0, sizeof(struct ControlStackEntry));
        ctrl->kind = CTRL_STACK_ENTRY_DUMMY;
        ctrl->unreachable = true;
        push_ctrl(ctx, ctrl);
        return COMPILE_OK;
    }
    /* if top->unreachable is false, ctx->curr_block must be not NULL */
    assert(ctx->curr_block != NULL);

    Blk *end_blk = newBlock(ctx->ir_func);
    list *end_results = NULL;
    if (end_type != WASM_BLOCKTYPE_NONE) {
        end_results = listCreate();
    }
    ctrl->kind = CTRL_STACK_ENTRY_BLOCK;
    ctrl->label_type = end_type;
    ctrl->end_type = end_type;
    ctrl->label_blk = end_blk;
    ctrl->start_blk = ctx->curr_block;
    ctrl->end_results = end_results;
    ctrl->unreachable = false;
    push_ctrl(ctx, ctrl);

    return COMPILE_OK;
}

static CompileErr_t compile_loop(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    WASMBlocktype end_type;
    read_u8(&ctx->offset, f->code_end, &end_type);
    ControlStackEntry *top = top_ctrl(ctx);
    ControlStackEntry *ctrl = malloc(sizeof(struct ControlStackEntry));
    if (ctrl == NULL) return COMPILE_ERR;

    /* if the top of the control stack is in a unreachable state
     * (e.g. loop after a br) we want to skip the compilation of the loop */
    if (top->unreachable) {
        memset(ctrl, 0, sizeof(struct ControlStackEntry));
        ctrl->unreachable = true;
        ctrl->kind = CTRL_STACK_ENTRY_DUMMY;
        push_ctrl(ctx, ctrl);
        return COMPILE_OK;
    }
    /* if top->unreachable is false, ctx->curr_block must be not NULL */
    assert(ctx->curr_block != NULL);

    Blk *loop_header = newBlock(ctx->ir_func);
    loop_header->is_loop_header = true;
    loop_header->loop_end_blk_list = listCreate();
    jmp(ctx->ir_func, ctx->curr_block, loop_header);
    ctx->curr_block = loop_header;

    list *end_results = NULL;
    if (end_type != WASM_BLOCKTYPE_NONE) {
        end_results = listCreate();
    }
    ctrl->kind = CTRL_STACK_ENTRY_LOOP;
    ctrl->label_type  = WASM_BLOCKTYPE_NONE;
    ctrl->end_type    = end_type;
    ctrl->label_blk   = loop_header;
    ctrl->start_blk   = ctx->curr_block;
    ctrl->end_results = end_results;
    ctrl->unreachable = false;
    push_ctrl(ctx, ctrl);

    return COMPILE_OK;
}

static CompileErr_t compile_drop(CompileCtx *ctx) {

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return COMPILE_OK;
    assert(ctx->curr_block != NULL);

    free(pop_opd(ctx));
    return COMPILE_OK;
}

static CompileErr_t compile_select(CompileCtx *ctx) {

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return COMPILE_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *result = malloc(sizeof(struct OperandStackEntry));
    if (result == NULL) return COMPILE_ERR;

    OperandStackEntry *cond, *arg1, *arg2;
    cond = pop_opd(ctx);
    arg2 = pop_opd(ctx);
    arg1 = pop_opd(ctx);

    Blk *select_arg1 = newBlock(ctx->ir_func);
    Blk *select_arg2 = newBlock(ctx->ir_func);
    Blk *end = newBlock(ctx->ir_func);

    assert(ctx->curr_block != NULL);
    jnz(ctx->ir_func, ctx->curr_block, cond->r, select_arg1, select_arg2);
    free(cond);

    jmp(ctx->ir_func, select_arg1, end);
    jmp(ctx->ir_func, select_arg2, end);
    ctx->curr_block = end;

    result->t = arg1->t;
    result->r = newTemp(ctx->ir_func);
    listNode *phi_node = newPhi(end, result->r, cast(result->t));
    phiAppendOperand(phi_node, select_arg1, arg1->r);
    phiAppendOperand(phi_node, select_arg2, arg2->r);

    free(arg1);
    free(arg2);
    push_opd(ctx, result);

    return COMPILE_OK;
}

static CompileErr_t compile_load(CompileCtx *ctx, WASMValtype t, IROpcode op) {

    WASMFunction *f = ctx->wasm_func;
    Blk *b = ctx->curr_block;
    //TODO: how to properly use align?
    uint32_t align, offset;
    readULEB128_u32(&ctx->offset, f->code_end, &align);
    readULEB128_u32(&ctx->offset, f->code_end, &offset);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return COMPILE_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *result = malloc(sizeof(struct OperandStackEntry));
    if (result == NULL) return COMPILE_ERR;

    OperandStackEntry *memidx = pop_opd(ctx);
    Ref ptr = newTemp(ctx->ir_func);
    APPEND_ADD(b, IR_TYPE_I32, ptr, ctx->mem0_ptr, memidx->r);
    free(memidx);
    //TODO: out of bound memory check

    Ref r = newTemp(ctx->ir_func);
    ir_append_ins(b, op, cast(t), r, INT32_CONST(offset), ptr);

    result->t = t;
    result->r = r;
    push_opd(ctx, result);

    return COMPILE_OK;
}

static CompileErr_t compile_store(CompileCtx *ctx, WASMValtype t, IROpcode op) {

    WASMFunction *f = ctx->wasm_func;
    Blk *b = ctx->curr_block;
    //TODO: how to properly use align?
    uint32_t align, offset;
    readULEB128_u32(&ctx->offset, f->code_end, &align);
    readULEB128_u32(&ctx->offset, f->code_end, &offset);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return COMPILE_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *value, *memidx;
    value  = pop_opd(ctx);
    memidx = pop_opd(ctx);

    Ref ptr = newTemp(ctx->ir_func);
    APPEND_ADD(b, IR_TYPE_I32, ptr, ctx->mem0_ptr, memidx->r);
    ir_append_ins(b, op, cast(t), value->r, INT32_CONST(offset), ptr);
    free(value);
    free(memidx);

    return COMPILE_OK;
}

static CompileErr_t compile_binop(CompileCtx *ctx, IROpcode op) {

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return COMPILE_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *result = malloc(sizeof(struct OperandStackEntry));
    if (result == NULL) return COMPILE_ERR;

    Blk *b = ctx->curr_block;
    OperandStackEntry *arg1, *arg2;
    arg2 = pop_opd(ctx);
    arg1 = pop_opd(ctx);

    result->r = newTemp(ctx->ir_func);
    result->t = arg1->t;
    ir_append_ins(b, op, cast(arg1->t), result->r, arg1->r, arg2->r);

    free(arg1);
    free(arg2);
    push_opd(ctx, result);

    return COMPILE_OK;
}

static CompileErr_t compile_testop(CompileCtx *ctx, IROpcode op) {

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return COMPILE_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *result = malloc(sizeof(struct OperandStackEntry));
    if (result == NULL) return COMPILE_ERR;

    Blk *b = ctx->curr_block;
    OperandStackEntry *v = pop_opd(ctx);
    result->r = newTemp(ctx->ir_func);
    result->t = WASM_VALTYPE_I32;
    ir_append_ins(b, op, IR_TYPE_I32, result->r, v->r, UNDEFINED_REF);

    free(v);
    push_opd(ctx, result);

    return COMPILE_OK;
}

static CompileErr_t compile_relop(CompileCtx *ctx, IROpcode op) {

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return COMPILE_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *result = malloc(sizeof(struct OperandStackEntry));
    if (result == NULL) return COMPILE_ERR;

    Blk *b = ctx->curr_block;
    OperandStackEntry *arg2 = pop_opd(ctx);
    OperandStackEntry *arg1 = pop_opd(ctx);

    result->r = newTemp(ctx->ir_func);
    result->t = WASM_VALTYPE_I32;
    ir_append_ins(b, op, IR_TYPE_I32, result->r, arg1->r, arg2->r);

    free(arg1);
    free(arg2);
    push_opd(ctx, result);

    return COMPILE_OK;
}

static CompileErr_t compile_i32const(CompileCtx *ctx) {

    int32_t n;
    readILEB128_i32(&ctx->offset, ctx->wasm_func->code_end, &n);

    ControlStackEntry *top = top_ctrl(ctx);
    if (top->unreachable) return COMPILE_OK;
    assert(ctx->curr_block != NULL);

    OperandStackEntry *opd = malloc(sizeof(struct OperandStackEntry));
    if (opd == NULL) return COMPILE_ERR;
    opd->t = WASM_VALTYPE_I32;
    opd->r = INT32_CONST(n);
    push_opd(ctx, opd);

    return COMPILE_OK;
}



static CompileErr_t compile_instr(CompileCtx *ctx, uint8_t opcode) {

    switch (opcode) {
    /* Control instructions */
    case WASM_OPCODE_UNREACHABLE:
        halt(ctx->ir_func, ctx->curr_block);
        return COMPILE_OK;
    case WASM_OPCODE_NOP:
        return COMPILE_OK;
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
    case WASM_OPCODE_GLOBAL_GET:
        return compile_global_get(ctx);
    case WASM_OPCODE_GLOBAL_SET:
        return compile_global_set(ctx);

    /* Memory instructions */
    case WASM_OPCODE_I32_LOAD:
        return compile_load(ctx, WASM_VALTYPE_I32, IR_OPCODE_LOAD);
    case WASM_OPCODE_I32_STORE:
        return compile_store(ctx, WASM_VALTYPE_I32, IR_OPCODE_STORE);
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
        return COMPILE_ERR;
    }

    return COMPILE_OK;
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
        free(ctrl);
        ctrl = next;
    }

    /* free IR funtion */
    freeFunc(ctx->ir_func);
}

static Fn *compile_fn(WASMModule *m, uint32_t funcidx) {

    assert(funcidx < m->function_count);
    WASMFunction *wasm_func = &m->functions[funcidx];
    WASMFuncType *t = wasm_func->type;
    uint32_t local_count = t->param_count + wasm_func->local_count;

    /* Allocate the IR function */
    IRType ret_type = IR_TYPE_VOID;
    if (t->result_count != 0) {
        ret_type = cast(t->result_type);
    }
    Fn *ir_func = newFunc(ret_type, wasm_func->name, local_count);
    Blk *start = newBlock(ir_func);
    ir_func->start = start;

    /* Initialize function parameters, the first parameter (a0)
     * always hold a pointer to the struct WASMExecEnv */
    Ref WASMExecEnv_ptr = newFuncParam(ir_func, IR_TYPE_I32);
    for (uint32_t i = 0; i < t->param_count; i++) {
        Ref param = newFuncParam(ir_func, cast(wasm_valtype_of(wasm_func, i)));
        start->locals[i] = param;
    }

    /* Initialize function locals */
    Ref zero = INT32_CONST(0);
    for (uint32_t i = 0; i < wasm_func->local_count; i++) {
        start->locals[t->param_count + i] = zero;
    }

    Ref WASMModuleInstance_ptr = UNDEFINED_REF;
    if (m->memories_count > 0 || m->global_count > 0) {
        WASMModuleInstance_ptr = newTemp(ir_func);
        APPEND_LOAD(start, IR_TYPE_I32, WASMModuleInstance_ptr,
                    INT32_CONST(8), WASMExecEnv_ptr);
    }
    /* mem0_ptr hold the pointer to the memory 0 in the WAMR AOT runtime */
    Ref mem0_ptr = UNDEFINED_REF;
    if (m->memories_count > 0) {
        mem0_ptr = newTemp(ir_func);
        APPEND_LOAD(start, IR_TYPE_I32, mem0_ptr,
                    INT32_CONST(376), WASMModuleInstance_ptr);
    }
    /* globals_start hold the pointer to the start of the
     * array of globals variables in the WAMR AOT runtime */
    Ref globals_start = UNDEFINED_REF;
    if (m->global_count > 0) {
        globals_start = newTemp(ir_func);
        APPEND_ADD(start, IR_TYPE_I32, globals_start,
                   WASMModuleInstance_ptr, INT32_CONST(464));
    }

    CompileCtx ctx = {0};
    ctx.m = m;
    ctx.funcidx = funcidx;
    ctx.wasm_func = wasm_func;
    ctx.ir_func = ir_func;
    ctx.offset = wasm_func->code_start;
    ctx.curr_block = start;
    ctx.a0 = WASMExecEnv_ptr;
    ctx.mem0_ptr = mem0_ptr;
    ctx.globals_start = globals_start;

    seal_block(&ctx, start);

    ControlStackEntry *ctrl = malloc(sizeof(struct ControlStackEntry));
    if (ctrl == NULL) goto ERROR;
    memset(ctrl, 0, sizeof(struct ControlStackEntry));
    ctrl->end_type = WASM_BLOCKTYPE_NONE;
    push_ctrl(&ctx, ctrl);

    /* compile the body of the function */
    uint8_t opcode;
    while (ctx.offset < wasm_func->code_end) {
        if (read_u8(&ctx.offset, wasm_func->code_end, &opcode)) goto ERROR;
        if (compile_instr(&ctx, opcode)) goto ERROR;
    }

    /* Add an implicit return if there is
     * no explicit return in the wasm code */
    compile_return(&ctx);
    free(pop_ctrl(&ctx));
    return ir_func;

ERROR:
    cleanup(&ctx);
    return NULL;
}

static void register_allocation(Fn *fn) {
    rv32_reg_pool gpr;
    memset(gpr.pool, false, RV32_NUM_REG);
    gpr.size = RV32_GP_NUM_REG;
    for (unsigned int i = 0; i < RV32_GP_NUM_REG; i++) {
        gpr.pool[rv32_gp_reg[i]] = true;
    }
    rv32_reg_pool argr;
    memset(argr.pool, false, RV32_NUM_REG);
    argr.size = RV32_ARG_NUM_REG;
        for (unsigned int i = 0; i < RV32_ARG_NUM_REG; i++) {
        argr.pool[rv32_arg_reg[i]] = true;
    }
    linear_scan(fn, &gpr, &argr);
}

CompileErr_t compile(WASMModule *wasm_mod, Target *T,
             uint8_t **out_buf, uint32_t *out_len) {

    AOTModule aot_mod;
    ERR_CHECK(aot_module_init(&aot_mod, wasm_mod, T));
    for (uint32_t i = 0; i < wasm_mod->function_count; i++) {
        Fn *fn = compile_fn(wasm_mod, i);
        if (fn == NULL) return COMPILE_ERR;
        printfn(fn, stdout);
        register_allocation(fn);
        WASMFunction *wf = &wasm_mod->functions[i];
        ERR_CHECK(T->emitfn(&aot_mod, fn, wf->typeidx));
        freeFunc(fn);
    }
    ERR_CHECK(aot_module_finalize(&aot_mod, out_buf, out_len));
    return COMPILE_OK;
}


static Ref get_default(WASMValtype t) {
    switch(t) {
    case WASM_VALTYPE_I32:
        return INT32_CONST(0);
    case WASM_VALTYPE_I64:
    case WASM_VALTYPE_F32:
    case WASM_VALTYPE_F64:
        /* not implemented */
        assert(0);
    default:
        assert(0);
    }
}

static IRType cast(WASMValtype t) {
    switch(t) {
        case WASM_VALTYPE_I32:
            return IR_TYPE_I32;
        case WASM_VALTYPE_I64:
            return IR_TYPE_I64;
        case WASM_VALTYPE_F32:
            return IR_TYPE_F32;
        case WASM_VALTYPE_F64:
            return IR_TYPE_F64;
        default:
            assert(0);
    }
}

/* The following SSA construction algorithm is taken from this article:
 * Simple and Efficient Construction of Static Single Assignment Form.
 * Authors: Matthias Braun, Sebastian Buchwald, Sebastian Hack,
 * Roland Leißa, Christoph Mallon, and Andreas Zwinkau */

static void write_local(CompileCtx *ctx, Blk *b, uint32_t localidx, Ref value) {

    (void)ctx;
    b->locals[localidx] = value;

    Ref *ptr = &b->locals[localidx];
    Use_ptr u = { .local = ptr };
    if (ptr->type == REF_TYPE_TMP) {
        rmUsage(listNodeValue(ptr->as.tmp_node), ULocal, u);
    }
    if (value.type == REF_TYPE_TMP) {
        addUsage(listNodeValue(value.as.tmp_node), ULocal, u);
    }
}

static Ref read_local(CompileCtx *ctx, Blk *b, uint32_t localidx) {
    if (b->locals[localidx].type != REF_TYPE_UNDEFINED) {
        return b->locals[localidx];
    }
    /* If a block currently contains no definition for a variable,
    * we recursively look for a definition in its predecessors. */
    return read_local_rec(ctx, b, localidx);
}

static Ref read_local_rec(CompileCtx *ctx, Blk *b, uint32_t localidx) {
   /* If the block b has a single predecessor (case 2), just query it
    * recursively for a definition. Otherwise (case 3), we collect the
    * definitions from all predecessors and construct a phi function,
    * which joins them into a single new value. This phi function is
    * recorded as current definition in this basic block.
    * In both cases, looking for a value in a predecessor might in
    * turn lead to further recursive look-ups.*/
    Ref value;
    if (!b->is_sealed) {
        /* (case 1) Incomplete CFG */
        Ref temp = newTemp(ctx->ir_func);
        WASMValtype valtype = wasm_valtype_of(ctx->wasm_func, localidx);
        listNode *phi_node = newPhi(b, temp, cast(valtype));
        b->incomplete_phis[localidx] = phi_node;
        value = temp;
    } else if (listLength(b->preds) == 1) {
        /* (case 2) The block b has a single predecessor, no phi needed */
        Blk *p = listNodeValue(listFirst(b->preds));
        value = read_local(ctx, p, localidx);
    } else {
        /* (case 3) The block b has Multiple predecessors */

        /* Break potential cycles with operandless phi:
         * looking for a value in a predecessor might in turn lead to further
         * recursive look-ups. Due to loops in the program, those might lead
         * to endless recursion. Therefore, before recursing, we first create
         * the phi function without operands and record it as the current
         * definition for the variable in the block. Then, we determine the
         * phi function’s operands. If a recursive look-up arrives back at the
         * block, this phi function will provide a definition and the recursion
         * will end. */
        Ref temp = newTemp(ctx->ir_func);
        WASMValtype valtype = wasm_valtype_of(ctx->wasm_func, localidx);
        listNode *phi_node = newPhi(b, temp, cast(valtype));
        write_local(ctx, b, localidx, temp);
        value = add_phi_operands(ctx, phi_node, localidx);
    }
    write_local(ctx, b, localidx, value);
    return value;
}

static Ref add_phi_operands(CompileCtx *ctx, listNode *phi_node, uint32_t index) {
    /* Determine phi operands from the predecessors */
    Phi *phi = listNodeValue(phi_node);
    Blk *b = phi->block;
    listNode *node;
    listNode *iter = listFirst(b->preds);
    while ((node = listNext(&iter)) != NULL) {
        Blk *pred = listNodeValue(node);
        Ref l = read_local(ctx, pred, index);
        if (l.type != REF_TYPE_UNDEFINED) {
            phiAppendOperand(phi_node, pred, l);
        }
    }
    /* Recursive look-up might leave redundant
     * phi functions, try to remove them */
    return try_remove_trivial_phi(ctx, phi_node);
}

static void phi_replace_by(listNode *phi_node, Ref r) {

    Phi *phi = listNodeValue(phi_node);
    Tmp *to = listNodeValue(phi->to.as.tmp_node);
    listNode *use_node;
    listNode *use_iter = listFirst(to->use_list);
    while ((use_node = listNext(&use_iter)) != NULL) {
        Use *use = listNodeValue(use_node);
        switch (use->type) {
            case UPhi: {
                Phi *p = listNodeValue(use->u.phi);
                listNode *phi_arg_node;
                listNode *phi_arg_iter = listFirst(p->phi_arg_list);
                while ((phi_arg_node = listNext(&phi_arg_iter)) != NULL) {
                    Phi_arg *phi_arg = listNodeValue(phi_arg_node);
                    if (REF_EQ(phi_arg->r, phi->to)) {
                        phi_arg->r = r;
                        if (r.as.tmp_node != NULL) {
                            addUsage(listNodeValue(r.as.tmp_node), UPhi, use->u);
                        }
                    }
                }
            } break;
            case UIns: {
                Ins *i = listNodeValue(use->u.ins);
                for (unsigned int j = 0; j < 2; j++) {
                    if (REF_EQ(i->arg[j], phi->to)) {
                        i->arg[j] = r;
                        if (r.as.tmp_node != NULL) {
                            addUsage(listNodeValue(r.as.tmp_node), UIns, use->u);
                        }
                    }
                }
            } break;
            case UJmp: {
                Blk *b = listNodeValue(use->u.blk);
                b->jmp.arg = r;
                if (r.as.tmp_node != NULL) {
                    addUsage(listNodeValue(r.as.tmp_node), UJmp, use->u);
                }
            } break;
            case ULocal: {
                Ref *l = use->u.local;
                *l = r;
                if (r.as.tmp_node != NULL) {
                    addUsage(listNodeValue(r.as.tmp_node), ULocal, use->u);
                }
            } break;
            default:
                assert(0);
        }
    }
}

/* A phi function is trivial iff it just references itself
 * and one other value v any number of times: exists v such
 * that t = phi(x1, x2, ..., xn) and x1 ..., xn in {t, v}.
 *
 * Examples of trivial phi:
 * - t = phi()
 * - t = phi(t)
 * - t = phi(t, t, t, t)
 * - t = phi(v)
 * - t = phi(v, v, v)
 * - t = phi(t, v, t, t)
 * - t = phi(t, v, t, v, t, v)
 *
 * Examples of not trivial phi:
 * - t = phi(v1, v2)
 * - t = phi(t, v1, t, v2)
 * 
 * A trivial phi function can be removed and the value v is used instead.
 * As a special case, the phi function might use no other value besides
 * itself. This means that it is either unreachable or in the start block.
 * We replace it by an undefined value. */
static Ref try_remove_trivial_phi(CompileCtx *ctx, listNode *phi_node) {
    Phi *phi = listNodeValue(phi_node);
    Ref same = UNDEFINED_REF;
    listNode *phi_arg_node;
    listNode *phi_arg_iter = NULL;
    if (phi->phi_arg_list != NULL) {
        phi_arg_iter = listFirst(phi->phi_arg_list);
    }
    while ((phi_arg_node = listNext(&phi_arg_iter)) != NULL) {
        Phi_arg *op = listNodeValue(phi_arg_node);
        if (REF_EQ(same, op->r) || REF_EQ(op->r, phi->to)) {
            /* Unique value or self-reference */
            continue;
        }
        if (same.type != REF_TYPE_UNDEFINED) {
            /* The phi merges at least two values: not trivial */
            return phi->to;
        }
        same = op->r;
    }

    assert(phi->to.type == REF_TYPE_TMP);
    listNode *to_node = phi->to.as.tmp_node;
    Tmp *to = listNodeValue(to_node);

    /* Reroute all uses of phi to same */
    phi_replace_by(phi_node, same);
    /* remove 'phi' */
    if (same.type == REF_TYPE_TMP && same.as.tmp_node != NULL) {
        Use_ptr u = { .phi = phi_node };
        rmUsage(listNodeValue(same.as.tmp_node), UPhi, u);
    }
    listDelNode(phi->block->phi_list, phi_node);

    /* Try to recursively remove all phi users,
     * which might have become trivial */
    listNode *use_node;
    listNode *use_iter = listFirst(to->use_list);
    while ((use_node = listNext(&use_iter)) != NULL) {
        Use *use = listNodeValue(use_node);
        if (use->type == UPhi && use->u.phi != phi_node) {
            try_remove_trivial_phi(ctx, use->u.phi);
        }
        listDelNode(to->use_list, use_node);
    }

    /* remove 'to' */
    listDelNode(ctx->ir_func->tmp_list, to_node);
    return same;
}

static void seal_block(CompileCtx *ctx, Blk *b) {
    assert(b->is_sealed == 0);
    for (uint32_t i = 0; i < ctx->ir_func->local_count; i++) {
        if (b->incomplete_phis[i] == NULL) continue;
        add_phi_operands(ctx, b->incomplete_phis[i], i);
    }
    b->is_sealed = true;
}

