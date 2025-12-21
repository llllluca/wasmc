#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "wasm.h"
#include "libqbe.h"
#include "listx.h"
#include "aot.h"
#include "compile.h"

typedef struct ValueStackEntry {
    struct list_head next;
    WASMValtype valtype;
    Ref r;
} ValueStackEntry;

typedef struct LabelStackEntry {
    struct list_head next;
    list *results;
    Blk *b;
    WASMBlocktype blktype;
} LabelStackEntry;

typedef enum SkipFlag {
    SKIP_FLAG_NONE,
    SKIP_FLAG_BR,
    SKIP_FLAG_RETURN,
} SkipFlag;

typedef struct CompileCtx {
    WASMModule *m;
    uint32_t funcidx;
    WASMFunction *wasm_func;
    Fn *ir_func;
    uint32_t local_count;
    uint8_t *offset;
    SkipFlag skip_flag;
    struct list_head value_stack;
    uint32_t value_stack_length;
    struct list_head label_stack;
    uint32_t label_stack_length;
    Blk *curr_block;
    Ref a0;
    Ref mem0_ptr;
    Ref globals_start;
} CompileCtx;

#define ERR_CHECK(x) if (x) return COMPILE_ERR

#define INIT_BOTTOM(x)                                \
    do {                                              \
        (x) = malloc(sizeof(struct ValueStackEntry)); \
        if ((x) == NULL) return COMPILE_ERR;          \
        (x)->valtype = -1;                            \
        (x)->r = UNDEFINED_REF;                       \
        if ((x) == NULL) return COMPILE_ERR;          \
    } while (0);

#define INIT_LABEL(x, block, type)                    \
    do {                                              \
        (x) = malloc(sizeof(struct LabelStackEntry)); \
        if ((x) == NULL) return COMPILE_ERR;          \
        (x)->b = block;                               \
        (x)->blktype = type;                          \
        (x)->results = listCreate();                  \
        listSetFreeMethod((x)->results, free);        \
    } while (0);

/* rega.c */
extern void linear_scan(Fn *f, rv32_reg_pool *gpr, rv32_reg_pool *argr);

static CompileErr_t compile_instr(CompileCtx *ctx, uint8_t opcode);
static void write_local(Blk *b, uint32_t index, Ref value);
static Ref read_local(CompileCtx *ctx, Blk *b, uint32_t index);
static Ref read_local_rec(CompileCtx *ctx, Blk *b, uint32_t index);
static Ref add_phi_operands(CompileCtx *ctx, listNode *phi_node, uint32_t index);
static Ref try_remove_trivial_phi(CompileCtx *ctx, listNode *phi_node);
static void seal_block(CompileCtx *ctx, Blk *b);
static IRType cast(WASMValtype t);

static CompileErr_t label_stack_pop(CompileCtx *ctx, LabelStackEntry *l) {
    if (list_empty(&ctx->label_stack)) return COMPILE_ERR;
    struct list_head *next = list_next(&ctx->label_stack);
    list_del(next);
    LabelStackEntry *top = (LabelStackEntry *)next;
    if (l != NULL) *l = *top;
    listRelease(top->results);
    free(top);
    ctx->label_stack_length--;
    return COMPILE_OK;
}

static LabelStackEntry *label_stack_get(CompileCtx *ctx, uint32_t index) {
    uint32_t n = index + 1;
    if (n > ctx->label_stack_length) return NULL;

    struct list_head *iter = &ctx->label_stack;
    for (uint32_t i = 0; i < n; i++) {
        iter = list_next(iter);
    }
    return (LabelStackEntry *)iter;
}

static void label_stack_push(CompileCtx *ctx, LabelStackEntry *l) {
    list_add(&l->next, &ctx->label_stack);
    ctx->label_stack_length++;
}

static LabelStackEntry *label_stack_top(CompileCtx *ctx) {
    if (list_empty(&ctx->label_stack)) return NULL;
    return (LabelStackEntry *)list_next(&ctx->label_stack);
}

static void value_stack_push(CompileCtx *ctx, ValueStackEntry *v) {
    list_add(&v->next, &ctx->value_stack);
    ctx->value_stack_length++;
}

static CompileErr_t value_stack_pop(CompileCtx *ctx, ValueStackEntry *v) {
    if (list_empty(&ctx->value_stack)) return COMPILE_ERR;
    struct list_head *next = list_next(&ctx->value_stack);
    list_del(next);
    ValueStackEntry *top = (ValueStackEntry *)next;
    if (v != NULL) *v = *top;
    free(top);
    ctx->value_stack_length--;
    return COMPILE_OK;
}

static ValueStackEntry *value_stack_top(CompileCtx *ctx) {
    if (list_empty(&ctx->value_stack)) return NULL;
    return (ValueStackEntry *)list_next(&ctx->value_stack);
}

static CompileErr_t compile_local_get(CompileCtx *ctx) {
    uint32_t localidx;
    WASMFunction *f = ctx->wasm_func;
    ERR_CHECK(readULEB128_u32(&ctx->offset, f->code_end, &localidx));
    Ref local = read_local(ctx, ctx->curr_block, localidx);
    if (local.type == REF_TYPE_UNDEFINED) {
        local = INT32_CONST(0);
    }
    ValueStackEntry *v = malloc(sizeof(struct ValueStackEntry));
    if (v == NULL) return COMPILE_ERR;
    v->valtype = wasm_valtype_of(ctx->wasm_func, localidx);
    v->r = local;
    value_stack_push(ctx, v);
    return COMPILE_OK;
}

