#include "all.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dalist.h"

static void compile_instr(FILE *f, func_compile_ctx_t *ctx, unsigned char opcode);

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

static void assert_stack_entry_value(
    stack_entry_t *entry, unsigned int wasm_type) {

    if (entry == NULL || entry->kind != STACK_ENTRY_VALUE) {
        panic();
    }
    if (entry->as.value.wasm_type != wasm_type) {
        panic();
    }
}

static void assert_stack_entry_block_end(stack_entry_t *entry) {
    if (entry == NULL || entry->kind != STACK_ENTRY_BLOCK_END) {
        panic();
    }
}

static void assert_block_type(unsigned char type) {
    if (type != BLOCK_TYPE_NONE && type != BLOCK_TYPE_I32 &&
        type != BLOCK_TYPE_I64  && type != BLOCK_TYPE_F32 &&
        type != BLOCK_TYPE_F64) {
        panic();
    }
}

char to_qbe_simple_types(enum wasm_valtype wasm_type) {
    switch (wasm_type) {
        case I32_VALTYPE:
            return 'w';
        case I64_VALTYPE:
            return 'l';
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
static unsigned int type_size_in_bytes(enum wasm_valtype wasm_type) {
    switch (wasm_type) {
        case I32_VALTYPE:
            return 4;
        case I64_VALTYPE:
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

static unsigned int fresh_var() {
    static unsigned int count = 0;
    return count++;
}

label_t *alloc_label(unsigned int qbe_labelidx, 
    unsigned int wasm_type, unsigned int qbe_result_varidx) {

    label_t *label = malloc_or_panic(sizeof(label_t));
    label->qbe_labelidx = qbe_labelidx;
    label->wasm_type = wasm_type;
    label->qbe_result_varidx = qbe_result_varidx;
    return label;
}


static stack_entry_t *alloc_stack_entry_value(
    unsigned int qbe_varidx, unsigned int wasm_type) {

    stack_entry_t *entry = malloc_or_panic(sizeof(stack_entry_t));
    entry->kind = STACK_ENTRY_VALUE;
    entry->as.value.qbe_varidx = qbe_varidx;
    entry->as.value.wasm_type = wasm_type;
    return entry;
}

static void compile_instr_i32_binop(
    FILE *f, func_compile_ctx_t *ctx, unsigned char opcode) {

    stack_entry_t *snd_operand = da_pop(ctx->value_stack);
    stack_entry_t *fst_operand = da_pop(ctx->value_stack);
    assert_stack_entry_value(snd_operand, I32_VALTYPE);
    assert_stack_entry_value(fst_operand, I32_VALTYPE);
    unsigned int result_varidx = fresh_var();
    stack_entry_t *entry = alloc_stack_entry_value(result_varidx, I32_VALTYPE);
    da_push(ctx->value_stack, entry);
    fprintf(f, "\t%%t%d =w ", result_varidx);
    switch (opcode) {
        case I32_ADD_OPCODE:
            fprintf(f, "add");
            break;
        case I32_SUB_OPCODE:
            fprintf(f, "sub");
            break;
        case I32_MUL_OPCODE:
            fprintf(f, "mul");
            break;
        case I32_DIV_S_UPCODE:
            fprintf(f, "div");
            break;
        case I32_DIV_U_OPCODE:
            fprintf(f, "udiv");
            break;
        case I32_REM_S_OPCODE:
            fprintf(f, "rem");
            break;
        case I32_REM_U_OPCODE:
            fprintf(f, "urem");
            break;
        case I32_AND_OPCODE:
            fprintf(f, "and");
            break;
        case I32_OR_OPCODE:
            fprintf(f, "or");
            break;
        case I32_XOR_OPCODE:
            fprintf(f, "xor");
            break;
        case I32_SHL_OPCODE:
            fprintf(f, "shl");
            break;
        case I32_SHR_S_OPCODE:
            fprintf(f, "sar");
            break;
        case I32_SHR_U_OPCODE:
            fprintf(f, "shr");
            break;
        default:
            panic();
    }
    fprintf(f, " %%t%d, %%t%d\n",
            fst_operand->as.value.qbe_varidx,
            snd_operand->as.value.qbe_varidx);
    free(snd_operand);
    free(fst_operand);
}

static void compile_instr_i32_tests(
    FILE *f, func_compile_ctx_t *ctx, unsigned char opcode) {

    stack_entry_t *test_cond = da_pop(ctx->value_stack);
    assert_stack_entry_value(test_cond, I32_VALTYPE);
    unsigned int result_varidx = fresh_var();
    stack_entry_t *entry = alloc_stack_entry_value(result_varidx, I32_VALTYPE);
    da_push(ctx->value_stack, entry);
    switch (opcode) {
        case I32_EQZ_OPCODE:
            fprintf(f, "\t%%t%d =w ceqw %%t%d, 0\n",
                    result_varidx, test_cond->as.value.qbe_varidx);
            break;
        default:
            panic();
    }
    free(test_cond);
}

static void compile_instr_i32_comparisons(
    FILE *f, func_compile_ctx_t *ctx, unsigned char opcode) {

    stack_entry_t *snd_operand = da_pop(ctx->value_stack);
    stack_entry_t *fst_operand = da_pop(ctx->value_stack);
    assert_stack_entry_value(snd_operand, I32_VALTYPE);
    assert_stack_entry_value(fst_operand, I32_VALTYPE);
    unsigned int result_varidx = fresh_var();
    stack_entry_t *entry = alloc_stack_entry_value(result_varidx, I32_VALTYPE);
    da_push(ctx->value_stack, entry);
    fprintf(f, "\t%%t%d =w ", result_varidx);
    switch (opcode) {
        case I32_EQ_OPCODE:
            fprintf(f, "ceqw");
            break;
        case I32_NE_OPCODE:
            fprintf(f, "cnew");
            break;
        case I32_LT_S_OPCODE:
            fprintf(f, "csltw");
            break;
        case I32_LT_U_OPCODE:
            fprintf(f, "cultw");
            break;
        case I32_GT_S_OPCODE:
            fprintf(f, "csgtw");
            break;
        case I32_GT_U_OPCODE:
            fprintf(f, "cugtw");
            break;
        case I32_LE_S_OPCODE:
            fprintf(f, "cslew");
            break;
        case I32_LE_U_OPCODE:
            fprintf(f, "culew");
            break;
        case I32_GE_S_OPCODE:
            fprintf(f, "csgew");
            break;
        case I32_GE_U_OPCODE:
            fprintf(f, "cugew");
            break;
        default:
            panic();
    }
    fprintf(f, " %%t%d, %%t%d\n",
            fst_operand->as.value.qbe_varidx,
            snd_operand->as.value.qbe_varidx);
    free(snd_operand);
    free(fst_operand);
}

static void compile_instr_local_get(FILE *f, func_compile_ctx_t *ctx) {

        unsigned int index, var;
        enum wasm_valtype wasm_type;
        char qbe_type;
        wasm_func_t *func = ctx->func;

        readULEB128_u32(&func->body, &index);
        var = fresh_var();
        if (index < func->type->num_params) {
            wasm_type = func->type->params_type[index];
            index = ctx->ssa_params[index];
        } else if (index - func->type->num_params < func->num_locals) {
            index -= func->type->num_params;
            wasm_type = func->locals_type[index];
            index = ctx->ssa_locals[index];
        } else panic();

        qbe_type = to_qbe_simple_types(wasm_type);
        stack_entry_t *entry = alloc_stack_entry_value(var, wasm_type);
        da_push(ctx->value_stack, entry);
        fprintf(f, "\t%%t%d =%c load%c %%t%d\n", var, qbe_type, qbe_type, index);
}

static void compile_instr_local_set(FILE *f, func_compile_ctx_t *ctx) {

        unsigned int index;
        enum wasm_valtype wasm_type;
        wasm_func_t *func = ctx->func;

        readULEB128_u32(&func->body, &index);
        if (index < func->type->num_params) {
            wasm_type = func->type->params_type[index];
            index = ctx->ssa_params[index];
        } else if (index - func->type->num_params < func->num_locals) {
            index -= func->type->num_params;
            wasm_type = func->locals_type[index];
            index = ctx->ssa_locals[index];
        } else panic();

        stack_entry_t *entry = da_pop(ctx->value_stack);
        assert_stack_entry_value(entry, wasm_type);
        char qbe_type = to_qbe_simple_types(wasm_type);
        fprintf(f, "\tstore%c %%t%d, %%t%d\n", 
                qbe_type,
                entry->as.value.qbe_varidx,
                index);
        free(entry);
}

static void compile_instr_i32_const(FILE *f, func_compile_ctx_t *ctx) {
        int32_t n;
        readILEB128_i32(&ctx->func->body, &n);
        unsigned int var = fresh_var();
        stack_entry_t *entry = alloc_stack_entry_value(var, I32_VALTYPE);
        da_push(ctx->value_stack, entry);
        fprintf(f, "\t%%t%d =w copy %d\n", var, n);
}

static void compile_instr_call(FILE *f, func_compile_ctx_t *ctx) {
        uint32_t funcidx;
        readULEB128_u32(&ctx->func->body, &funcidx);
        if (funcidx >= ctx->m->funcs_len) {
            panic();
        }
        wasm_func_t *func = &ctx->m->funcs[funcidx];
        wasm_func_type_t *type = ctx->m->funcs[funcidx].type;
        if (ctx->value_stack->size < type->num_params) {
            panic();
        }
        unsigned int func_return_value = fresh_var();
        fprintf(f, "\t");
        if (type->return_type != NO_VALTYPE) {
            char qbe_return_type = to_qbe_simple_types(type->return_type);
            fprintf(f, "%%t%d =%c ", func_return_value, qbe_return_type);
        }
        fprintf(f, "call ");
        if (func->export_name != NULL) {
            fprintf(f, "$%s(", func->export_name);
        } else {
            fprintf(f, "$f%d(", funcidx);
        }
        stack_entry_t **args = calloc_or_panic(
            type->num_params, 
            sizeof(stack_entry_t *));
        for (unsigned int i = 0; i < type->num_params; i++) {
             stack_entry_t *entry = da_pop(ctx->value_stack);
            if (entry == NULL) panic();
            args[type->num_params - i - 1] = entry;
        }
        for (unsigned int i = 0; i < type->num_params; i++) {
            assert_stack_entry_value(args[i], type->params_type[i]);
            char qbe_param_type = to_qbe_simple_types(type->params_type[i]);
            fprintf(f, "%c %%t%d", qbe_param_type, args[i]->as.value.qbe_varidx);
            if (i < type->num_params-1) {
                fprintf(f, ", ");
            }
            free(args[i]);
        }
        free(args);
        fprintf(f, ")\n");
        if (type->return_type != NO_VALTYPE) {
            stack_entry_t *entry = alloc_stack_entry_value(
                func_return_value,
                type->return_type);
            da_push(ctx->value_stack, entry);
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

static void compile_instr_br(FILE *f, func_compile_ctx_t *ctx) {
    uint32_t labelidx;

    readULEB128_u32(&ctx->func->body, &labelidx);
    unsigned int size = ctx->label_stack->size;
    if (size <= labelidx) panic();
    label_t *label = ctx->label_stack->items[size - 1 - labelidx];

    if (label->wasm_type != BLOCK_TYPE_NONE) {
        stack_entry_t *result = da_pop(ctx->value_stack);
        assert_stack_entry_value(result, label->wasm_type);
        char qbe_type = to_qbe_simple_types(label->wasm_type);
        fprintf(f, "\t%%t%d =%c copy %%t%d\n",
                label->qbe_result_varidx,
                qbe_type,
                result->as.value.qbe_varidx);
        free(result);
    }
    unwind_value_stack(ctx->value_stack);
    fprintf(f, "\tjmp @l%d\n", label->qbe_labelidx);
    fprintf(f, "@l%d\n", ctx->label_count++);
    ctx->br_or_return_flag = BRANCH_FLAG;
}

static void compile_instr_br_if(FILE *f, func_compile_ctx_t *ctx) {
    uint32_t labelidx;

    /* Pop a value of type i32 from the value stack,
     * that value is the condition of the if. */
    stack_entry_t *ifcond = da_pop(ctx->value_stack);
    assert_stack_entry_value(ifcond, I32_VALTYPE);
    unsigned int qbe_varidx = ifcond->as.value.qbe_varidx;
    free(ifcond);

    readULEB128_u32(&ctx->func->body, &labelidx);
    unsigned int size = ctx->label_stack->size;
    if (size <= labelidx) panic();
    label_t *label = ctx->label_stack->items[size - 1 - labelidx];

    if (label->wasm_type != BLOCK_TYPE_NONE) {
        stack_entry_t *result = da_peak_last(ctx->value_stack);
        assert_stack_entry_value(result, label->wasm_type);
        char qbe_type = to_qbe_simple_types(label->wasm_type);
        fprintf(f, "\t%%t%d =%c copy %%t%d\n",
                label->qbe_result_varidx,
                qbe_type,
                result->as.value.qbe_varidx);
    }

    unsigned int continue_label = ctx->label_count++;
    fprintf(f, "\tjnz %%t%d, @l%d, @l%d\n", 
            qbe_varidx,
            label->qbe_labelidx,
            continue_label);
    fprintf(f, "@l%d\n", continue_label);

}

static void compile_instr_return(FILE *f, func_compile_ctx_t *ctx) {
    dalist_t *value_stack = ctx->value_stack;
    stack_entry_t *result;
    enum wasm_valtype return_type = ctx->func->type->return_type;
    if (return_type != NO_VALTYPE) {
        result = da_pop(value_stack);
        assert_stack_entry_value(result, return_type);
        fprintf(f, "\tret %%t%d\n", result->as.value.qbe_varidx);
        free(result);
    } else {
        fprintf(f, "\tret\n");
    }
    unwind_value_stack(value_stack);
    ctx->br_or_return_flag = RETURN_FLAG;
}

static void common_blocks_logic(
    FILE *f, func_compile_ctx_t *ctx, label_t l,
    unsigned char *last_read_opcode, unsigned char *last_br_or_return_flag) {

    dalist_t *value_stack = ctx->value_stack;
    dalist_t *label_stack = ctx->label_stack;
    read_struct_t *r = &ctx->func->body;

    fprintf(f, "\t%%t%d =w copy 0\n", l.qbe_result_varidx);

    /* add a special stack entry to keep track of the status of the
     * value stack before the start of the compilatoin of this block */
    da_push(value_stack, &bottom_block_stack);

    label_t *label = alloc_label(
        l.qbe_labelidx,
        l.wasm_type,
        l.qbe_result_varidx);
    da_push(label_stack, label);

    unsigned char opcode;
    read_u8(r, &opcode);
    while (opcode != END_CODE && opcode != ELSE_OPCODE) {
        compile_instr(f, ctx, opcode);
        read_u8(r, &opcode);
    }
    if (last_read_opcode != NULL) {
        *last_read_opcode = opcode;
    }
    enum br_or_return_flag branch = ctx->br_or_return_flag;
    if (last_br_or_return_flag != NULL) {
        *last_br_or_return_flag = branch;
    }
    /* reset branch_flag to false, there is no more need to skip
     * instructions between a br and the end of the closest block */
    ctx->br_or_return_flag = NONE;

    if (l.wasm_type != BLOCK_TYPE_NONE && !branch) {
        stack_entry_t *block_result = da_pop(value_stack);
        assert_stack_entry_value(block_result, l.wasm_type);
        unsigned int qbe_type = to_qbe_simple_types(l.wasm_type);
        fprintf(f, "\t%%t%d =%c copy %%t%d\n",
                l.qbe_result_varidx,
                qbe_type,
                block_result->as.value.qbe_varidx);
        free(block_result);
    }
    /* check that the value stack did not change */
    stack_entry_t *bottom = da_pop(value_stack);
    assert_stack_entry_block_end(bottom);

    free(da_pop(ctx->label_stack));
}

static void return_from_blocks(func_compile_ctx_t *ctx, label_t l) {
    if (l.wasm_type != BLOCK_TYPE_NONE) {
        stack_entry_t *result = alloc_stack_entry_value(
            l.qbe_result_varidx,
            l.wasm_type);
        da_push(ctx->value_stack, result);
    }
}

static void compile_instr_if(FILE *f, func_compile_ctx_t *ctx) {
    label_t l;

    /* Pop a value of type i32 from the value stack,
     * that value is the condition of the if. */
    stack_entry_t *ifcond = da_pop(ctx->value_stack);
    assert_stack_entry_value(ifcond, I32_VALTYPE);
    unsigned int ifcond_qbe_varidx = ifcond->as.value.qbe_varidx;
    free(ifcond);

    read_u8(&ctx->func->body, &l.wasm_type);
    assert_block_type(l.wasm_type);
    unsigned int then_label = ctx->label_count++;
    unsigned int else_label = ctx->label_count++;
    unsigned int  end_label = ctx->label_count++;
    l.qbe_labelidx = end_label;
    l.qbe_result_varidx = fresh_var();

    /* if the condition is not zero jump to then_label
     * otherwise  jump to else_label */
    fprintf(f, "\tjnz %%t%d, @l%d, @l%d\n", 
            ifcond_qbe_varidx, then_label, else_label);

    fprintf(f, "@l%d\n", then_label);
    /* start the compilation of the then branch */
    unsigned char last_read_opcode, last_branch_flag;
    common_blocks_logic(f, ctx, l, &last_read_opcode, &last_branch_flag);
    if (!last_branch_flag) {
        fprintf(f, "\tjmp @l%d\n", end_label);
    }

    fprintf(f, "@l%d\n", else_label);
    /* if there is a matching else, start the compilation of the else branch */
    if (last_read_opcode == ELSE_OPCODE) {
        common_blocks_logic(f, ctx, l, &last_read_opcode, NULL);
    }

    if (last_read_opcode != END_CODE) panic();
    return_from_blocks(ctx, l);
    fprintf(f, "@l%d\n", end_label);
}

static void compile_instr_block(FILE *f, func_compile_ctx_t *ctx) {
    label_t l;

    read_u8(&ctx->func->body, &l.wasm_type);
    assert_block_type(l.wasm_type);
    l.qbe_labelidx = ctx->label_count++;
    l.qbe_result_varidx = fresh_var();
    /* start the compilation of the body of the block */
    unsigned char last_read_opcode;
    common_blocks_logic(f, ctx, l, &last_read_opcode, NULL);
    if (last_read_opcode != END_CODE) panic();
    return_from_blocks(ctx, l);
    fprintf(f, "@l%d\n", l.qbe_labelidx);
}

static void compile_instr_loop(FILE *f, func_compile_ctx_t *ctx) {
    label_t l;

    read_u8(&ctx->func->body, &l.wasm_type);
    assert_block_type(l.wasm_type);
    l.qbe_labelidx = ctx->label_count++;
    fprintf(f, "@l%d\n", l.qbe_labelidx);
    l.qbe_result_varidx = fresh_var();
    /* start the compilation of the body of the loop */
    unsigned char last_read_opcode;
    common_blocks_logic(f, ctx, l, &last_read_opcode, NULL);
    if (last_read_opcode != END_CODE) panic();
    return_from_blocks(ctx, l);
}

static void compile_control_instr(
    FILE *f, func_compile_ctx_t *ctx, unsigned char opcode) {

    switch (opcode) {
        case UNREACHABLE_OPCODE:
            fprintf(f, "\thlt\n");
            break;
        case NOP_OPCODE:
            break;
        case BLOCK_OPCODE:
            compile_instr_block(f, ctx);
            break;
        case LOOP_OPCODE:
            compile_instr_loop(f, ctx);
            break;
        case IF_OPCODE:
            compile_instr_if(f, ctx);
            break;
        case BRANCH_OPCODE:
            compile_instr_br(f, ctx);
            break;
        case BRANCH_IF_OPCODE:
            compile_instr_br_if(f, ctx);
            break;
        case RETURN_OPCODE:
            compile_instr_return(f, ctx);
            break;
        case CALL_OPCODE:
            compile_instr_call(f, ctx);
            break;
        default:
            printf("PANIC: opcode = 0x%02X\n", opcode);
            panic();
    }
}
static void compile_parametric_instr(
    FILE *f, func_compile_ctx_t *ctx, unsigned char opcode) {

    switch (opcode) {
        case DROP_OPCODE: {
            stack_entry_t *entry = da_pop(ctx->value_stack);
            if (entry == NULL || entry->kind != STACK_ENTRY_VALUE) {
                panic();
            }
            free(entry);
        } break;
        case SELECT_OPCODE: {
            // TODO: missing check on select_cond, snd_operand, fst_operand
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
            enum wasm_valtype wasm_type = snd_operand->as.value.wasm_type;
            unsigned int select_snd = ctx->label_count++;
            unsigned int select_fst = ctx->label_count++;
            unsigned int end = ctx->label_count++;
            unsigned int result_var = fresh_var();

            fprintf(f, "\tjnz %%t%d, @l%d, @l%d\n",
                    select_cond->as.value.qbe_varidx,
                    select_fst, select_snd);
            fprintf(f, "@l%d\n", select_fst);
            fprintf(f, "\tjmp @l%d\n", end);
            fprintf(f, "@l%d\n", select_snd);
            fprintf(f, "\tjmp @l%d\n", end);
            fprintf(f, "@l%d\n", end);
            // TODO: change =w with the correct size of wasm_type
            fprintf(f, "\t%%t%d =w phi @l%d %%t%d, @l%d %%t%d\n",
                    result_var,
                    select_fst,
                    fst_operand->as.value.qbe_varidx,
                    select_snd,
                    snd_operand->as.value.qbe_varidx);
           stack_entry_t *result = alloc_stack_entry_value(result_var, wasm_type);
            da_push(ctx->value_stack, result);
            free(select_cond);
            free(snd_operand);
            free(fst_operand);
        } break;
        default:
            printf("PANIC: opcode = 0x%02X\n", opcode);
            panic();

    }
}

static void compile_variable_instr(
    FILE *f, func_compile_ctx_t *ctx, unsigned char opcode) {

    switch (opcode) {
        case LOCAL_GET_OPCODE:
            compile_instr_local_get(f, ctx);
            break;
        case LOCAL_SET_OPCODE:
            compile_instr_local_set(f, ctx);
            break;
        case GLOBAL_GET_OPCODE: {
            uint32_t globalidx;
            readULEB128_u32(&ctx->func->body, &globalidx);
            unsigned int var = fresh_var();
            if (globalidx >= ctx->m->globals_len) panic();
            global_t *g = &ctx->m->globals[globalidx];
            if (g->expr.type == NO_VALTYPE) panic();
            char qbe_type = to_qbe_simple_types(g->expr.type);
            stack_entry_t *entry = alloc_stack_entry_value(var, g->expr.type);
            da_push(ctx->value_stack, entry);
            fprintf(f, "\t%%t%d =%c load%c $g%d\n",
                    var, qbe_type, qbe_type, globalidx);

        } break;
        case GLOBAL_SET_OPCODE:
        default: 
            printf("PANIC: opcode = 0x%02X\n", opcode);
            panic();
    }
}
static void compile_i32_load_instr(FILE *f, func_compile_ctx_t *ctx) {
    assert_memory0_exists(ctx);
    //TODO: how to properly use align?
    uint32_t align, offset;
    readULEB128_u32(&ctx->func->body, &align);
    readULEB128_u32(&ctx->func->body, &offset);
    unsigned int result_varidx = fresh_var();
    unsigned int address_plus_offset = fresh_var();
    stack_entry_t *address = da_pop(ctx->value_stack);
    assert_stack_entry_value(address, I32_VALTYPE);
    fprintf(f, "\t%%t%d =l extsw %%t%d\n",
        address_plus_offset, address->as.value.qbe_varidx);
    fprintf(f, "\t%%t%d =l add %%t%d, %d\n",
            address_plus_offset, address_plus_offset, offset);
    fprintf(f, "\t%%t%d =l add $mem0, %%t%d\n",
            address_plus_offset, address_plus_offset);
    //TODO: out of bound memory check
    fprintf(f, "\t%%t%d =w loadw %%t%d\n", result_varidx, address_plus_offset);
    stack_entry_t *result = alloc_stack_entry_value(result_varidx, I32_VALTYPE);
    da_push(ctx->value_stack, result);
    free(address);
}

static void compile_i32_store_instr(FILE *f, func_compile_ctx_t *ctx) {
    assert_memory0_exists(ctx);
    //TODO: how to properly use align?
    uint32_t align, offset;
    readULEB128_u32(&ctx->func->body, &align);
    readULEB128_u32(&ctx->func->body, &offset);
    stack_entry_t *value = da_pop(ctx->value_stack);
    assert_stack_entry_value(value, I32_VALTYPE);
    stack_entry_t *address = da_pop(ctx->value_stack);
    assert_stack_entry_value(address, I32_VALTYPE);
    unsigned int address_plus_offset = fresh_var();
    fprintf(f, "\t%%t%d =l extsw %%t%d\n",
        address_plus_offset, address->as.value.qbe_varidx);
    fprintf(f, "\t%%t%d =l add %%t%d, %d\n",
            address_plus_offset, address_plus_offset, offset);
    fprintf(f, "\t%%t%d =l add $mem0, %%t%d\n",
            address_plus_offset, address_plus_offset);
    //TODO: out of bound memory check
    fprintf(f, "\tstorew %%t%d, %%t%d\n",
            value->as.value.qbe_varidx, address_plus_offset);
    free(address);
    free(value);
}

static void compile_memory_instr(
    FILE *f, func_compile_ctx_t *ctx, unsigned char opcode) {

    switch (opcode) {
        case I32_LOAD_OPCODE:
            compile_i32_load_instr(f, ctx);
            break;
        case I32_STORE_OPCODE:
            compile_i32_store_instr(f, ctx);
            break;
        default:
            printf("PANIC: opcode = 0x%02X\n", opcode);
            panic();
    }
}


static void compile_numeric_instr(
    FILE *f, func_compile_ctx_t *ctx, unsigned char opcode) {

    switch (opcode) {
        case I32_CONST_OPCODE:
            compile_instr_i32_const(f, ctx);
            break;
        case I32_EQZ_OPCODE:
            compile_instr_i32_tests(f, ctx, opcode);
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
            compile_instr_i32_comparisons(f, ctx, opcode);
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
            compile_instr_i32_binop(f, ctx, opcode);
            break;
                default:
            printf("PANIC: opcode = 0x%02X\n", opcode);
            panic();
    }
}



static void compile_instr(FILE *f, func_compile_ctx_t *ctx, unsigned char opcode) {
    /* if the branch_flag is true, we are trying to compile an instruction
     * between a br and the end of the closest block, so skip it */
    if (ctx->br_or_return_flag != NONE) return;
    else if (opcode <= 0x11) {
        compile_control_instr(f, ctx, opcode);
    }
    else if (0x1A <= opcode && opcode <= 0x1B) {
        compile_parametric_instr(f, ctx, opcode);
    }
    else if (0x20 <= opcode && opcode <= 0x24) {
        compile_variable_instr(f, ctx, opcode);
    }
    else if (0x28 <= opcode && opcode <= 0x40) {
        compile_memory_instr(f, ctx, opcode);
    }
    else if (0x41 <= opcode && opcode <= 0xBF) {
        compile_numeric_instr(f, ctx, opcode);
    }
    else {
        printf("PANIC: opcode = 0x%02X\n", opcode);
        panic();
    }
}

static void compile_func(FILE *f, wasm_module_t *m, unsigned int func_index) {
    wasm_func_t *func = &m->funcs[func_index];
    wasm_func_type_t *type = func->type;
    unsigned int *qbe_params_varidx, *qbe_locals_varidx;

    qbe_params_varidx = qbe_locals_varidx = NULL;
    if (type->num_params > 0) {
     qbe_params_varidx = calloc_or_panic(
            type->num_params,
            sizeof(unsigned int));
    }
    if (func->num_locals > 0) {
        qbe_locals_varidx = calloc_or_panic(
            func->num_locals,
            sizeof(unsigned int));
    }

    char qbe_return_type = ' ';
    if (type->return_type != NO_VALTYPE) {
        qbe_return_type = to_qbe_simple_types(type->return_type);
    }
    if (func->export_name != NULL) {
        fprintf(f, "export function %c $%s(", 
                qbe_return_type,
                func->export_name);
    } else {
        fprintf(f, "function %c $f%d(", 
                qbe_return_type,
                func_index);
    }
    for (unsigned int i = 0; i < type->num_params; i++) {
        char qbe_param_type = to_qbe_simple_types(type->params_type[i]);
        unsigned int qbe_param_varidx = fresh_var();
        fprintf(f, "%c %%t%d", qbe_param_type, qbe_param_varidx);
        qbe_params_varidx[i] = qbe_param_varidx;
        if (i < type->num_params-1) {
            fprintf(f, ", ");
        }
    }
    fprintf(f, ") {\n");
    fprintf(f, "@start\n");
    /* Store and initialize function parameters on the stack */
    for (unsigned int i = 0; i < type->num_params; i++) {
        char qbe_param_type = to_qbe_simple_types(type->params_type[i]);
        unsigned int align, size;
        align = size = type_size_in_bytes(type->params_type[i]);
        unsigned int qbe_param_varidx = fresh_var();
        fprintf(f, "\t%%t%d =l alloc%d %d\n", qbe_param_varidx, align, size);
        fprintf(f, "\tstore%c %%t%d, %%t%d\n",
                qbe_param_type,
                qbe_params_varidx[i],
                qbe_param_varidx);
        qbe_params_varidx[i] = qbe_param_varidx;
    }
    /* Store and initialize function local variables on the stack */
    for (unsigned int i = 0; i < func->num_locals; i++) {
        char qbe_local_type = to_qbe_simple_types(func->locals_type[i]);
        unsigned int align, size;
        align = size = type_size_in_bytes(func->locals_type[i]);
        unsigned int qbe_local_varidx = fresh_var();
        fprintf(f, "\t%%t%d =l alloc%d %d\n", qbe_local_varidx, align, size);
        fprintf(f, "\tstore%c 0, %%t%d\n", qbe_local_type, qbe_local_varidx);
        qbe_locals_varidx[i] = qbe_local_varidx;
    }

    dalist_t value_stack = {0};
    dalist_t label_stack = {0};
    func_compile_ctx_t ctx = {
        .m = m,
        .func = func,
        .value_stack = &value_stack,
        .label_stack = &label_stack,
        .ssa_params = qbe_params_varidx,
        .ssa_locals = qbe_locals_varidx,
        .label_count = 0,
        .br_or_return_flag = NONE,
    };
    da_push(ctx.value_stack, &bottom_block_stack);
    unsigned char opcode;
    while (func->body.offset != func->body.end) {
        read_u8(&func->body, &opcode);
        compile_instr(f, &ctx, opcode);
    }
    if (ctx.br_or_return_flag != RETURN_FLAG) {
        if (func->type->return_type != NO_VALTYPE) {
            stack_entry_t *entry = da_pop(&value_stack);
            assert_stack_entry_value(entry, func->type->return_type);
            fprintf(f, "\tret %%t%d\n", entry->as.value.qbe_varidx);
            free(entry);
        } else {
            fprintf(f, "\tret\n");
        }
    }
    stack_entry_t *bottom = da_pop(ctx.value_stack);
    assert_stack_entry_block_end(bottom);

    fprintf(f, "}\n");
    if (qbe_params_varidx != NULL) free(qbe_params_varidx);
    if (qbe_locals_varidx != NULL) free(qbe_locals_varidx);
    da_free(&value_stack);
}

static void compile_globals(wasm_module_t *m, FILE *out) {
    for (uint32_t i = 0; i < m->globals_len; i++) {
        global_t *g = &m->globals[i];
        char qbe_type = to_qbe_simple_types(g->expr.type);
        /* How to specify align? */
        fprintf(out, "data $g%d = { %c ", i, qbe_type);
        switch (g->expr.type) {
            case I32_VALTYPE:
                fprintf(out, "%d", g->expr.as.i32);
                break;
            default:
                panic();
        }
        fprintf(out, " }\n");
    }
}

static int compare_data_segment(const void *lhs, const void *rhs) {
    data_segment_t *lhs_ds = (data_segment_t *) lhs;
    data_segment_t *rhs_ds = (data_segment_t *) rhs;
    return lhs_ds->offset - rhs_ds->offset;
}

static void compile_data_segments(wasm_module_t *m, FILE *out) {
    if (m->mem.min_page_num == 0) return;
    qsort( m->data_segments, m->num_data_segments, 
          sizeof(data_segment_t), compare_data_segment);
    unsigned int mem_size = m->mem.min_page_num * 64 * 1024;
    fprintf(out, "data $mem0 = { ");
    unsigned int mem_offset = 0;
    for (uint32_t i = 0; i < m->num_data_segments; i++) {
        data_segment_t *d = &m->data_segments[i];
        if (d->offset < mem_offset) panic();
        fprintf(out, "z %d, ", d->offset - mem_offset);
            fprintf(out, "b \"");
        for (uint32_t j = 0; j < d->init_len; j++) {
            fprintf(out, "\\x%02x", d->init_bytes[j]);
        }
        fprintf(out, "\", ");
        mem_offset = d->offset + d->init_len;
    }
    fprintf(out, "z %d }\n", mem_size - mem_offset);
}

void compile(wasm_module_t *m, FILE *out) {
    compile_data_segments(m, out);
    compile_globals(m, out);
    for (unsigned int i = 0; i < m->funcs_len; i++) {
        compile_func(out, m, i);
    }
}
