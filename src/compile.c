#include "all.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if ESP_HEAP_DEBUG != 0
#include "esp_system.h"
#endif

extern void loop_detection(Fn *f);

extern void rv32_emitfn(Fn *fn, FILE *f);
extern void rv32_emitdata(Data *d, FILE *f);

static void compile_instr(func_compile_ctx_t *ctx, unsigned char opcode);

stack_entry_t bottom_block_stack = {
    .kind = STACK_ENTRY_BLOCK_END,
};

static void assert_memory0_exists(func_compile_ctx_t *ctx) {
    /* If min_page_num and max_page_num are both 0
     * the WebAssembly module dosn't use memory */
    if (ctx->m->mem.min_page_num == 0 && ctx->m->mem.max_page_num == 0) {
        panic();
    }
}

static void assert_stack_entry_value( stack_entry_t *entry, wasm_valtype t) {
    if (entry == NULL || entry->kind != STACK_ENTRY_VALUE) {
        panic();
    }
    if (entry->as.value.wasm_type != t) {
        panic();
    }
}

static void assert_stack_entry_block_end(stack_entry_t *entry) {
    if (entry == NULL || entry->kind != STACK_ENTRY_BLOCK_END) {
        panic();
    }
}

static void assert_block_type(wasm_blocktype type) {
    if (type != BLOCK_TYPE_NONE && type != BLOCK_TYPE_I32 &&
        type != BLOCK_TYPE_I64  && type != BLOCK_TYPE_F32 &&
        type != BLOCK_TYPE_F64) {
        panic();
    }
}



static uint32_t size(wasm_valtype wasm_type) {
    switch (wasm_type) {
        case I32_VALTYPE:
            return 4;
        case I64_VALTYPE:
            panic();
            return 8;
        case F32_VALTYPE:
            panic(); //TODO: add support for floating point
            return 4;
        case F64_VALTYPE:
            panic(); //TODO: add support for floating point
            return 8;
        default:
            panic();
            return 0; // dead code, only to remove compiler warning
    }
}


static phi_arg *alloc_phi_arg(Blk *label, Ref result) {
    phi_arg *pa = xmalloc(sizeof(struct phi_arg));
    pa->label = label;
    pa->result.type = result.type;
    pa->result.val = result.val;
    return pa;
}


static label_t *alloc_label(Blk *qbe_block, wasm_blocktype t) {

    label_t *label = xmalloc(sizeof(struct label_t));
    label->qbe_block = qbe_block;
    label->wasm_type = t;
    label->results = listCreate();
    listSetFreeMethod(label->results, free);
    return label;
}

static void free_label(label_t *l) {
    listRelease(l->results);
    free(l);
}


static stack_entry_t *alloc_stack_entry_value(Ref qbe_temp, wasm_valtype t) {
    stack_entry_t *entry = xmalloc(sizeof(struct stack_entry_t));
    entry->kind = STACK_ENTRY_VALUE;
    entry->as.value.qbe_temp = qbe_temp;
    entry->as.value.wasm_type = t;
    return entry;
}