static CompileErr_t compile_local_set(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    uint32_t index;
    ValueStackEntry v;
    ERR_CHECK(value_stack_pop(ctx, &v));
    ERR_CHECK(readULEB128_u32(&ctx->offset, f->code_end, &index));
    WASMValtype valtype = wasm_valtype_of(f, index);
    if (v.valtype != valtype) return COMPILE_ERR;
    write_local(ctx->curr_block, index, v.r);
    return COMPILE_OK;
}

static CompileErr_t compile_global_get(CompileCtx *ctx) {

    uint32_t globalidx;
    WASMFunction *f = ctx->wasm_func;
    ERR_CHECK(readULEB128_u32(&ctx->offset, f->code_end, &globalidx));
    if (globalidx >= ctx->m->global_count) return COMPILE_ERR;
    WASMGlobal *g = &ctx->m->globals[globalidx];

    Blk *b = ctx->curr_block;
    Ref r;
    if (g->is_mutable) {
        r = newTemp(ctx->ir_func);
        LOADI32(b, r, INT32_CONST(sizeof(uint32_t) * globalidx), ctx->globals_start);
    } else {
        r = INT32_CONST(g->as.i32);
    }

    ValueStackEntry *v = malloc(sizeof(struct ValueStackEntry));
    if (v == NULL) return COMPILE_ERR;
    v->valtype = g->type;
    v->r = r;
    value_stack_push(ctx, v);
    return COMPILE_OK;
}

static CompileErr_t compile_global_set(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    uint32_t globalidx;
    ERR_CHECK(readULEB128_u32(&ctx->offset, f->code_end, &globalidx));
    if (globalidx >= ctx->m->global_count) return COMPILE_ERR;
    WASMGlobal *g = &ctx->m->globals[globalidx];
    if (!g->is_mutable) return COMPILE_ERR;

    ValueStackEntry v;
    ERR_CHECK(value_stack_pop(ctx, &v));
    if (v.valtype != g->type) return COMPILE_ERR;
    STOREI32(ctx->curr_block, v.r, INT32_CONST(sizeof(uint32_t) * globalidx), ctx->globals_start);
    return COMPILE_OK;
}

static CompileErr_t compile_call(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    uint32_t funcidx;
    ERR_CHECK(readULEB128_u32(&ctx->offset, f->code_end, &funcidx));
    if (funcidx >= ctx->m->function_count) return COMPILE_ERR;

    WASMFunction *called_func = &ctx->m->functions[funcidx];
    WASMFuncType *t = called_func->type;

    if (ctx->value_stack_length < t->param_count) {
        return COMPILE_ERR;
    }
    newFuncCallArg(ctx->curr_block, IR_TYPE_INT32, ctx->a0);
    if (t->param_count > 0) {
        uint32_t n = t->param_count;
        if (ctx->value_stack_length < n) return COMPILE_ERR;
        struct list_head *iter = &ctx->value_stack;
        for (uint32_t i = 0; i < n; i++) {
            iter = list_next(iter);
        }

        for (uint32_t i = 0; i < n; i++) {
            ValueStackEntry *v = (ValueStackEntry *)iter;
            if (v->valtype != t->param_types[i]) return COMPILE_ERR;
            newFuncCallArg(ctx->curr_block, cast(t->param_types[i]), v->r);
            iter = list_prev(iter);
        }

        for (uint32_t i = 0; i < n; i++) {
            ERR_CHECK(value_stack_pop(ctx, NULL));
        }
    }

    Ref called_addr = NEW_NAME(called_func->name);
    if (t->result_count == 0) {
        VOID_FUNC_CALL(ctx->curr_block, called_addr);
        return COMPILE_OK;
    }

    Ref result = newTemp(ctx->ir_func);
    IRType ret_type = cast(t->result_type);
    FUNC_CALL(ctx->curr_block, result, ret_type, called_addr);
    ValueStackEntry *v = malloc(sizeof(struct ValueStackEntry));
    if (v == NULL) return COMPILE_ERR;
    v->valtype = t->result_type;
    v->r = result;
    value_stack_push(ctx, v);
    return COMPILE_OK;
}

static CompileErr_t unwind_value_stack(CompileCtx *ctx, ValueStackEntry *v) {
    ValueStackEntry *top = value_stack_top(ctx);
    while(top != v && top != NULL) {
        value_stack_pop(ctx, NULL);
        top = value_stack_top(ctx);
    }
    if (top == NULL) return COMPILE_ERR;
    else return COMPILE_OK;
}

static CompileErr_t compile_br(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    uint32_t labelidx;
    ERR_CHECK(readULEB128_u32(&ctx->offset, f->code_end, &labelidx));

    LabelStackEntry *l = label_stack_get(ctx, labelidx);
    if (l == NULL) return COMPILE_ERR;

    if (l->blktype != WASM_BLOCKTYPE_NONE) {
        ValueStackEntry v;
        ERR_CHECK(value_stack_pop(ctx, &v));
        if (v.valtype != (WASMValtype)l->blktype) return COMPILE_ERR;
        Phi_arg *pa = malloc(sizeof(struct Phi_arg));
        if (pa == NULL) return COMPILE_ERR;
        pa->b = ctx->curr_block;
        pa->r = v.r;
        listPush(l->results, pa);
    }
    if (l->b->is_loop_header) {
        listAddNodeTail(l->b->loop_end_blk_list, ctx->curr_block);
    }
    jmp(ctx->ir_func, ctx->curr_block, l->b);
    ctx->skip_flag = SKIP_FLAG_BR;
    ctx->curr_block = NULL;

    return COMPILE_OK;
}

