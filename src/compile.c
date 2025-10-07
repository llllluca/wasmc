#include "all.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dalist.h"

#if ESP_HEAP_DEBUG != 0
#include "esp_system.h"
#endif

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

simple_type cast(wasm_valtype t) {
    switch(t) {
        case I32_VALTYPE:
            return WORD_TYPE;
        case I64_VALTYPE:
            return LONG_TYPE;
        case F32_VALTYPE:
            panic(); //TODO: add support for floating point
            return 's';
        case F64_VALTYPE:
            panic(); //TODO: add support for floating point
            return 'd';
        default:
            panic();
            return '_'; // dead code, only to remove compiler warning
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

static label_t *alloc_label(Blk *qbe_block, wasm_blocktype t, Ref qbe_result_temp) {

    label_t *label = xmalloc(sizeof(label_t));
    label->qbe_block = qbe_block;
    label->wasm_type = t;
    label->qbe_result_temp = qbe_result_temp;
    return label;
}


static stack_entry_t *alloc_stack_entry_value(Ref qbe_temp, wasm_valtype t) {
    stack_entry_t *entry = xmalloc(sizeof(stack_entry_t));
    entry->kind = STACK_ENTRY_VALUE;
    entry->as.value.qbe_temp = qbe_temp;
    entry->as.value.wasm_type = t;
    return entry;
}

static void compile_instr_i32_binop(func_compile_ctx_t *ctx, unsigned char opcode) {

    stack_entry_t *snd_operand = da_pop(ctx->value_stack);
    stack_entry_t *fst_operand = da_pop(ctx->value_stack);
    assert_stack_entry_value(snd_operand, I32_VALTYPE);
    assert_stack_entry_value(fst_operand, I32_VALTYPE);

    Ref result_temp = newTemp(ctx->qbe_func);
    stack_entry_t *entry = alloc_stack_entry_value(result_temp, I32_VALTYPE);
    da_push(ctx->value_stack, entry);
    Ref fst = fst_operand->as.value.qbe_temp;
    Ref snd = snd_operand->as.value.qbe_temp;
    switch (opcode) {
        case I32_ADD_OPCODE:
            ADD(result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_SUB_OPCODE:
            SUB(result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_MUL_OPCODE:
            MUL(result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_DIV_S_UPCODE:
            DIV(result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_DIV_U_OPCODE:
            UDIV(result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_REM_S_OPCODE:
            REM(result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_REM_U_OPCODE:
            UREM(result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_AND_OPCODE:
            AND(result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_OR_OPCODE:
            OR(result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_XOR_OPCODE:
            XOR(result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_SHL_OPCODE:
            SHL(result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_SHR_S_OPCODE:
            SAR(result_temp, WORD_TYPE, fst, snd);
            break;
        case I32_SHR_U_OPCODE:
            SAR(result_temp, WORD_TYPE, fst, snd);
            break;
        default:
            panic();
    }
    free(snd_operand);
    free(fst_operand);
}

static void compile_instr_i32_tests(func_compile_ctx_t *ctx, unsigned char opcode) {

    stack_entry_t *test_cond = da_pop(ctx->value_stack);
    assert_stack_entry_value(test_cond, I32_VALTYPE);
    Ref result = newTemp(ctx->qbe_func);
    stack_entry_t *entry = alloc_stack_entry_value(result, I32_VALTYPE);
    da_push(ctx->value_stack, entry);
    switch (opcode) {
        case I32_EQZ_OPCODE: {
            Ref zero = newIntConst(ctx->qbe_func, 0);
            CEQW(result, test_cond->as.value.qbe_temp, zero);
        } break;
        default:
            panic();
    }
    free(test_cond);
}

static void compile_instr_i32_comparisons(func_compile_ctx_t *ctx, unsigned char opcode) {

    stack_entry_t *snd_operand = da_pop(ctx->value_stack);
    stack_entry_t *fst_operand = da_pop(ctx->value_stack);
    assert_stack_entry_value(snd_operand, I32_VALTYPE);
    assert_stack_entry_value(fst_operand, I32_VALTYPE);
    Ref result = newTemp(ctx->qbe_func);
    stack_entry_t *entry = alloc_stack_entry_value(result, I32_VALTYPE);
    da_push(ctx->value_stack, entry);
    value_t *fst = &fst_operand->as.value;
    value_t *snd = &snd_operand->as.value;
    switch (opcode) {
        case I32_EQ_OPCODE:
            CEQW(result, fst->qbe_temp, snd->qbe_temp);
            break;
        case I32_NE_OPCODE:
            CNEW(result, fst->qbe_temp, snd->qbe_temp);
            break;
        case I32_LT_S_OPCODE:
            CSLTW(result, fst->qbe_temp, snd->qbe_temp);
            break;
        case I32_LT_U_OPCODE:
            CULTW(result, fst->qbe_temp, snd->qbe_temp);
            break;
        case I32_GT_S_OPCODE:
            CSGTW(result, fst->qbe_temp, snd->qbe_temp);
            break;
        case I32_GT_U_OPCODE:
            CUGTW(result, fst->qbe_temp, snd->qbe_temp);
            break;
        case I32_LE_S_OPCODE:
            CSLEW(result, fst->qbe_temp, snd->qbe_temp);
            break;
        case I32_LE_U_OPCODE:
            CULEW(result, fst->qbe_temp, snd->qbe_temp);
            break;
        case I32_GE_S_OPCODE:
            CSGEW(result, fst->qbe_temp, snd->qbe_temp);
            break;
        case I32_GE_U_OPCODE:
            CUGEW(result, fst->qbe_temp, snd->qbe_temp);
            break;
        default:
            panic();
    }
    free(snd_operand);
    free(fst_operand);
}

static void compile_instr_local_get(func_compile_ctx_t *ctx) {

    wasm_func_decl *decl = ctx->wasm_func_decl;
    wasm_func_body *body = ctx->wasm_func_body;
    wasm_valtype wasm_type = NO_VALTYPE;
    uint32_t index;
    Ref local;

    readULEB128_u32(&body->expr, &index);
    if (index < decl->type->num_params) {
        wasm_type = decl->type->params_type[index];
        local = ctx->qbe_params[index];
    } else if (index - decl->type->num_params < body->num_locals) {
        index -= decl->type->num_params;
        wasm_type = body->locals_type[index];
        local = ctx->qbe_locals[index];
    } else panic();

    if (size(wasm_type) != 4) panic();
    Ref temp = newTemp(ctx->qbe_func);
    stack_entry_t *entry = alloc_stack_entry_value(temp, wasm_type);
    da_push(ctx->value_stack, entry);
    LOADW(temp, local);
}

static void compile_instr_local_set(func_compile_ctx_t *ctx) {

    wasm_func_decl *decl = ctx->wasm_func_decl;
    wasm_func_body *body = ctx->wasm_func_body;
    wasm_valtype wasm_type = NO_VALTYPE;
    uint32_t index;
    Ref local;

    readULEB128_u32(&body->expr, &index);
    if (index < decl->type->num_params) {
        wasm_type = decl->type->params_type[index];
        local = ctx->qbe_params[index];
    } else if (index - decl->type->num_params < body->num_locals) {
        index -= decl->type->num_params;
        wasm_type = body->locals_type[index];
        local = ctx->qbe_locals[index];
    } else panic();

    stack_entry_t *entry = da_pop(ctx->value_stack);
    assert_stack_entry_value(entry, wasm_type);

    if (size(wasm_type) != 4) panic();
    STOREW(entry->as.value.qbe_temp, local);
    free(entry);
}

static void compile_instr_local_tee(func_compile_ctx_t *ctx) {

    wasm_func_decl *decl = ctx->wasm_func_decl;
    wasm_func_body *body = ctx->wasm_func_body;
    wasm_valtype wasm_type = NO_VALTYPE;
    uint32_t index;
    Ref local;

    readULEB128_u32(&body->expr, &index);
    if (index < decl->type->num_params) {
        wasm_type = decl->type->params_type[index];
        local = ctx->qbe_params[index];
    } else if (index - decl->type->num_params < body->num_locals) {
        index -= decl->type->num_params;
        wasm_type = body->locals_type[index];
        local = ctx->qbe_locals[index];
    } else panic();

    stack_entry_t *entry = da_peak_last(ctx->value_stack);
    assert_stack_entry_value(entry, wasm_type);

    if (size(wasm_type) != 4) panic();
    STOREW(entry->as.value.qbe_temp, local);
}

static void compile_instr_global_get(func_compile_ctx_t *ctx) {
    uint32_t globalidx;
    readULEB128_u32(&ctx->wasm_func_body->expr, &globalidx);
    Ref temp = newTemp(ctx->qbe_func);
    if (globalidx >= ctx->m->globals_len) panic();
    global_t *g = &ctx->m->globals[globalidx];
    if (g->expr.type == NO_VALTYPE) panic();
    stack_entry_t *entry = alloc_stack_entry_value(temp, g->expr.type);
    da_push(ctx->value_stack, entry);
    if (size(g->expr.type) != 4) panic();
    Ref global = newAddrConst(ctx->qbe_func, (char *) g->name);
    LOADW(temp, global);
}

static void compile_instr_global_set(func_compile_ctx_t *ctx) {
    uint32_t globalidx;
    readULEB128_u32(&ctx->wasm_func_body->expr, &globalidx);
    if (globalidx >= ctx->m->globals_len) panic();
    global_t *g = &ctx->m->globals[globalidx];
    if (!g->is_mutable) panic();
    if (g->expr.type == NO_VALTYPE) panic();
    stack_entry_t *entry = da_pop(ctx->value_stack);
    assert_stack_entry_value(entry, g->expr.type);
    if (size(g->expr.type) != 4) panic();
    Ref global = newAddrConst(ctx->qbe_func, (char *) g->name);
    STOREW(entry->as.value.qbe_temp, global);
    free(entry);
}

static void compile_instr_i32_const(func_compile_ctx_t *ctx) {
    int32_t n;
    readILEB128_i32(&ctx->wasm_func_body->expr, &n);
    Ref c = newIntConst(ctx->qbe_func, n);
    stack_entry_t *entry = alloc_stack_entry_value(c, I32_VALTYPE);
    da_push(ctx->value_stack, entry);
}

static void compile_instr_call(func_compile_ctx_t *ctx) {
    uint32_t funcidx;
    readULEB128_u32(&ctx->wasm_func_body->expr, &funcidx);
    if (funcidx >= ctx->m->num_funcs) {
        panic();
    }
    wasm_func_type_t *called_type = ctx->m->func_decls[funcidx].type;
    if (ctx->value_stack->size < called_type->num_params) {
        panic();
    }

    Ref temp = newTemp(ctx->qbe_func);
    Ref called_addr = newAddrConst(ctx->qbe_func, ctx->m->func_decls[funcidx].name);

    uint32_t num_params = called_type->num_params;
    uint32_t stack_size = ctx->value_stack->size;
    if (num_params > 0) {
            if (stack_size < num_params) panic();
            size_t paramsidx =  stack_size - num_params;
            for (uint32_t i = 0; i < num_params; i++) {
                wasm_valtype param_type = called_type->params_type[i];
                stack_entry_t *entry = ctx->value_stack->items[paramsidx + i];
                assert_stack_entry_value(entry, param_type);
                newFuncCallArg(cast(param_type), entry->as.value.qbe_temp);
            }
            for (uint32_t i = 0; i < num_params; i++) {
                stack_entry_t *entry = da_pop(ctx->value_stack);
                free(entry);
            }
    }
    if (called_type->return_type != NO_VALTYPE) {
        simple_type ret_type = cast(called_type->return_type);
        FUNC_CALL(temp, ret_type, called_addr);
        stack_entry_t *entry = alloc_stack_entry_value(temp, called_type->return_type);
        da_push(ctx->value_stack, entry);
    } else {
        VOID_FUNC_CALL(called_addr);
    }
}

static void unwind_value_stack(dalist_t *value_stack) {
    stack_entry_t *entry = NULL;
    do {
        free(entry);
        entry = da_pop(value_stack);
        if (entry == NULL) panic();
    } while(entry->kind != STACK_ENTRY_BLOCK_END);
    da_push(value_stack, entry);
}

static void compile_instr_br(func_compile_ctx_t *ctx) {
    uint32_t labelidx;

    readULEB128_u32(&ctx->wasm_func_body->expr, &labelidx);
    unsigned int len = ctx->label_stack->size;
    if (len <= labelidx) panic();
    label_t *label = ctx->label_stack->items[len - 1 - labelidx];

    if (label->wasm_type != BLOCK_TYPE_NONE) {
        stack_entry_t *result = da_pop(ctx->value_stack);
        assert_stack_entry_value(result, (wasm_valtype) label->wasm_type);
        if (size(result->as.value.wasm_type) != 4) panic();
        COPY(label->qbe_result_temp, WORD_TYPE, result->as.value.qbe_temp);
        free(result);
    }
    unwind_value_stack(ctx->value_stack);
    jmp(ctx->qbe_func, ctx->curr_block, label->qbe_block);
    Blk *continue_blk = newBlock();
    ctx->curr_block = continue_blk;
    ctx->br_or_return_flag = BRANCH_FLAG;
}

static void compile_instr_br_if(func_compile_ctx_t *ctx) {
    uint32_t labelidx;

    // Pop a value of type i32 from the value stack,
    // that value is the condition of the if.
    stack_entry_t *ifcond = da_pop(ctx->value_stack);
    assert_stack_entry_value(ifcond, I32_VALTYPE);
    Ref ifcond_qbe_temp = ifcond->as.value.qbe_temp;
    free(ifcond);

    readULEB128_u32(&ctx->wasm_func_body->expr, &labelidx);
    unsigned int len = ctx->label_stack->size;
    if (len <= labelidx) panic();
    label_t *label = ctx->label_stack->items[len - 1 - labelidx];

    if (label->wasm_type != BLOCK_TYPE_NONE) {
        stack_entry_t *result = da_peak_last(ctx->value_stack);
        assert_stack_entry_value(result, (wasm_valtype) label->wasm_type);
        if (size(result->as.value.wasm_type) != 4) panic();
        COPY(label->qbe_result_temp, WORD_TYPE, result->as.value.qbe_temp);
    }

    Blk *continue_blk = newBlock();
    jnz(ctx->qbe_func, ctx->curr_block, ifcond_qbe_temp, label->qbe_block, continue_blk);
    ctx->curr_block = continue_blk;
}

static void compile_instr_return(func_compile_ctx_t *ctx) {
    dalist_t *value_stack = ctx->value_stack;
    stack_entry_t *result;
    wasm_valtype return_type = ctx->wasm_func_decl->type->return_type;
    if (return_type != NO_VALTYPE) {
        result = da_pop(value_stack);
        assert_stack_entry_value(result, (wasm_valtype) return_type);
        retRef(ctx->curr_block, result->as.value.qbe_temp);
        free(result);
    } else {
        ret(ctx->curr_block);
    }
    unwind_value_stack(value_stack);
    ctx->br_or_return_flag = RETURN_FLAG;
}

static void common_blocks_logic(func_compile_ctx_t *ctx, label_t l,
    unsigned char *last_read_opcode, br_or_return_flag *last_br_or_return_flag) {

    dalist_t *value_stack = ctx->value_stack;
    dalist_t *label_stack = ctx->label_stack;
    read_struct_t *r = &ctx->wasm_func_body->expr;

    Ref zero = newIntConst(ctx->qbe_func, 0);
    COPY(l.qbe_result_temp, WORD_TYPE, zero);

    // add a special stack entry to keep track of the status of the
    // value stack before the start of the compilatoin of this block
    da_push(value_stack, &bottom_block_stack);

    label_t *label = alloc_label(
        l.qbe_block,
        l.wasm_type,
        l.qbe_result_temp);
    da_push(label_stack, label);

    unsigned char opcode;
    read_u8(r, &opcode);
    while (opcode != END_CODE && opcode != ELSE_OPCODE) {
        compile_instr(ctx, opcode);
        read_u8(r, &opcode);
    }
    if (last_read_opcode != NULL) {
        *last_read_opcode = opcode;
    }
    br_or_return_flag branch = ctx->br_or_return_flag;
    if (last_br_or_return_flag != NULL) {
        *last_br_or_return_flag = branch;
    }
    // reset branch_flag to false, there is no more need to skip
    // instructions between a br and the end of the closest block
    ctx->br_or_return_flag = NONE;

    if (l.wasm_type != BLOCK_TYPE_NONE && !branch) {
        stack_entry_t *block_result = da_pop(value_stack);
        assert_stack_entry_value(block_result, (wasm_valtype) l.wasm_type);
        COPY(l.qbe_result_temp,
             cast((wasm_valtype) l.wasm_type),
             block_result->as.value.qbe_temp);
        free(block_result);
    }
    // check that the value stack did not change
    stack_entry_t *bottom = da_pop(value_stack);
    assert_stack_entry_block_end(bottom);

    free(da_pop(ctx->label_stack));
}

static void return_from_blocks(func_compile_ctx_t *ctx, label_t l) {
    if (l.wasm_type != BLOCK_TYPE_NONE) {
        stack_entry_t *result = alloc_stack_entry_value(
            l.qbe_result_temp,
            (wasm_valtype) l.wasm_type);
        da_push(ctx->value_stack, result);
    }
}

static void compile_instr_if(func_compile_ctx_t *ctx) {
    label_t l;

    // Pop a value of type i32 from the value stack,
    // that value is the condition of the if.
    stack_entry_t *ifcond = da_pop(ctx->value_stack);
    assert_stack_entry_value(ifcond, I32_VALTYPE);
    Ref ifcond_qbe_temp = ifcond->as.value.qbe_temp;
    free(ifcond);

    read_u8(&ctx->wasm_func_body->expr, &l.wasm_type);
    assert_block_type(l.wasm_type);
    Blk *then = newBlock();
    Blk *otherwise = newBlock();
    Blk *end = newBlock();
    l.qbe_block = end;
    l.qbe_result_temp = newTemp(ctx->qbe_func);

    // if the condition is not zero jump to then label
    // otherwise jump to otherwise (else) label
    jnz(ctx->qbe_func, ctx->curr_block, ifcond_qbe_temp, then, otherwise);

    // start the compilation of the then branch
    ctx->curr_block = then;
    unsigned char last_read_opcode = 0;
    br_or_return_flag last_br_or_return_flag = NONE;
    common_blocks_logic(ctx, l, &last_read_opcode, &last_br_or_return_flag);
    if (last_br_or_return_flag == NONE) {
        jmp(ctx->qbe_func, ctx->curr_block, end);
    }
    // if there is a matching else, start the compilation of the else branch
    ctx->curr_block = otherwise;
    last_br_or_return_flag = NONE;
    if (last_read_opcode == ELSE_OPCODE) {
        common_blocks_logic(ctx, l, &last_read_opcode, &last_br_or_return_flag);
    }
    if (last_br_or_return_flag == NONE) {
        jmp(ctx->qbe_func, ctx->curr_block, end);
    }
    
    if (last_read_opcode != END_CODE) panic();
    return_from_blocks(ctx, l);
    ctx->curr_block = end;
}

static void compile_instr_block(func_compile_ctx_t *ctx) {
    label_t l;

    read_u8(&ctx->wasm_func_body->expr, &l.wasm_type);
    assert_block_type(l.wasm_type);

    Blk *end = newBlock();
    l.qbe_block = end;
    l.qbe_result_temp = newTemp(ctx->qbe_func);
    // start the compilation of the body of the block
    unsigned char last_read_opcode;
    br_or_return_flag last_br_or_return_flag;
    common_blocks_logic(ctx, l, &last_read_opcode, &last_br_or_return_flag);
    if (last_read_opcode != END_CODE) panic();
    if (last_br_or_return_flag == NONE) {
        jmp(ctx->qbe_func, ctx->curr_block, end);
    }
    return_from_blocks(ctx, l);
    ctx->curr_block = end;
}

static void compile_instr_loop(func_compile_ctx_t *ctx) {
    label_t l;

    Blk *loop = newBlock();
    jmp(ctx->qbe_func, ctx->curr_block, loop);
    ctx->curr_block = loop;

    read_u8(&ctx->wasm_func_body->expr, &l.wasm_type);
    assert_block_type(l.wasm_type);
    l.qbe_block = loop;
    l.qbe_result_temp = newTemp(ctx->qbe_func);

    // start the compilation of the body of the loop
    unsigned char last_read_opcode;
    br_or_return_flag last_br_or_return_flag = NONE;
    common_blocks_logic(ctx, l, &last_read_opcode, &last_br_or_return_flag);
    if (last_read_opcode != END_CODE) panic();
    return_from_blocks(ctx, l);
}

static void compile_control_instr(func_compile_ctx_t *ctx, unsigned char opcode) {

    switch (opcode) {
        case UNREACHABLE_OPCODE:
            halt(ctx->curr_block);
            panic();
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
            stack_entry_t *entry = da_pop(ctx->value_stack);
            if (entry == NULL || entry->kind != STACK_ENTRY_VALUE) {
                panic();
            }
            free(entry);
        } break;
        case SELECT_OPCODE: {
            stack_entry_t *select_cond = da_pop(ctx->value_stack);
            assert_stack_entry_value(select_cond, I32_VALTYPE);
            stack_entry_t *snd_operand = da_pop(ctx->value_stack);
            stack_entry_t *fst_operand = da_pop(ctx->value_stack);
            if (snd_operand == NULL || snd_operand->kind != STACK_ENTRY_VALUE ||
                fst_operand == NULL || fst_operand->kind != STACK_ENTRY_VALUE) {
                panic();
            }
            if (snd_operand->as.value.wasm_type != fst_operand->as.value.wasm_type) {
                panic();
            }
            wasm_valtype wasm_type = snd_operand->as.value.wasm_type;
            Blk *select_fst = newBlock();
            Blk *select_snd = newBlock();
            Blk *end = newBlock();

            jnz(ctx->qbe_func, ctx->curr_block,
                select_cond->as.value.qbe_temp, select_fst, select_snd);
            jmp(ctx->qbe_func, select_fst, end);
            jmp(ctx->qbe_func, select_snd, end);
            ctx->curr_block = end;

            Ref result = newTemp(ctx->qbe_func);
            phi(result, cast(wasm_type), select_fst, fst_operand->as.value.qbe_temp, select_snd, snd_operand->as.value.qbe_temp);

            stack_entry_t *entry =
                alloc_stack_entry_value(result, wasm_type);
            da_push(ctx->value_stack, entry);

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
            compile_instr_local_tee(ctx);
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

static void compile_i32_load_instr(func_compile_ctx_t *ctx) {
    assert_memory0_exists(ctx);
    //TODO: how to properly use align?
    uint32_t align, offset;
    readULEB128_u32(&ctx->wasm_func_body->expr, &align);
    readULEB128_u32(&ctx->wasm_func_body->expr, &offset);

    Ref result = newTemp(ctx->qbe_func);
    Ref address_plus_offset = newTemp(ctx->qbe_func);

    stack_entry_t *address = da_pop(ctx->value_stack);
    assert_stack_entry_value(address, I32_VALTYPE);

    EXTSW(address_plus_offset, address->as.value.qbe_temp);
    Ref c = newIntConst(ctx->qbe_func, offset);
    ADD(address_plus_offset, LONG_TYPE, address_plus_offset, c);
    Ref mem0 = newAddrConst(ctx->qbe_func, "mem0");
    ADD(address_plus_offset, LONG_TYPE, mem0, address_plus_offset);
    //TODO: out of bound memory check
    LOADW(result, address_plus_offset);

    stack_entry_t *entry = alloc_stack_entry_value(result, I32_VALTYPE);
    da_push(ctx->value_stack, entry);
    free(address);
}

static void compile_i32_store_instr(func_compile_ctx_t *ctx) {
    assert_memory0_exists(ctx);
    //TODO: how to properly use align?
    uint32_t align, offset;
    readULEB128_u32(&ctx->wasm_func_body->expr, &align);
    readULEB128_u32(&ctx->wasm_func_body->expr, &offset);

    stack_entry_t *value = da_pop(ctx->value_stack);
    assert_stack_entry_value(value, I32_VALTYPE);
    stack_entry_t *address = da_pop(ctx->value_stack);
    assert_stack_entry_value(address, I32_VALTYPE);

    Ref address_plus_offset = newTemp(ctx->qbe_func);
    EXTSW(address_plus_offset, address->as.value.qbe_temp);
    Ref c = newIntConst(ctx->qbe_func, offset);
    ADD(address_plus_offset, LONG_TYPE, address_plus_offset, c);
    Ref mem0 = newAddrConst(ctx->qbe_func, "mem0");
    ADD(address_plus_offset, LONG_TYPE, mem0, address_plus_offset);
    //TODO: out of bound memory check
    STOREW(value->as.value.qbe_temp, address_plus_offset);
    free(address);
    free(value);
}

static void compile_memory_instr(func_compile_ctx_t *ctx, unsigned char opcode) {

    switch (opcode) {
        case I32_LOAD_OPCODE:
            compile_i32_load_instr(ctx);
            break;
        case I32_STORE_OPCODE:
            compile_i32_store_instr(ctx);
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
    if (ctx->br_or_return_flag != NONE) return;
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

    Ref *qbe_params = NULL;
    Ref *qbe_locals = NULL;
    if (t->num_params > 0) {
        qbe_params = xcalloc(t->num_params, sizeof(Ref));
    }
    if (body->num_locals > 0) {
        qbe_locals = xcalloc(body->num_locals, sizeof(Ref));
    }

    Lnk link_info = {0};
    link_info.export = decl->is_exported;
    func_return_type ret_type = FUNC_NO_RETURN_TYPE;
    if (t->return_type != NO_VALTYPE) {
        ret_type = (func_return_type) cast(t->return_type);
    }
    Fn *qbe_func = newFunc(&link_info, ret_type, decl->name);
    Blk *start = newBlock();
    Ref four = newIntConst(qbe_func, 4);
    Ref zero = newIntConst(qbe_func, 0);

    for (uint32_t i = 0; i < t->num_params; i++) {
        Ref param = newFuncParam(qbe_func, cast(t->params_type[i]));
        qbe_params[i] = param;
    }

    for (uint32_t i = 0; i < t->num_params; i++) {
        Ref temp = newTemp(qbe_func);
        /* Only 32 bit wasm value type are supported */
        if (size(t->params_type[i]) != 4) panic();
        ALLOC4(temp, LONG_TYPE, four);
        STOREW(qbe_params[i], temp);
        qbe_params[i] = temp;
    }

    /* Store and initialize function local variables on the stack */
    for (uint32_t i = 0; i < body->num_locals; i++) {
        Ref temp = newTemp(qbe_func);
        /* Only 32 bit wasm value type are supported */
        if (size(body->locals_type[i]) != 4) panic();
        ALLOC4(temp, LONG_TYPE, four);
        STOREW(zero, temp);
        qbe_locals[i] = temp;
    }

    dalist_t value_stack = {0};
    dalist_t label_stack = {0};
    func_compile_ctx_t ctx = {
        .m = m,
        .wasm_func_decl = decl,
        .wasm_func_body = body,
        .qbe_func = qbe_func,
        .value_stack = &value_stack,
        .label_stack = &label_stack,
        .qbe_params = qbe_params,
        .qbe_locals = qbe_locals,
        .label_count = 0,
        .br_or_return_flag = NONE,
        .curr_block = start,
    };
    da_push(ctx.value_stack, &bottom_block_stack);
    /* compile the body of the function */
    unsigned char opcode;
    while (body->expr.offset != body->expr.end) {
        read_u8(&body->expr, &opcode);
        compile_instr(&ctx, opcode);
    }
    /* Add an implicit return if there is no explicit return in the wasm code */
    if (ctx.br_or_return_flag != RETURN_FLAG) {
        if (t->return_type != NO_VALTYPE) {
            stack_entry_t *entry = da_pop(&value_stack);
            assert_stack_entry_value(entry, t->return_type);
            retRef(ctx.curr_block, entry->as.value.qbe_temp);
            free(entry);
        } else {
            ret(ctx.curr_block);
        }
    }
    stack_entry_t *bottom = da_pop(ctx.value_stack);
    assert_stack_entry_block_end(bottom);

    if (qbe_params != NULL) free(qbe_params);
    if (qbe_locals != NULL) free(qbe_locals);
    da_free(&value_stack);
    da_free(&label_stack);

    typecheck(qbe_func);
    //printfn(qbe_func, stdout);
    optimizeFunc(qbe_func);
}

static void compile_globals(wasm_module *m) {
    for (uint32_t i = 0; i < m->globals_len; i++) {
        global_t *g = &m->globals[i];
        Lnk lnk = {
            .align = 8,
        };
        newData((char *)g->name, &lnk);
        switch (g->expr.type) {
            case I32_VALTYPE:
                ADD_INT32_DATA_FIELD(g->expr.as.i32);
                break;
            default:
                panic();
        }
        closeData();
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
    ;
    newData("mem0", &lnk);
    unsigned int mem_offset = 0;
    for (uint32_t i = 0; i < m->num_data_segments; i++) {
        data_segment_t *d = &m->data_segments[i];
        if (d->offset < mem_offset) panic();
        ADD_ZEROS_DATA_FIELD(d->offset - mem_offset);

        uint32_t str_len = 4*d->init_len+3;
        char *str = xcalloc(str_len, sizeof(char));
        str[0] = '"';
        for (uint32_t j = 0; j < d->init_len; j++) {
            char *offset = str+1+4*j;
            snprintf(offset, 5, "\\x%02x", d->init_bytes[j]);
        }
        str[str_len-2] = '"';
        str[str_len-1] = '\0';

        ADD_STR_DATA_FIELD(str);
        free(str);
        mem_offset = d->offset + d->init_len;
    }
    ADD_ZEROS_DATA_FIELD(mem_size - mem_offset);
    closeData();
}

extern Target T_amd64_sysv;
void compile(wasm_module *m) {
    T = T_amd64_sysv;
    compile_data_segments(m);
    /* m->data_segments is not used anymore. */
    free(m->data_segments);
    m->data_segments = NULL;
    m->num_data_segments = 0;
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
    T.emitfin(stdout);
}