static void compile_instr_i32_binop(func_compile_ctx_t *ctx, unsigned char opcode) {

    stack_entry_t *snd_operand = listPop(ctx->value_stack);
    stack_entry_t *fst_operand = listPop(ctx->value_stack);
    assert_stack_entry_value(snd_operand, I32_VALTYPE);
    assert_stack_entry_value(fst_operand, I32_VALTYPE);
    Blk *b = ctx->curr_block;
    Ref result_temp = newTemp(ctx->qbe_func);
    stack_entry_t *entry = alloc_stack_entry_value(result_temp, I32_VALTYPE);
    listPush(ctx->value_stack, entry);
    Ref fst = fst_operand->as.value.qbe_temp;
    Ref snd = snd_operand->as.value.qbe_temp;
    switch (opcode) {
        case I32_ADD_OPCODE:
            ADD(b, result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_SUB_OPCODE:
            SUB(b, result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_MUL_OPCODE:
            MUL(b, result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_DIV_S_UPCODE:
            DIV(b, result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_DIV_U_OPCODE:
            UDIV(b, result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_REM_S_OPCODE:
            REM(b, result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_REM_U_OPCODE:
            UREM(b, result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_AND_OPCODE:
            AND(b, result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_OR_OPCODE:
            OR(b, result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_XOR_OPCODE:
            XOR(b, result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_SHL_OPCODE:
            SHL(b, result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_SHR_S_OPCODE:
            SAR(b, result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_SHR_U_OPCODE:
            SAR(b, result_temp, WORD_TYPE, fst, snd);
            break;
        default:
            panic();
    }
    free(snd_operand);
    free(fst_operand);
}

static void compile_instr_i32_tests(func_compile_ctx_t *ctx, unsigned char opcode) {

    stack_entry_t *test_cond = listPop(ctx->value_stack);
    assert_stack_entry_value(test_cond, I32_VALTYPE);
    Ref result = newTemp(ctx->qbe_func);
    stack_entry_t *entry = alloc_stack_entry_value(result, I32_VALTYPE);
    listPush(ctx->value_stack, entry);
    switch (opcode) {
        case I32_EQZ_OPCODE: {
            EQZW(ctx->curr_block, result, test_cond->as.value.qbe_temp);
        } break;
        default:
            panic();
    }
    free(test_cond);
}

static void compile_instr_i32_comparisons(func_compile_ctx_t *ctx, unsigned char opcode) {

    stack_entry_t *snd_operand = listPop(ctx->value_stack);
    stack_entry_t *fst_operand = listPop(ctx->value_stack);
    assert_stack_entry_value(snd_operand, I32_VALTYPE);
    assert_stack_entry_value(fst_operand, I32_VALTYPE);
    Blk *b = ctx->curr_block;
    Ref result = newTemp(ctx->qbe_func);
    stack_entry_t *entry = alloc_stack_entry_value(result, I32_VALTYPE);
    listPush(ctx->value_stack, entry);
    value_t *fst = &fst_operand->as.value;
    value_t *snd = &snd_operand->as.value;
    switch (opcode) {
        case I32_EQ_OPCODE: {
            Ref tmp = newTemp(ctx->qbe_func);
            XOR(b, tmp, WORD_TYPE, fst->qbe_temp, snd->qbe_temp);
            EQZW(b, result, tmp);
        } break;
        case I32_NE_OPCODE:
            XOR(b, result, WORD_TYPE, fst->qbe_temp, snd->qbe_temp);
            break;
        case I32_LT_S_OPCODE:
            CSLTW(b, result, fst->qbe_temp, snd->qbe_temp);
            break;
        case I32_LT_U_OPCODE:
            CULTW(b, result, fst->qbe_temp, snd->qbe_temp);
            break;
        case I32_GT_S_OPCODE:
            CSLTW(b, result,  snd->qbe_temp, fst->qbe_temp);
            break;
        case I32_GT_U_OPCODE:
            CULTW(b, result, snd->qbe_temp, fst->qbe_temp);
            break;
        case I32_LE_S_OPCODE: {
            Ref tmp = newTemp(ctx->qbe_func);
            CSLTW(b, tmp, snd->qbe_temp, fst->qbe_temp);
            Ref one = newIntConst(ctx->qbe_func, 1);
            XOR(b, result, WORD_TYPE, tmp, one);
        } break;
        case I32_LE_U_OPCODE: {
            Ref tmp = newTemp(ctx->qbe_func);
            CULTW(b, tmp, snd->qbe_temp, fst->qbe_temp);
            Ref one = newIntConst(ctx->qbe_func, 1);
            XOR(b, result, WORD_TYPE, tmp, one);
        } break;
        case I32_GE_S_OPCODE: {
            Ref tmp = newTemp(ctx->qbe_func);
            CSLTW(b, tmp, fst->qbe_temp, snd->qbe_temp);
            Ref one = newIntConst(ctx->qbe_func, 1);
            XOR(b, result, WORD_TYPE, tmp, one);
        } break;
        case I32_GE_U_OPCODE: {
            Ref tmp = newTemp(ctx->qbe_func);
            CULTW(b, tmp, fst->qbe_temp, snd->qbe_temp);
            Ref one = newIntConst(ctx->qbe_func, 1);
            XOR(b, result, WORD_TYPE, tmp, one);
        } break;
        default:
            panic();
    }
    free(snd_operand);
    free(fst_operand);
}


static void compile_instr_local_get(func_compile_ctx_t *ctx) {

    uint32_t index;
    readULEB128_u32(&ctx->wasm_func_body->expr, &index);
    wasm_valtype wasm_type = local_type(ctx, index);
    if (size(wasm_type) != 4) panic();
    Ref local = read_local(ctx, ctx->curr_block, index);
    if (local.val.tmp_node == NULL) {
        local = newIntConst(ctx->qbe_func, 0);
    }
    stack_entry_t *entry = alloc_stack_entry_value(local, wasm_type);
    listPush(ctx->value_stack, entry);
}

static void compile_instr_local_set(func_compile_ctx_t *ctx) {

    uint32_t index;
    stack_entry_t *entry = listPop(ctx->value_stack);
    readULEB128_u32(&ctx->wasm_func_body->expr, &index);
    wasm_valtype wasm_type = local_type(ctx, index);
    if (size(wasm_type) != 4) panic();
    assert_stack_entry_value(entry, wasm_type);
    write_local(ctx->curr_block, index, entry->as.value.qbe_temp);
    free(entry);
}

/*
static void compile_instr_local_tee(func_compile_ctx_t *ctx) {

    uint32_t index;
    stack_entry_t *entry = da_peak_last(ctx->value_stack);
    readULEB128_u32(&ctx->wasm_func_body->expr, &index);
    wasm_valtype wasm_type = local_type(ctx, index);
    if (size(wasm_type) != 4) panic();
    assert_stack_entry_value(entry, wasm_type);
    Ref *locals = da_peak_last(ctx->locals_stack);
    locals[index] = entry->as.value.qbe_temp;
}
*/

static void compile_instr_global_get(func_compile_ctx_t *ctx) {
    uint32_t globalidx;
    readULEB128_u32(&ctx->wasm_func_body->expr, &globalidx);
    if (globalidx >= ctx->m->globals_len) panic();
    global_t *g = &ctx->m->globals[globalidx];

    if (g->expr.type != I32_VALTYPE) panic();

    if (g->is_mutable) {
        Ref temp = newTemp(ctx->qbe_func);
        stack_entry_t *entry = alloc_stack_entry_value(temp, g->expr.type);
        listPush(ctx->value_stack, entry);
        Ref global = newAddrConst(ctx->qbe_func, (char *) g->name);
        LOADW(ctx->curr_block, temp, global);
    } else {
        Ref c = newIntConst(ctx->qbe_func, g->expr.as.i32);
        stack_entry_t *entry = alloc_stack_entry_value(c, g->expr.type);
        listPush(ctx->value_stack, entry);
    }
}

static void compile_instr_global_set(func_compile_ctx_t *ctx) {
    uint32_t globalidx;
    readULEB128_u32(&ctx->wasm_func_body->expr, &globalidx);
    if (globalidx >= ctx->m->globals_len) panic();
    global_t *g = &ctx->m->globals[globalidx];
    if (!g->is_mutable) panic();
    if (g->expr.type == NO_VALTYPE) panic();
    stack_entry_t *entry = listPop(ctx->value_stack);
    assert_stack_entry_value(entry, g->expr.type);
    if (size(g->expr.type) != 4) panic();
    Ref global = newAddrConst(ctx->qbe_func, (char *) g->name);
    STOREW(ctx->curr_block, entry->as.value.qbe_temp, global);
    free(entry);
}

static void compile_instr_i32_const(func_compile_ctx_t *ctx) {
    int32_t n;
    readILEB128_i32(&ctx->wasm_func_body->expr, &n);
    Ref c = newIntConst(ctx->qbe_func, n);
    stack_entry_t *entry = alloc_stack_entry_value(c, I32_VALTYPE);
    listPush(ctx->value_stack, entry);
}

static void compile_instr_call(func_compile_ctx_t *ctx) {
    uint32_t funcidx;
    readULEB128_u32(&ctx->wasm_func_body->expr, &funcidx);
    if (funcidx >= ctx->m->num_funcs) {
        panic();
    }
    wasm_func_type_t *called_type = ctx->m->func_decls[funcidx].type;
    if (listLength(ctx->value_stack) < called_type->num_params) {
        panic();
    }

    Ref called_addr = newAddrConst(ctx->qbe_func, ctx->m->func_decls[funcidx].name);
    uint32_t num_params = called_type->num_params;
    if (num_params > 0) {
            listNode *node;
            listNode *iter = listIndex(ctx->value_stack, -1 * (int) num_params);
            if (iter == NULL) panic();
            unsigned int i = 0;
            while ((node = listNext(&iter)) != NULL) {
                wasm_valtype param_type = called_type->params_type[i];
                stack_entry_t *entry = listNodeValue(node);
                assert_stack_entry_value(entry, param_type);
                newFuncCallArg(ctx->curr_block, cast(param_type), entry->as.value.qbe_temp);
                listDelNode(ctx->value_stack, node);
                i++;
            }
    }
    if (called_type->return_type != NO_VALTYPE) {
        Ref temp = newTemp(ctx->qbe_func);
        simple_type ret_type = cast(called_type->return_type);
        FUNC_CALL(ctx->curr_block, temp, ret_type, called_addr);
        stack_entry_t *entry = alloc_stack_entry_value(temp, called_type->return_type);
        listPush(ctx->value_stack, entry);
    } else {
        VOID_FUNC_CALL(ctx->curr_block, called_addr);
    }
}

static void unwind_value_stack(list *value_stack) {
    stack_entry_t *entry = NULL;
    do {
        free(entry);
        entry = listPop(value_stack);
        if (entry == NULL) panic();
    } while(entry->kind != STACK_ENTRY_BLOCK_END);
    listPush(value_stack, entry);
}

static void compile_instr_br(func_compile_ctx_t *ctx) {
    uint32_t labelidx;

    readULEB128_u32(&ctx->wasm_func_body->expr, &labelidx);
    long index = -1 * (long) (labelidx + 1);
    listNode *node = listIndex(ctx->label_stack, index);
    if (node == NULL) panic();
    label_t *label = listNodeValue(node);

    stack_entry_t *result = NULL;
    if (label->wasm_type != BLOCK_TYPE_NONE) {
        result = listPop(ctx->value_stack);
        assert_stack_entry_value(result, (wasm_valtype) label->wasm_type);
    }
    unwind_value_stack(ctx->value_stack);
    if (label->wasm_type != BLOCK_TYPE_NONE) {
        Ref r = result->as.value.qbe_temp;
        phi_arg *p = alloc_phi_arg(ctx->curr_block, r);
        listPush(label->results, p);
        free(result);
    }
    Blk *b = label->qbe_block;
    if (b->is_loop_header) {
        listAddNodeTail(b->loop_end_blk_list, ctx->curr_block);
    }
    jmp(ctx->qbe_func, ctx->curr_block, label->qbe_block);
    ctx->skip_flag = BR_FLAG;
    ctx->curr_block = NULL;
}

static void compile_instr_br_if(func_compile_ctx_t *ctx) {
    uint32_t labelidx;

    stack_entry_t *ifcond = listPop(ctx->value_stack);
    assert_stack_entry_value(ifcond, I32_VALTYPE);
    Ref ifcond_qbe_temp = ifcond->as.value.qbe_temp;
    free(ifcond);

    readULEB128_u32(&ctx->wasm_func_body->expr, &labelidx);
    long index = -1 * ((long) labelidx + 1);
    listNode *node = listIndex(ctx->label_stack, index);
    if (node == NULL) panic();
    label_t *label = listNodeValue(node);

    if (label->wasm_type != BLOCK_TYPE_NONE) {
        listNode *node = listLast(ctx->value_stack);
        stack_entry_t *result = listNodeValue(node);
        assert_stack_entry_value(result, (wasm_valtype) label->wasm_type);
        Ref r = result->as.value.qbe_temp;
        phi_arg *p = alloc_phi_arg(ctx->curr_block, r);
        listPush(label->results, p);
    }

    Blk *b = label->qbe_block;
    if (b->is_loop_header) {
        listAddNodeTail(b->loop_end_blk_list, ctx->curr_block);
    }
    Blk *continue_blk = newBlock(ctx->locals_len);
    jnz(ctx->qbe_func, ctx->curr_block, ifcond_qbe_temp, label->qbe_block, continue_blk);
    seal_block(ctx, continue_blk);
    ctx->curr_block = continue_blk;
}

static void compile_instr_return(func_compile_ctx_t *ctx) {
    wasm_valtype return_type = ctx->wasm_func_decl->type->return_type;
    if (return_type != NO_VALTYPE) {
        stack_entry_t *result = listPop(ctx->value_stack);
        assert_stack_entry_value(result, (wasm_valtype) return_type);
        retRef(ctx->qbe_func, ctx->curr_block, result->as.value.qbe_temp);
        free(result);
    } else {
        ret(ctx->qbe_func, ctx->curr_block);
    }
    unwind_value_stack(ctx->value_stack);
    ctx->skip_flag = RETURN_FLAG;
    ctx->curr_block = NULL;
}

static boolean compile_then_branch(func_compile_ctx_t *ctx) {
   
    read_struct_t *r = &ctx->wasm_func_body->expr;
    listPush(ctx->value_stack, &bottom_block_stack);

    unsigned char opcode;
    read_u8(r, &opcode);
    while (opcode != END_CODE && opcode != ELSE_OPCODE) {
        compile_instr(ctx, opcode);
        read_u8(r, &opcode);
    }
    skip_flag skip_flag = ctx->skip_flag;
    if (skip_flag == BR_FLAG || skip_flag == RETURN_FLAG) {
        ctx->skip_flag = NONE;
        unwind_value_stack(ctx->value_stack);
        stack_entry_t *bottom = listPop(ctx->value_stack);
        assert_stack_entry_block_end(bottom);
    } else if (skip_flag == NONE) {
        listNode *node = listLast(ctx->label_stack);
        label_t *label = listNodeValue(node);
        jmp(ctx->qbe_func, ctx->curr_block, label->qbe_block);
        if (label->wasm_type != BLOCK_TYPE_NONE) {
            stack_entry_t *then_result = listPop(ctx->value_stack);
            assert_stack_entry_value(then_result, (wasm_valtype) label->wasm_type);
            Ref r = then_result->as.value.qbe_temp;
            phi_arg *p = alloc_phi_arg(ctx->curr_block, r);
            listPush(label->results, p);
            free(then_result);
        }
        stack_entry_t *bottom = listPop(ctx->value_stack);
        assert_stack_entry_block_end(bottom);
    } else panic();
    return opcode == ELSE_OPCODE;
}

static boolean compile_else_branch(func_compile_ctx_t *ctx) {

    read_struct_t *r = &ctx->wasm_func_body->expr;
    listPush(ctx->value_stack, &bottom_block_stack);

    unsigned char opcode;
    read_u8(r, &opcode);
    while (opcode != END_CODE) {
        compile_instr(ctx, opcode);
        read_u8(r, &opcode);
    }

    skip_flag skip_flag = ctx->skip_flag;
    if (skip_flag == BR_FLAG || skip_flag == RETURN_FLAG) {
        ctx->skip_flag = NONE;
        unwind_value_stack(ctx->value_stack);
        stack_entry_t *bottom = listPop(ctx->value_stack);
        assert_stack_entry_block_end(bottom);
    } else if (skip_flag == NONE) {
        listNode *node = listLast(ctx->label_stack);
        label_t *label = listNodeValue(node);
        if (label->wasm_type != BLOCK_TYPE_NONE) {
            stack_entry_t *otherwise_result = listPop(ctx->value_stack);
            assert_stack_entry_value(otherwise_result, (wasm_valtype) label->wasm_type);
            Ref r = otherwise_result->as.value.qbe_temp;
            phi_arg *p = alloc_phi_arg(ctx->curr_block, r);
            listPush(label->results, p);
            free(otherwise_result);
        }
        stack_entry_t *bottom = listPop(ctx->value_stack);
        assert_stack_entry_block_end(bottom);
    } else panic();
    return skip_flag != NONE;
}

static void compile_instr_if(func_compile_ctx_t *ctx) {

    read_struct_t *r = &ctx->wasm_func_body->expr;

    stack_entry_t *ifcond = listPop(ctx->value_stack);
    assert_stack_entry_value(ifcond, I32_VALTYPE);
    Ref ifcond_qbe_temp = ifcond->as.value.qbe_temp;
    free(ifcond);

    Blk *then = newBlock(ctx->locals_len);
    Blk *otherwise = newBlock(ctx->locals_len);
    Blk *end = newBlock(ctx->locals_len);

    wasm_blocktype wasm_type;
    read_u8(r, &wasm_type);
    assert_block_type(wasm_type);

    label_t *label = alloc_label(end, wasm_type);
    listPush(ctx->label_stack, label);

    assert(ctx->curr_block->is_sealed);
    jnz(ctx->qbe_func, ctx->curr_block, ifcond_qbe_temp, then, otherwise);
    seal_block(ctx, then);
    seal_block(ctx, otherwise);

    ctx->curr_block = then;
    boolean matching_else = compile_then_branch(ctx);

    ctx->curr_block = otherwise;
    boolean br = FALSE;
    if (matching_else) {
        br = compile_else_branch(ctx);
    }
    if (!br) {
        jmp(ctx->qbe_func, ctx->curr_block, end);
    }
    assert(label == listPop(ctx->label_stack));

    seal_block(ctx, end);
    ctx->curr_block = end;
    if (wasm_type != BLOCK_TYPE_NONE) {
        uint32_t result_size = listLength(label->results);
        Ref r;
        if (result_size == 1) {
            phi_arg *p = listPop(label->results);
            r = p->result;
            free(p);
        } else if (result_size > 1) {
            r = newTemp(ctx->qbe_func);
            listNode *phi_node = newPhi(end, r, cast((wasm_valtype) wasm_type));
            for (uint32_t i = 0; i < result_size; i++) {
                phi_arg *p = listPop(label->results);
                phiAppendOperand(phi_node, p->label, p->result);
                free(p);
            }
        } else {
            r = newIntConst(ctx->qbe_func, 0);
        }
        stack_entry_t *result = alloc_stack_entry_value(
            r, (wasm_valtype) wasm_type);
        listPush(ctx->value_stack, result);
    }
    free_label(label);
}

static void compile_instr_block(func_compile_ctx_t *ctx) {

    wasm_blocktype wasm_type;
    read_struct_t *r = &ctx->wasm_func_body->expr;
    read_u8(r, &wasm_type);
    assert_block_type(wasm_type);

    Blk *end = newBlock(ctx->locals_len);
    listPush(ctx->value_stack, &bottom_block_stack);

    label_t *label = alloc_label(end, wasm_type);
    listPush(ctx->label_stack, label);

    unsigned char opcode;
    read_u8(r, &opcode);
    while (opcode != END_CODE) {
        compile_instr(ctx, opcode);
        read_u8(r, &opcode);
    }
    skip_flag skip_flag = ctx->skip_flag;
    if (skip_flag == BR_FLAG || skip_flag == RETURN_FLAG) {
        ctx->skip_flag = NONE;
        stack_entry_t *bottom = listPop(ctx->value_stack);
        assert_stack_entry_block_end(bottom);
    } else if (skip_flag == NONE) {
        if (wasm_type != BLOCK_TYPE_NONE) {
             stack_entry_t *block_result = listPop(ctx->value_stack);
            assert_stack_entry_value(block_result, (wasm_valtype) wasm_type);
            Ref r = block_result->as.value.qbe_temp;
            phi_arg *p = alloc_phi_arg(ctx->curr_block, r);
            listPush(label->results, p);
            free(block_result);
        }
        stack_entry_t *bottom = listPop(ctx->value_stack);
        assert_stack_entry_block_end(bottom);
        if (ctx->curr_block != NULL) {
            jmp(ctx->qbe_func, ctx->curr_block, end);
        }
    }
    else panic();

    assert(label == listPop(ctx->label_stack));
    if (wasm_type != BLOCK_TYPE_NONE) {
        uint32_t result_size = listLength(label->results);
        Ref r;
        if (result_size == 1) {
            phi_arg *p = listPop(label->results);
            r = p->result;
            free(p);
        } else if (result_size > 1) {
            r = newTemp(ctx->qbe_func);
            listNode *phi_node = newPhi(end, r, cast((wasm_valtype) wasm_type));
            for (uint32_t i = 0; i < result_size; i++) {
                phi_arg *p = listPop(label->results);
                phiAppendOperand(phi_node, p->label, p->result);
                free(p);
            }
        } else {
            r = newIntConst(ctx->qbe_func, 0);
        }
        stack_entry_t *result = alloc_stack_entry_value(
            r, (wasm_valtype) wasm_type);
        listPush(ctx->value_stack, result);
    }

    free_label(label);
    seal_block(ctx, end);
    ctx->curr_block = end;
}

static void compile_instr_loop(func_compile_ctx_t *ctx) {

    wasm_blocktype wasm_type;
    read_struct_t *r = &ctx->wasm_func_body->expr;
    read_u8(r, &wasm_type);
    assert_block_type(wasm_type);
    listPush(ctx->value_stack, &bottom_block_stack);

    Blk *loop_header = newBlock(ctx->locals_len);
    loop_header->is_loop_header = TRUE;
    loop_header->loop_end_blk_list = listCreate();

    jmp(ctx->qbe_func, ctx->curr_block, loop_header);
    ctx->curr_block = loop_header;

    label_t *label = alloc_label(loop_header, wasm_type);
    listPush(ctx->label_stack, label);

    unsigned char opcode;
    read_u8(r, &opcode);
    while (opcode != END_CODE) {
        compile_instr(ctx, opcode);
        read_u8(r, &opcode);
    }

    skip_flag skip_flag = ctx->skip_flag;
    if (skip_flag == BR_FLAG || skip_flag == RETURN_FLAG) {
        ctx->skip_flag = NONE;
        stack_entry_t *bottom = listPop(ctx->value_stack);
        assert_stack_entry_block_end(bottom);
    } else if (skip_flag == NONE) {
        stack_entry_t *loop_result = NULL;
        if (wasm_type != BLOCK_TYPE_NONE) {
            loop_result = listPop(ctx->value_stack);
            assert_stack_entry_value(loop_result, (wasm_valtype) wasm_type);
        }
        stack_entry_t *bottom = listPop(ctx->value_stack);
        assert_stack_entry_block_end(bottom);
        if (wasm_type != BLOCK_TYPE_NONE) {
            listPush(ctx->value_stack, loop_result);
        }
    }
    else panic();

    assert(label == listPop(ctx->label_stack));
    free_label(label);
    seal_block(ctx, loop_header);

    if (listLength(loop_header->loop_end_blk_list) == 0) {
        listRelease(loop_header->loop_end_blk_list);
        loop_header->loop_end_blk_list = NULL;
        loop_header->is_loop_header = FALSE;
    }
}

static void compile_control_instr(func_compile_ctx_t *ctx, unsigned char opcode) {

    switch (opcode) {
        case UNREACHABLE_OPCODE:
            halt(ctx->qbe_func, ctx->curr_block);
            break;
        case NOP_OPCODE:
            break;
        case BLOCK_OPCODE:
            compile_instr_block(ctx);
            break;
        case LOOP_OPCODE:
            compile_instr_loop(ctx);
            break;
        case IF_OPCODE:
            compile_instr_if(ctx);
            break;
        case BRANCH_OPCODE:
            compile_instr_br(ctx);
            break;
        case BRANCH_IF_OPCODE:
            compile_instr_br_if(ctx);
            break;
        case RETURN_OPCODE:
            compile_instr_return(ctx);
            break;
        case CALL_OPCODE:
            compile_instr_call(ctx);
            break;
        default:
            printf("PANIC: opcode = 0x%02X\n", opcode);
            panic();
    }
}

static void compile_parametric_instr(func_compile_ctx_t *ctx, unsigned char opcode) {

    switch (opcode) {
        case DROP_OPCODE: {
            stack_entry_t *entry = listPop(ctx->value_stack);
            if (entry == NULL || entry->kind != STACK_ENTRY_VALUE) {
                panic();
            }
            free(entry);
        } break;
        case SELECT_OPCODE: {
            stack_entry_t *select_cond = listPop(ctx->value_stack);
            assert_stack_entry_value(select_cond, I32_VALTYPE);
            stack_entry_t *snd_operand = listPop(ctx->value_stack);
            stack_entry_t *fst_operand = listPop(ctx->value_stack);
            if (snd_operand == NULL || snd_operand->kind != STACK_ENTRY_VALUE ||
                fst_operand == NULL || fst_operand->kind != STACK_ENTRY_VALUE) {
                panic();
            }
            if (snd_operand->as.value.wasm_type != fst_operand->as.value.wasm_type) {
                panic();
            }
            wasm_valtype wasm_type = snd_operand->as.value.wasm_type;
            Blk *select_fst = newBlock(ctx->locals_len);
            Blk *select_snd = newBlock(ctx->locals_len);
            Blk *end = newBlock(ctx->locals_len);

            jnz(ctx->qbe_func, ctx->curr_block,
                select_cond->as.value.qbe_temp, select_fst, select_snd);
            jmp(ctx->qbe_func, select_fst, end);
            jmp(ctx->qbe_func, select_snd, end);
            ctx->curr_block = end;

            Ref result = newTemp(ctx->qbe_func);
            listNode *phi_node = newPhi(end, result, cast(wasm_type));
            Ref fst_arg = fst_operand->as.value.qbe_temp;
            Ref snd_arg = snd_operand->as.value.qbe_temp;
            phiAppendOperand(phi_node, select_fst, fst_arg);
            phiAppendOperand(phi_node, select_snd, snd_arg);

            stack_entry_t *entry = alloc_stack_entry_value(result, wasm_type);
            listPush(ctx->value_stack, entry);

            free(select_cond);
            free(snd_operand);
            free(fst_operand);
        } break;
        default:
            printf("PANIC: opcode = 0x%02X\n", opcode);
            panic();

    }
}

static void compile_variable_instr(func_compile_ctx_t *ctx, unsigned char opcode) {

    switch (opcode) {
        case LOCAL_GET_OPCODE:
            compile_instr_local_get(ctx);
            break;
        case LOCAL_SET_OPCODE:
            compile_instr_local_set(ctx);
            break;
        case LOCAL_TEE_OPCODE:
            panic();
            //compile_instr_local_tee(ctx);
            break;
        case GLOBAL_GET_OPCODE:
            compile_instr_global_get(ctx);
            break;
        case GLOBAL_SET_OPCODE:
            compile_instr_global_set(ctx);
            break;
        default: 
            printf("PANIC: opcode = 0x%02X\n", opcode);
            panic();
    }
}

static Ref addr_32bit(func_compile_ctx_t *ctx,
    Ref mem0, Ref address, uint32_t offset) {

    Fn *f = ctx->qbe_func;
    Blk *b = ctx->curr_block;

    Ref addr = newTemp(f);
    if (offset != 0) {
        Ref t1 = newTemp(f);
        Ref c = newIntConst(ctx->qbe_func, offset);
        ADD(b, t1, WORD_TYPE, address, c);
        ADD(b, addr, WORD_TYPE, mem0, t1);
    } else {
        ADD(b, addr, WORD_TYPE, mem0, address);
    }
    return addr;
}

static Ref addr_64bit(func_compile_ctx_t *ctx,
    Ref mem0, Ref address, uint32_t offset) {

    Fn *f = ctx->qbe_func;
    Blk *b = ctx->curr_block;

    Ref t0;
    if (address.type == RCon) {
        t0 = address;
    } else if (address.type == RTmp) {
        t0 = newTemp(f);
        EXTSW(b, t0, address);
    } else panic();

    Ref addr = newTemp(f);
    if (offset != 0) {
        Ref t1 = newTemp(f);
        Ref c = newIntConst(ctx->qbe_func, offset);
        ADD(b, t1, LONG_TYPE, t0, c);
        ADD(b, addr, LONG_TYPE, mem0, t1);
    } else {
        ADD(b, addr, LONG_TYPE, mem0, t0);
    }
    return addr;
}

static void compile_i32_load_instr_generic(
    func_compile_ctx_t *ctx, int load_instr) {

    assert_memory0_exists(ctx);
    //TODO: how to properly use align?
    uint32_t align, offset;
    readULEB128_u32(&ctx->wasm_func_body->expr, &align);
    readULEB128_u32(&ctx->wasm_func_body->expr, &offset);
    Blk *b = ctx->curr_block;
    Fn *f = ctx->qbe_func;

    stack_entry_t *address = listPop(ctx->value_stack);
    assert_stack_entry_value(address, I32_VALTYPE);
    Ref mem0 = newAddrConst(ctx->qbe_func, "mem0");

    Ref load_addr = addr_32bit(ctx, mem0, address->as.value.qbe_temp, offset);
    //TODO: out of bound memory check
    Ref result = newTemp(f);
    instr(b, result, WORD_TYPE, load_instr, load_addr, UNDEF_TMP_REF);
    stack_entry_t *entry = alloc_stack_entry_value(result, I32_VALTYPE);
    listPush(ctx->value_stack, entry);
    free(address);
}

static void compile_i32_store_instr_generic(
    func_compile_ctx_t *ctx, int store_instr) {
    assert_memory0_exists(ctx);
    //TODO: how to properly use align?
    uint32_t align, offset;
    readULEB128_u32(&ctx->wasm_func_body->expr, &align);
    readULEB128_u32(&ctx->wasm_func_body->expr, &offset);
    Blk *b = ctx->curr_block;

    stack_entry_t *value = listPop(ctx->value_stack);
    assert_stack_entry_value(value, I32_VALTYPE);
    stack_entry_t *address = listPop(ctx->value_stack);
    assert_stack_entry_value(address, I32_VALTYPE);
    Ref mem0 = newAddrConst(ctx->qbe_func, "mem0");
    Ref store_addr = addr_32bit(ctx, mem0, address->as.value.qbe_temp, offset);
    //TODO: out of bound memory check
    instr(b, UNDEF_TMP_REF, WORD_TYPE, store_instr, value->as.value.qbe_temp, store_addr);

    free(address);
    free(value);
}

static void compile_memory_instr(func_compile_ctx_t *ctx, unsigned char opcode) {

    switch (opcode) {
        case I32_LOAD_OPCODE:
            compile_i32_load_instr_generic(ctx, LOADUW_INSTR);
            break;
        case I32_STORE_OPCODE:
            compile_i32_store_instr_generic(ctx, STOREW_INSTR);
            break;
        case I32_LOAD8_U_OPCODE:
            compile_i32_load_instr_generic(ctx, LOADUB_INSTR);
            break;
        case I32_STORE8_OPCODE:
            compile_i32_store_instr_generic(ctx, STOREB_INSTR);
            break;
        default:
            printf("PANIC: opcode = 0x%02X\n", opcode);
            panic();
    }
}

static void compile_numeric_instr(func_compile_ctx_t *ctx, unsigned char opcode) {

    switch (opcode) {
        case I32_CONST_OPCODE:
            compile_instr_i32_const(ctx);
            break;
        case I32_EQZ_OPCODE:
            compile_instr_i32_tests(ctx, opcode);
            break;
        case I32_EQ_OPCODE:
        case I32_NE_OPCODE:
        case I32_LT_S_OPCODE:
        case I32_LT_U_OPCODE:
        case I32_GT_S_OPCODE:
        case I32_GT_U_OPCODE:
        case I32_LE_S_OPCODE:
        case I32_LE_U_OPCODE:
        case I32_GE_S_OPCODE:
        case I32_GE_U_OPCODE:
            compile_instr_i32_comparisons(ctx, opcode);
            break;
        case I32_ADD_OPCODE:
        case I32_SUB_OPCODE:
        case I32_MUL_OPCODE:
        case I32_DIV_S_UPCODE:
        case I32_DIV_U_OPCODE:
        case I32_REM_S_OPCODE:
        case I32_REM_U_OPCODE:
        case I32_AND_OPCODE:
        case I32_OR_OPCODE:
        case I32_XOR_OPCODE:
        case I32_SHL_OPCODE:
        case I32_SHR_S_OPCODE:
        case I32_SHR_U_OPCODE:
            compile_instr_i32_binop(ctx, opcode);
            break;
                default:
            printf("PANIC: opcode = 0x%02X\n", opcode);
            panic();
    }
}

static void compile_instr(func_compile_ctx_t *ctx, unsigned char opcode) {
    /* if the branch_flag is true, we are trying to compile an instruction
     * between a br and the end of the closest block, so skip it */
    if (ctx->skip_flag != NONE) return;
    else if (opcode <= 0x11) {
        compile_control_instr(ctx, opcode);
    }
    else if (0x1A <= opcode && opcode <= 0x1B) {
        compile_parametric_instr(ctx, opcode);
    }
    else if (0x20 <= opcode && opcode <= 0x24) {
        compile_variable_instr(ctx, opcode);
    }
    else if (0x28 <= opcode && opcode <= 0x40) {
        compile_memory_instr(ctx, opcode);
    }
    else if (0x41 <= opcode && opcode <= 0xBF) {
        compile_numeric_instr(ctx, opcode);
    }
    else {
        printf("PANIC: opcode = 0x%02X\n", opcode);
        panic();
    }
}

static void compile_func(wasm_module *m, wasm_func_decl *decl, wasm_func_body *body) {
    wasm_func_type_t *t = decl->type;

    uint32_t locals_len = t->num_params + body->num_locals;

    Lnk link_info = {0};
    link_info.is_exported = decl->is_exported;
    simple_type ret_type = NO_TYPE;
    if (t->return_type != NO_VALTYPE) {
        ret_type = cast(t->return_type);
    }
    Blk *start = newBlock(locals_len);
    Fn *qbe_func = newFunc(&link_info, ret_type, decl->name, start);
    Ref zero = newIntConst(qbe_func, 0);

    for (uint32_t i = 0; i < t->num_params; i++) {
        Ref param = newFuncParam(qbe_func, cast(t->params_type[i]));
        start->locals[i] = param;
    }

    for (uint32_t i = 0; i < body->num_locals; i++) {
        start->locals[t->num_params + i] = zero;
    }
    func_compile_ctx_t ctx = {
        .m = m,
        .wasm_func_decl = decl,
        .wasm_func_body = body,
        .qbe_func = qbe_func,
        .value_stack = listCreate(),
        .label_stack = listCreate(),
        .locals_len = locals_len,
        .skip_flag = NONE,
        .curr_block = start,
    };
    listSetFreeMethod(ctx.value_stack, free);
    listSetFreeMethod(ctx.value_stack, free);
    seal_block(&ctx, start);
    listPush(ctx.value_stack, &bottom_block_stack);
    /* compile the body of the function */
    unsigned char opcode;
    while (body->expr.offset != body->expr.end) {
        read_u8(&body->expr, &opcode);
        compile_instr(&ctx, opcode);
    }
    /* Add an implicit return if there is no explicit return in the wasm code */
    if (ctx.skip_flag != RETURN_FLAG) {
        if (t->return_type != NO_VALTYPE) {
            stack_entry_t *entry = listPop(ctx.value_stack);
            assert_stack_entry_value(entry, t->return_type);
            if (ctx.curr_block != NULL) {
                retRef(ctx.qbe_func, ctx.curr_block, entry->as.value.qbe_temp);
            }
            free(entry);
        } else {
            if (ctx.curr_block != NULL) {
                ret(ctx.qbe_func, ctx.curr_block);
            }
        }
    }
    stack_entry_t *bottom = listPop(ctx.value_stack);
    assert_stack_entry_block_end(bottom);

    listRelease(ctx.value_stack);
    listRelease(ctx.label_stack);

    rv32_reg_pool gpr;
    memset(gpr.pool, FALSE, RV32_NUM_REG);
    gpr.size = RV32_GP_NUM_REG;
    for (unsigned int i = 0; i < RV32_GP_NUM_REG; i++) {
        gpr.pool[rv32_gp_reg[i]] = 1;
    }
    rv32_reg_pool argr;
    memset(argr.pool, FALSE, RV32_NUM_REG);
    argr.size = RV32_ARG_NUM_REG;
    for (unsigned int i = 0; i < RV32_ARG_NUM_REG; i++) {
        argr.pool[rv32_arg_reg[i]] = 1;
    }
    //printfn(qbe_func, stdout);
    linear_scan(qbe_func, &gpr, &argr);
    rv32_emitfn(qbe_func, stdout);
    /*
    listNode *tmp_node;
    listNode *tmp_iter = listFirst(qbe_func->tmp_list);
    while ((tmp_node = listNext(&tmp_iter)) != NULL) {
        Tmp *t = listNodeValue(tmp_node);
        printf("%s: i->start = %d, i->end = %d ",
               t->name, t->i->start, t->i->end);
        switch(t->i->assign.type) {
            case ARG_REGISTER:
                printf("ARG REGISTER %d\n", t->i->assign.as.reg);
                break;
            case GP_REGISTER:
                printf("GP REGISTER %d\n", t->i->assign.as.reg);
                break;
            case STACK_SLOT:
                printf("STACK SLOT %d\n", t->i->assign.as.stack_slot);
                break;
            default:
                panic();
        }
    }
    */
    freeFunc(qbe_func);
}

static void compile_globals(wasm_module *m) {
    for (uint32_t i = 0; i < m->globals_len; i++) {
        global_t *g = &m->globals[i];
        if (!g->is_mutable) continue;
        Lnk lnk = {
            .align = 8,
        };
        Data *d = newData(&lnk, (char *)g->name);
        switch (g->expr.type) {
            case I32_VALTYPE:
                dataAppendWordField(d, g->expr.as.i32);
                break;
            default:
                panic();
        }
        //printdata(d, stdout);
        rv32_emitdata(d, stdout);
        freeData(d);
    }
}

static int compare_data_segment(const void *lhs, const void *rhs) {
    data_segment_t *lhs_ds = (data_segment_t *) lhs;
    data_segment_t *rhs_ds = (data_segment_t *) rhs;
    return lhs_ds->offset - rhs_ds->offset;
}

static void compile_data_segments(wasm_module *m) {
    if (m->mem.min_page_num == 0) return;
    qsort(m->data_segments, m->num_data_segments, 
          sizeof(data_segment_t), compare_data_segment);
    unsigned int mem_size = m->mem.min_page_num * 64 * 1024;
    Lnk lnk = {
        .align = 8,
    };
    Data *d = newData(&lnk, "mem0");
    unsigned int mem_offset = 0;
    for (uint32_t i = 0; i < m->num_data_segments; i++) {
        data_segment_t *dseg = &m->data_segments[i];
        if (dseg->offset < mem_offset) panic();
        dataAppendZerosField(d, dseg->offset - mem_offset);
        uint32_t str_len = 4*dseg->init_len+3;
        char *str = xcalloc(str_len, sizeof(char));
        str[0] = '"';
        for (uint32_t j = 0; j < dseg->init_len; j++) {
            char *offset = str+1+4*j;
            snprintf(offset, 5, "\\x%02x", dseg->init_bytes[j]);
        }
        str[str_len-2] = '"';
        str[str_len-1] = '\0';
        dataAppendStringField(d, str, str_len);
        free(str);
        mem_offset = dseg->offset + dseg->init_len;
    }
    dataAppendZerosField(d, mem_size - mem_offset);
    rv32_emitdata(d, stdout);
    //printdata(d, stdout);
    freeData(d);
}

void compile(wasm_module *m) {
    compile_data_segments(m);
    /* m->data_segments is not used anymore. */
    if (m->data_segments != NULL) {
        free(m->data_segments);
        m->data_segments = NULL;
        m->num_data_segments = 0;
    }
    compile_globals(m);
    for (unsigned int i = 0; i < m->num_funcs; i++) {
        #if ESP_HEAP_DEBUG != 0
        printf("free heap: before compile_func %ld\n", esp_get_free_heap_size());
        #endif 
        wasm_func_decl *decl = &m->func_decls[i];
        wasm_func_body *body = parse_next_func_body(m);
        compile_func(m, decl, body);
        free(body);
        #if ESP_HEAP_DEBUG != 0
        printf("free heap after compile_func: %ld\n", esp_get_free_heap_size());
        #endif 
    }
}