static CompileErr_t compile_br_if(CompileCtx *ctx) {
    WASMFunction *f = ctx->wasm_func;
    uint32_t labelidx;
    ValueStackEntry v;
    ERR_CHECK(value_stack_pop(ctx, &v));

    ERR_CHECK(readULEB128_u32(&ctx->offset, f->code_end, &labelidx));

    LabelStackEntry *l = label_stack_get(ctx, labelidx);
    if (l == NULL) return COMPILE_ERR;

    if (l->blktype != WASM_BLOCKTYPE_NONE) {
        ValueStackEntry *result = value_stack_top(ctx);
        if (result == NULL) return COMPILE_ERR;
        if (result->valtype != (WASMValtype)l->blktype) return COMPILE_ERR;
        Phi_arg *pa = malloc(sizeof(struct Phi_arg));
        if (pa == NULL) return COMPILE_ERR;
        pa->b = ctx->curr_block;
        pa->r = result->r;
        listPush(l->results, pa);
    }
    if (l->b->is_loop_header) {
        listAddNodeTail(l->b->loop_end_blk_list, ctx->curr_block);
    }
    Blk *continue_blk = newBlock(ctx->local_count);
    jnz(ctx->ir_func, ctx->curr_block, v.r, l->b, continue_blk);
    seal_block(ctx, continue_blk);
    ctx->curr_block = continue_blk;

    return COMPILE_OK;
}

static CompileErr_t compile_return(CompileCtx *ctx) {
    WASMFuncType *t = ctx->wasm_func->type;
    if (t->result_count != 0) {
        ValueStackEntry e;
        ERR_CHECK(value_stack_pop(ctx, &e));
        if (e.valtype != t->result_type) return COMPILE_ERR;
        if (ctx->curr_block != NULL) {
            retRef(ctx->ir_func, ctx->curr_block, e.r);
        }
    } else {
        if (ctx->curr_block != NULL) {
            ret(ctx->ir_func, ctx->curr_block);
        }
    }
    ctx->skip_flag = SKIP_FLAG_RETURN;
    ctx->curr_block = NULL;
    return COMPILE_OK;
}

static CompileErr_t finalize_block_body(
    CompileCtx *ctx, ValueStackEntry *bottom) {

    switch (ctx->skip_flag) {
    case SKIP_FLAG_BR:
    case SKIP_FLAG_RETURN: {
        ctx->skip_flag = SKIP_FLAG_NONE;
        ERR_CHECK(unwind_value_stack(ctx, bottom));
    } break;
    case SKIP_FLAG_NONE: {
        LabelStackEntry *l = label_stack_top(ctx);
        if (l == NULL) return COMPILE_ERR;
        if (ctx->curr_block != NULL) {
            jmp(ctx->ir_func, ctx->curr_block, l->b);
        }
        if (l->blktype != WASM_BLOCKTYPE_NONE) {
            ValueStackEntry v;
            ERR_CHECK(value_stack_pop(ctx, &v));
            if (v.valtype != (WASMValtype)l->blktype) return COMPILE_ERR;

            Phi_arg *pa = malloc(sizeof(struct Phi_arg));
            if (pa == NULL) return COMPILE_ERR;
            pa->r = v.r;
            pa->b = ctx->curr_block;
            listPush(l->results, pa);
        }
    } break;
    default:
        return COMPILE_ERR;
    }

    if (value_stack_top(ctx) != bottom) return COMPILE_ERR;
    ERR_CHECK(value_stack_pop(ctx, NULL));
    return COMPILE_OK;
}

static CompileErr_t compile_then(CompileCtx *ctx, uint8_t *last_opcode) {

    WASMFunction *f = ctx->wasm_func;

    ValueStackEntry *bottom;
    INIT_BOTTOM(bottom);
    value_stack_push(ctx, bottom);

    uint8_t opcode;
    ERR_CHECK(read_u8(&ctx->offset, f->code_end, &opcode));
    while (opcode != 0x0B && opcode != WASM_OPCODE_ELSE) {
        ERR_CHECK(compile_instr(ctx, opcode));
        ERR_CHECK(read_u8(&ctx->offset, f->code_end, &opcode));
    }
    *last_opcode = opcode;

    ERR_CHECK(finalize_block_body(ctx, bottom));
    return COMPILE_OK;
}

static CompileErr_t compile_else(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;

    ValueStackEntry *bottom;
    INIT_BOTTOM(bottom);
    value_stack_push(ctx, bottom);

    uint8_t opcode;
    ERR_CHECK(read_u8(&ctx->offset, f->code_end, &opcode));
    while (opcode != 0x0B) {
        ERR_CHECK(compile_instr(ctx, opcode));
        ERR_CHECK(read_u8(&ctx->offset, f->code_end, &opcode));
    }
    ERR_CHECK(finalize_block_body(ctx, bottom));
    return COMPILE_OK;
}

static CompileErr_t finalize_block(CompileCtx *ctx, LabelStackEntry *l) {
    seal_block(ctx, l->b);
    ctx->curr_block = l->b;
    if (l->blktype != WASM_BLOCKTYPE_NONE) {
        uint32_t result_size = listLength(l->results);
        Ref r;
        if (result_size == 1) {
            Phi_arg *pa = listPop(l->results);
            r = pa->r;
            free(pa);
        } else if (result_size > 1) {
            r = newTemp(ctx->ir_func);
            listNode *phi_node;
            phi_node = newPhi(ctx->curr_block, r, cast((WASMValtype)l->blktype));
            for (uint32_t i = 0; i < result_size; i++) {
                Phi_arg *pa = listPop(l->results);
                phiAppendOperand(phi_node, pa->b, pa->r);
                free(pa);
            }
        } else {
            r = INT32_CONST(0);
        }

        ValueStackEntry *result = malloc(sizeof(struct ValueStackEntry));
        if (result == NULL) return COMPILE_ERR;
        result->valtype = (WASMValtype)l->blktype;
        result->r = r;
        value_stack_push(ctx, result);
    }

    ERR_CHECK(label_stack_pop(ctx, NULL));
    return COMPILE_OK;
}

static CompileErr_t compile_if(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    ValueStackEntry v;
    ERR_CHECK(value_stack_pop(ctx, &v));
    if (v.valtype != WASM_VALTYPE_I32) return COMPILE_ERR;

    Blk *then_blk = newBlock(ctx->local_count);
    Blk *else_blk = newBlock(ctx->local_count);
    Blk *end_blk  = newBlock(ctx->local_count);

    WASMBlocktype blktype;
    ERR_CHECK(read_u8(&ctx->offset, f->code_end, &blktype));
    if (!IS_WASM_BLOCK_TYPE(blktype)) return COMPILE_ERR;

    LabelStackEntry *l;
    INIT_LABEL(l, end_blk, blktype)
    label_stack_push(ctx, l);

    assert(ctx->curr_block->is_sealed);
    jnz(ctx->ir_func, ctx->curr_block, v.r, then_blk, else_blk);
    seal_block(ctx, then_blk);
    seal_block(ctx, else_blk);

    ctx->curr_block = then_blk;
    uint8_t last_opcode;
    ERR_CHECK(compile_then(ctx, &last_opcode));
    if (label_stack_top(ctx) != l) return COMPILE_ERR;

    ctx->curr_block = else_blk;
    if (last_opcode == WASM_OPCODE_ELSE) {
        ERR_CHECK(compile_else(ctx));
        if (label_stack_top(ctx) != l) return COMPILE_ERR;
    } else {
        jmp(ctx->ir_func, ctx->curr_block, end_blk);
    }
    ERR_CHECK(finalize_block(ctx, l));
    return COMPILE_OK;
}

static CompileErr_t compile_block(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    Blk *end_blk = newBlock(ctx->local_count);

    WASMBlocktype blktype;
    ERR_CHECK(read_u8(&ctx->offset, f->code_end, &blktype));
    if (!IS_WASM_BLOCK_TYPE(blktype)) return COMPILE_ERR;

    LabelStackEntry *l;
    INIT_LABEL(l, end_blk, blktype)
    label_stack_push(ctx, l);

    ValueStackEntry *bottom;
    INIT_BOTTOM(bottom);
    value_stack_push(ctx, bottom);

    uint8_t opcode;
    ERR_CHECK(read_u8(&ctx->offset, f->code_end, &opcode));
    while (opcode != 0x0B) {
        ERR_CHECK(compile_instr(ctx, opcode));
        ERR_CHECK(read_u8(&ctx->offset, f->code_end, &opcode));
    }

    if (label_stack_top(ctx) != l) return COMPILE_ERR;
    ERR_CHECK(finalize_block_body(ctx, bottom));
    ERR_CHECK(finalize_block(ctx, l));
    return COMPILE_OK;
}

static CompileErr_t compile_loop(CompileCtx *ctx) {

    WASMFunction *f = ctx->wasm_func;
    Blk *loop_header = newBlock(ctx->local_count);
    loop_header->is_loop_header = true;
    loop_header->loop_end_blk_list = listCreate();
    jmp(ctx->ir_func, ctx->curr_block, loop_header);
    ctx->curr_block = loop_header;

    WASMBlocktype blktype;
    ERR_CHECK(read_u8(&ctx->offset, f->code_end, &blktype));
    if (!IS_WASM_BLOCK_TYPE(blktype)) return COMPILE_ERR;

    LabelStackEntry *l;
    INIT_LABEL(l, loop_header, blktype);
    label_stack_push(ctx, l);

    ValueStackEntry *bottom;
    INIT_BOTTOM(bottom);
    value_stack_push(ctx, bottom);

    uint8_t opcode;
    ERR_CHECK(read_u8(&ctx->offset, f->code_end, &opcode));
    while (opcode != 0x0B) {
        ERR_CHECK(compile_instr(ctx, opcode));
        ERR_CHECK(read_u8(&ctx->offset, f->code_end, &opcode));
    }

    ValueStackEntry v;
    switch (ctx->skip_flag) {
    case SKIP_FLAG_BR:
    case SKIP_FLAG_RETURN: {
        ctx->skip_flag = SKIP_FLAG_NONE;
        ERR_CHECK(unwind_value_stack(ctx, bottom));
    } break;
    case SKIP_FLAG_NONE: {
        if (blktype != WASM_BLOCKTYPE_NONE) {
            ERR_CHECK(value_stack_pop(ctx, &v));
            if (v.valtype != (WASMValtype)l->blktype) return COMPILE_ERR;
        }
    } break;
    default:
        return COMPILE_ERR;
    }

    if (value_stack_top(ctx) != bottom) return COMPILE_ERR;
    ERR_CHECK(value_stack_pop(ctx, NULL));

    if (blktype != WASM_BLOCKTYPE_NONE) {
        ValueStackEntry *result = malloc(sizeof(struct ValueStackEntry));
        if (result == NULL) return COMPILE_ERR;
        *result = v;
        value_stack_push(ctx, result);
    }

    if (label_stack_top(ctx) != l) return COMPILE_ERR;
    ERR_CHECK(label_stack_pop(ctx, NULL));
    seal_block(ctx, loop_header);
    if (listLength(loop_header->loop_end_blk_list) == 0) {
        listRelease(loop_header->loop_end_blk_list);
        loop_header->loop_end_blk_list = NULL;
        loop_header->is_loop_header = false;
    }
    return COMPILE_OK;
}

static CompileErr_t compile_select(CompileCtx *ctx) {

    ValueStackEntry v, v1, v2;
    ERR_CHECK(value_stack_pop(ctx, &v));
    if (v.valtype != WASM_VALTYPE_I32) return COMPILE_ERR;
    ERR_CHECK(value_stack_pop(ctx, &v2));
    ERR_CHECK(value_stack_pop(ctx, &v1));
    if (v1.valtype != v2.valtype) return COMPILE_ERR;
    WASMValtype valtype = v1.valtype;

    Blk *select_fst = newBlock(ctx->local_count);
    Blk *select_snd = newBlock(ctx->local_count);
    Blk *end = newBlock(ctx->local_count);

    jnz(ctx->ir_func, ctx->curr_block, v.r, select_fst, select_snd);
    jmp(ctx->ir_func, select_fst, end);
    jmp(ctx->ir_func, select_snd, end);
    ctx->curr_block = end;

    Ref r = newTemp(ctx->ir_func);
    listNode *phi_node = newPhi(end, r, cast(valtype));
    phiAppendOperand(phi_node, select_fst, v1.r);
    phiAppendOperand(phi_node, select_snd, v2.r);

    ValueStackEntry *result = malloc(sizeof(struct ValueStackEntry));
    if (result == NULL) return COMPILE_ERR;
    result->r = r;
    result->valtype = valtype;
    value_stack_push(ctx, result);
    return COMPILE_OK;
}

static CompileErr_t compile_load(CompileCtx *ctx, WASMValtype type, IROpcode ir_opcode) {

    if (ctx->m->memories_count < 1) return COMPILE_ERR;

    WASMFunction *f = ctx->wasm_func;
    //TODO: how to properly use align?
    uint32_t align, offset;
    ERR_CHECK(readULEB128_u32(&ctx->offset, f->code_end, &align));
    ERR_CHECK(readULEB128_u32(&ctx->offset, f->code_end, &offset));

    ValueStackEntry v;
    ERR_CHECK(value_stack_pop(ctx, &v));
    if (v.valtype != WASM_VALTYPE_I32) return COMPILE_ERR;

    Ref load_addr = newTemp(ctx->ir_func);
    ADD(ctx->curr_block, load_addr, IR_TYPE_INT32, ctx->mem0_ptr, v.r);
    //TODO: out of bound memory check
    Ref r = newTemp(ctx->ir_func);
    instr(ctx->curr_block, r, IR_TYPE_INT32, ir_opcode, INT32_CONST(offset), load_addr);

    ValueStackEntry *result = malloc(sizeof(struct ValueStackEntry));
    if (result == NULL) return COMPILE_ERR;
    result->valtype = type;
    result->r = r;
    value_stack_push(ctx, result);
    return COMPILE_OK;
}

static CompileErr_t compile_store(CompileCtx *ctx, WASMValtype type, IROpcode ir_opcode) {

    if (ctx->m->memories_count < 1) return COMPILE_ERR;

    WASMFunction *f = ctx->wasm_func;
    //TODO: how to properly use align?
    uint32_t align, offset;
    ERR_CHECK(readULEB128_u32(&ctx->offset, f->code_end, &align));
    ERR_CHECK(readULEB128_u32(&ctx->offset, f->code_end, &offset));

    ValueStackEntry v, addr;
    ERR_CHECK(value_stack_pop(ctx, &v));
    ERR_CHECK(value_stack_pop(ctx, &addr));
    if (v.valtype != type) return COMPILE_ERR;
    if (addr.valtype != WASM_VALTYPE_I32) return COMPILE_ERR;

    Blk *b = ctx->curr_block;
    Ref store_addr = newTemp(ctx->ir_func);
    ADD(b, store_addr, IR_TYPE_INT32, ctx->mem0_ptr, addr.r);
    instr(b, v.r, IR_TYPE_INT32, ir_opcode, INT32_CONST(offset), store_addr);
    return COMPILE_OK;
}

static CompileErr_t compile_binop(CompileCtx *ctx, WASMValtype type,
                                  IROpcode ir_opcode) {

    ValueStackEntry arg1, arg2;
    ERR_CHECK(value_stack_pop(ctx, &arg2));
    ERR_CHECK(value_stack_pop(ctx, &arg1));

    if (arg1.valtype != type) return COMPILE_ERR;
    if (arg2.valtype != type) return COMPILE_ERR;

    Ref result = newTemp(ctx->ir_func);
    ValueStackEntry *v = malloc(sizeof(struct ValueStackEntry));
    if (v == NULL) return COMPILE_ERR;
    v->valtype = type;
    v->r = result;
    value_stack_push(ctx, v);
    instr(ctx->curr_block, result, cast(type), ir_opcode, arg1.r, arg2.r);
    return COMPILE_OK;
}

static CompileErr_t compile_testop(CompileCtx *ctx, WASMValtype type,
                                  IROpcode ir_opcode) {
    ValueStackEntry v;
    ERR_CHECK(value_stack_pop(ctx, &v));
    if (v.valtype != type) return COMPILE_ERR;
    Ref r = newTemp(ctx->ir_func);
    ValueStackEntry *result = malloc(sizeof(struct ValueStackEntry));
    if (result == NULL) return COMPILE_ERR;
    result->valtype = WASM_VALTYPE_I32;
    result->r = r;
    instr(ctx->curr_block, r, IR_TYPE_INT32, ir_opcode, v.r, UNDEFINED_REF);
    value_stack_push(ctx, result);
    return COMPILE_OK;
}

static CompileErr_t compile_relop(CompileCtx *ctx, WASMValtype type,
                                  IROpcode ir_opcode) {
    ValueStackEntry arg1, arg2;
    ERR_CHECK(value_stack_pop(ctx, &arg2));
    ERR_CHECK(value_stack_pop(ctx, &arg1));

    if (arg1.valtype != type) return COMPILE_ERR;
    if (arg2.valtype != type) return COMPILE_ERR;

    Ref result = newTemp(ctx->ir_func);
    ValueStackEntry *v = malloc(sizeof(struct ValueStackEntry));
    if (v == NULL) return COMPILE_ERR;
    v->valtype = WASM_VALTYPE_I32;
    v->r = result;
    value_stack_push(ctx, v);
    instr(ctx->curr_block, result, cast(type), ir_opcode, arg1.r, arg2.r);
    return COMPILE_OK;
}

static CompileErr_t compile_const(CompileCtx *ctx, WASMValtype type) {
    WASMFunction *f = ctx->wasm_func;
    Ref c;
    switch (type) {
    case WASM_VALTYPE_I32: {
        int32_t n;
        ERR_CHECK(readILEB128_i32(&ctx->offset, f->code_end, &n));
        c = INT32_CONST(n);
    } break;
    case WASM_VALTYPE_F32:
    case WASM_VALTYPE_I64:
    case WASM_VALTYPE_F64:
        /* not implemented */
        assert(0);
    default:
        return COMPILE_ERR;
    }
    ValueStackEntry *v = malloc(sizeof(struct ValueStackEntry));
    if (v == NULL) return COMPILE_ERR;
    v->valtype = type;
    v->r = c;
    value_stack_push(ctx, v);
    return COMPILE_OK;
}

static CompileErr_t compile_instr(CompileCtx *ctx, uint8_t opcode) {
    /* If the branch_flag is SKIP_FLAG_BR(RETURN), we are trying to compile an
     * instruction between a 'br'('return') and the end of the closest block,
     * so skip it.*/
    if (ctx->skip_flag != SKIP_FLAG_NONE) return COMPILE_OK;

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
            return value_stack_pop(ctx, NULL);
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
        return compile_load(ctx, WASM_VALTYPE_I32, IR_OPCODE_LOAD32);
    case WASM_OPCODE_I32_STORE:
        return compile_store(ctx, WASM_VALTYPE_I32, IR_OPCODE_STORE32);
    case WASM_OPCODE_I32_LOAD8_U:
        return compile_load(ctx, WASM_VALTYPE_I32, IR_OPCODE_LOADU8);
    case WASM_OPCODE_I32_STORE8:
        return compile_store(ctx, WASM_VALTYPE_I32, IR_OPCODE_STORE8);

    /* Numeric instruction */
    case WASM_OPCODE_I32_CONST:
        return compile_const(ctx, WASM_VALTYPE_I32);
    case WASM_OPCODE_I32_EQZ:
        return compile_testop(ctx, WASM_VALTYPE_I32, IR_OPCODE_CEQZI32);
    case WASM_OPCODE_I32_EQ:
        return compile_relop(ctx, WASM_VALTYPE_I32, IR_OPCODE_CEQI32);
    case WASM_OPCODE_I32_NE:
        return compile_relop(ctx, WASM_VALTYPE_I32, IR_OPCODE_CNEI32);
    case WASM_OPCODE_I32_LT_S:
        return compile_relop(ctx, WASM_VALTYPE_I32, IR_OPCODE_CSLTI32);
    case WASM_OPCODE_I32_LT_U:
        return compile_relop(ctx, WASM_VALTYPE_I32, IR_OPCODE_CULTI32);
    case WASM_OPCODE_I32_GT_S:
        return compile_relop(ctx, WASM_VALTYPE_I32, IR_OPCODE_CSGTI32);
    case WASM_OPCODE_I32_GT_U:
        return compile_relop(ctx, WASM_VALTYPE_I32, IR_OPCODE_CUGTI32);
    case WASM_OPCODE_I32_LE_S:
        return compile_relop(ctx, WASM_VALTYPE_I32, IR_OPCODE_CSLEI32);
    case WASM_OPCODE_I32_LE_U:
        return compile_relop(ctx, WASM_VALTYPE_I32, IR_OPCODE_CULEI32);
    case WASM_OPCODE_I32_GE_S:
        return compile_relop(ctx, WASM_VALTYPE_I32, IR_OPCODE_CSGEI32);
    case WASM_OPCODE_I32_GE_U:
        return compile_relop(ctx, WASM_VALTYPE_I32, IR_OPCODE_CUGEI32);
    case WASM_OPCODE_I32_ADD:
        return compile_binop(ctx, WASM_VALTYPE_I32, IR_OPCODE_ADD);
    case WASM_OPCODE_I32_SUB:
        return compile_binop(ctx, WASM_VALTYPE_I32, IR_OPCODE_SUB);
    case WASM_OPCODE_I32_MUL:
        return compile_binop(ctx, WASM_VALTYPE_I32, IR_OPCODE_MUL);
    case WASM_OPCODE_I32_DIV_S:
        return compile_binop(ctx, WASM_VALTYPE_I32, IR_OPCODE_DIV);
    case WASM_OPCODE_I32_DIV_U:
        return compile_binop(ctx, WASM_VALTYPE_I32, IR_OPCODE_UDIV);
    case WASM_OPCODE_I32_REM_S:
        return compile_binop(ctx, WASM_VALTYPE_I32, IR_OPCODE_REM);
    case WASM_OPCODE_I32_REM_U:
        return compile_binop(ctx, WASM_VALTYPE_I32, IR_OPCODE_UREM);
    case WASM_OPCODE_I32_AND:
        return compile_binop(ctx, WASM_VALTYPE_I32, IR_OPCODE_AND);
    case WASM_OPCODE_I32_OR:
        return compile_binop(ctx, WASM_VALTYPE_I32, IR_OPCODE_OR);
    case WASM_OPCODE_I32_XOR:
        return compile_binop(ctx, WASM_VALTYPE_I32, IR_OPCODE_XOR);
    case WASM_OPCODE_I32_SHL:
        return compile_binop(ctx, WASM_VALTYPE_I32, IR_OPCODE_SHL);
    case WASM_OPCODE_I32_SHR_S:
        return compile_binop(ctx, WASM_VALTYPE_I32, IR_OPCODE_SAR);
    case WASM_OPCODE_I32_SHR_U:
        return compile_binop(ctx, WASM_VALTYPE_I32, IR_OPCODE_SAR);

    /* unknown opcode */
    default:
        assert(0);
        return COMPILE_ERR;
    }
}

static void cleanup(CompileCtx *ctx) {
    (void)ctx;
    /*
    listRelease(ctx.value_stack);
    listRelease(ctx.label_stack);
    listNode *blk_node;
    listNode *blk_iter = listFirst(func->blk_list);
    while ((blk_node = listNext(&blk_iter)) != NULL) {
        Blk *b = listNodeValue(blk_node);
        free(b->locals);
        b->locals = NULL;
        free(b->incomplete_phis);
        b->incomplete_phis = NULL;
    }
    */
}

static Fn *compile_fn(WASMModule *m, uint32_t funcidx) {

    assert(funcidx < m->function_count);
    WASMFunction *wasm_func = &m->functions[funcidx];
    WASMFuncType *t = wasm_func->type;
    uint32_t local_count = t->param_count + wasm_func->local_count;

    IRType ret_type = IR_TYPE_NONE;
    if (t->result_count != 0) {
        ret_type = cast(t->result_type);
    }
    Blk *start = newBlock(local_count);
    Fn *ir_func = newFunc(ret_type, wasm_func->name, start);

    /* Initialize function parameters, the first parameter (a0)
     * always hold a pointer to the struct WASMExecEnv */
    Ref WASMExecEnv_ptr = newFuncParam(ir_func, IR_TYPE_INT32);
    for (uint32_t i = 0; i < t->param_count; i++) {
        Ref param = newFuncParam(ir_func, cast(wasm_valtype_of(wasm_func, i)));
        start->locals[i] = param;
    }

    /* initialize function locals */
    Ref zero = INT32_CONST(0);
    for (uint32_t i = 0; i < wasm_func->local_count; i++) {
        start->locals[t->param_count + i] = zero;
    }

    Ref WASMModuleInstance_ptr = newTemp(ir_func);
    LOADI32(start, WASMModuleInstance_ptr, INT32_CONST(8), WASMExecEnv_ptr);

    Ref mem0_ptr = newTemp(ir_func);
    LOADI32(start, mem0_ptr, INT32_CONST(376), WASMModuleInstance_ptr);
    Ref globals_start = newTemp(ir_func);
    ADD(start, globals_start, IR_TYPE_INT32, WASMModuleInstance_ptr, INT32_CONST(464));

    CompileCtx ctx = {
        .m = m,
        .funcidx = funcidx,
        .wasm_func = wasm_func,
        .ir_func = ir_func,
        .offset = wasm_func->code_start,
        .local_count = local_count,
        .skip_flag = SKIP_FLAG_NONE,
        .curr_block = start,
        .value_stack_length = 0,
        .a0 = WASMExecEnv_ptr,
        .mem0_ptr = mem0_ptr,
        .globals_start = globals_start,
    };

    INIT_LIST_HEAD(&ctx.value_stack);
    INIT_LIST_HEAD(&ctx.label_stack);

    seal_block(&ctx, start);
    
    /* compile the body of the function */
    uint8_t opcode;
    while (ctx.offset < wasm_func->code_end) {
        if (read_u8(&ctx.offset, wasm_func->code_end, &opcode)) goto ERROR;
        if (compile_instr(&ctx, opcode)) goto ERROR;
    }

    /* Add an implicit return if there is no explicit return in the wasm code */
    if (ctx.skip_flag != SKIP_FLAG_RETURN) {
        if (compile_return(&ctx)) goto ERROR;
    }

    /* Chech that the value stack is empty */
    if (!list_empty(&ctx.value_stack)) goto ERROR;

    /* Check the function body code size */
    if (ctx.offset != wasm_func->code_end) goto ERROR;

    cleanup(&ctx);
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
        register_allocation(fn);
        WASMFunction *wf = &wasm_mod->functions[i];
        ERR_CHECK(T->emitfn(&aot_mod, fn, wf->typeidx));
        freeFunc(fn);
    }
    ERR_CHECK(aot_module_finalize(&aot_mod, out_buf, out_len));
    return COMPILE_OK;
}

static void write_local(Blk *b, uint32_t index, Ref value) {
    Ref *ptr = &b->locals[index];
    Use_ptr u = { .local = ptr };

    if (ptr->type == REF_TYPE_TMP) {
        rmUsage(listNodeValue(ptr->as.tmp_node), ULocal, u);
    }
    b->locals[index] = value;
    if (value.type == REF_TYPE_TMP) {
        addUsage(listNodeValue(value.as.tmp_node), ULocal, u);
    }
}

static Ref read_local(CompileCtx *ctx, Blk *b, uint32_t index) {
    if (b->locals[index].type != REF_TYPE_UNDEFINED) {
        return b->locals[index];
    }
    return read_local_rec(ctx, b, index);
}

static Ref read_local_rec(CompileCtx *ctx, Blk *b, uint32_t index) {
    Ref value;
    if (!b->is_sealed) {
        // Incomplete CFG
        Ref temp = newTemp(ctx->ir_func);
        IRType phi_type = IR_TYPE_INT32;
        if (index > 0) {
            phi_type = cast(wasm_valtype_of(ctx->wasm_func, index));
        }
        listNode *phi_node = newPhi(b, temp, phi_type);
        b->incomplete_phis[index] = phi_node;
        value = temp;
    } else if (listLength(b->preds) == 1) {
        // Optimize the common case of one predecessor: no phi needed
        Blk *p = listNodeValue(listFirst(b->preds));
        value = read_local(ctx, p, index);
    } else {
        // Break potential cycles with operandless phi
        Ref temp = newTemp(ctx->ir_func);
        IRType phi_type = IR_TYPE_INT32;
        if (index > 0) {
            phi_type = cast(wasm_valtype_of(ctx->wasm_func, index));
        }
        listNode *phi_node = newPhi(b, temp, phi_type);
        write_local(b, index, temp);
        value = add_phi_operands(ctx, phi_node, index);
    }
    write_local(b, index, value);
    return value;
}

static Ref add_phi_operands(CompileCtx *ctx, listNode *phi_node, uint32_t index) {
    // Determine phi operands from the predecessors
    Phi *phi = listNodeValue(phi_node);
    Blk *b = phi->block;
    listNode *node;
    listNode *iter = listFirst(b->preds);
    while ((node = listNext(&iter)) != NULL) {
        Blk *pred = listNodeValue(node);
        Ref l = read_local(ctx, pred, index);
        phiAppendOperand(phi_node, pred, l);
    }
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

static Ref try_remove_trivial_phi(CompileCtx *ctx, listNode *phi_node) {
    Phi *phi = listNodeValue(phi_node);
    Ref same = UNDEFINED_REF;
    listNode *phi_arg_node;
    listNode *phi_arg_iter = listFirst(phi->phi_arg_list);
    while ((phi_arg_node = listNext(&phi_arg_iter)) != NULL) {
        Phi_arg *op = listNodeValue(phi_arg_node);
        if (REF_EQ(same, op->r) || REF_EQ(op->r, phi->to)) {
            // Unique value or self-reference
            continue;
        }
        if (same.type != REF_TYPE_UNDEFINED) {
            // The phi merges at least two values: not trivial
            return phi->to;
        }
        same = op->r;
    }

    assert(phi->to.type == REF_TYPE_TMP);
    listNode *to_node = phi->to.as.tmp_node;
    Tmp *to = listNodeValue(to_node);

    phi_replace_by(phi_node, same);
    if (same.type == REF_TYPE_TMP && same.as.tmp_node != NULL) {
        Use_ptr u = { .phi = phi_node };
        rmUsage(listNodeValue(same.as.tmp_node), UPhi, u);
    }
    listDelNode(phi->block->phi_list, phi_node);

    listNode *use_node;
    listNode *use_iter = listFirst(to->use_list);
    while ((use_node = listNext(&use_iter)) != NULL) {
        Use *use = listNodeValue(use_node);
        if (use->type == UPhi && use->u.phi != phi_node) {
            try_remove_trivial_phi(ctx, use->u.phi);
        }
        listDelNode(to->use_list, use_node);
    }

    listDelNode(ctx->ir_func->tmp_list, to_node);
    return same;
}

static void seal_block(CompileCtx *ctx, Blk *b) {
    assert(b->is_sealed == 0);
    for (uint32_t i = 0; i < ctx->local_count; i++) {
        if (b->incomplete_phis[i] == NULL) continue;
        add_phi_operands(ctx, b->incomplete_phis[i], i);
    }
    b->is_sealed = true;
}

IRType cast(WASMValtype t) {
    switch(t) {
        case WASM_VALTYPE_I32:
            return IR_TYPE_INT32;
        case WASM_VALTYPE_I64:
            return IR_TYPE_INT64;
        case WASM_VALTYPE_F32:
            return IR_TYPE_F32;
        case WASM_VALTYPE_F64:
            return IR_TYPE_F64;
        default:
            assert(0);
    }
}

