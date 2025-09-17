#include "all.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dalist.h"

static void compile_instr(FILE *f, func_compile_ctx_t *ctx, unsigned char opcode);

char to_qbe_simple_types(unsigned char wasm_type) {
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
static unsigned int type_size_in_bytes(unsigned char wasm_type) {
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

static void compile_instr_i32_binop(FILE *f, func_compile_ctx_t *ctx, unsigned char opcode) {

    unsigned int var;
    dalist_t *stack = ctx->stack;

    if (stack->size < 2) {
        panic();
    }
    stack_item_t *snd = da_pop(stack);
    stack_item_t *fst = da_pop(stack);
    if (snd->type != I32_VALTYPE || fst->type != I32_VALTYPE) {
        panic();
    }
    var = fresh_var();
    stack_item_t *item = malloc_or_panic(sizeof(stack_item_t));
    item->ssa_var = var;
    item->type = I32_VALTYPE;
    da_push(stack, item);
    fprintf(f, "\t%%t%d =w ", var);
    switch (opcode) {
        case I32_ADD_OPCODE:
            fprintf(f, "add");
            break;
        case I32_SUB_OPCODE:
            fprintf(f, "sub");
            break;
        default:
            panic();
    }
    fprintf(f, " %%t%d, %%t%d\n", fst->ssa_var, snd->ssa_var);
    free(snd);
    free(fst);
}


static void compile_instr_local_get(FILE *f, func_compile_ctx_t *ctx) {

        unsigned int index, var;
        unsigned char wasm_type, qbe_type;
        wasm_func_t *func = ctx->func;

        readULEB128_u32(&func->body, &index);
        var = fresh_var();
        if (index < func->type->num_params) {
            wasm_type = func->type->params_type[index];
            index = ctx->ssa_params[index];
        } else if (index < func->num_locals) {
            wasm_type = func->locals_type[index];
            index = ctx->ssa_locals[index];
        } else panic();

        qbe_type = to_qbe_simple_types(wasm_type);
        stack_item_t *item = malloc_or_panic(sizeof(stack_item_t));
        item->ssa_var = var;
        item->type = wasm_type;
        da_push(ctx->stack, item);
        fprintf(f, "\t%%t%d =%c load%c %%t%d\n", var, qbe_type, qbe_type, index);
}

static void compile_instr_i32_const(FILE *f, func_compile_ctx_t *ctx) {
            int32_t n;
            wasm_func_t *func = ctx->func;
            unsigned int var;

            readULEB128_i32(&func->body, &n);
            var = fresh_var();
            stack_item_t *item = malloc_or_panic(sizeof(stack_item_t));
            item->ssa_var = var;
            item->type = I32_VALTYPE;
            da_push(ctx->stack, item);
            /* Constants in QBE IR are always parsed as 64-bit blobs.
             * Depending on the context surrounding a constant,
             * only some of its bits are used. */
            fprintf(f, "\t%%t%d =w copy %d\n", var, n);
}


/*
static void compile_instr_block(FILE *f, func_compile_ctx_t *ctx) {
    wasm_func_t *func = ctx->func;
    unsigned char block_result_type;
    unsigned char opcode;

    read_u8(&func->body, &block_result_type);
    if (block_result_type != BLOCK_EMPTY_TYPE &&
        block_result_type != I32_VALTYPE &&
        block_result_type != I64_VALTYPE) {
        // TODO: add support for F32_VALTYPE and F64_VALTYPE
        panic();
    }
    // TODO: add support for nested block

}
*/

static void compile_instr_call(FILE *f, func_compile_ctx_t *ctx) {
        uint32_t funcidx;
        wasm_func_type_t *type;
        wasm_func_t *func;
        unsigned int var;
        unsigned char qbe_return_type, qbe_param_type;

        readULEB128_u32(&ctx->func->body, &funcidx);
        if (funcidx >= ctx->m->funcs_len) {
            panic();
        }
        func = &ctx->m->funcs[funcidx];
        type = ctx->m->funcs[funcidx].type;
        if (ctx->stack->size < type->num_params) {
            panic();
        }
        var = fresh_var();
        // TODO: function without return type
        qbe_return_type = to_qbe_simple_types(type->return_type);
        if (func->export_name != NULL) {
            fprintf(f, "\t%%t%d =%c call $%s(", var, qbe_return_type, func->export_name);
        } else {
            fprintf(f, "\t%%t%d =%c call $f%d(", var, qbe_return_type, funcidx);
        }

        stack_item_t **args = calloc_or_panic(type->num_params, sizeof(stack_item_t *));
        for (unsigned int i = 0; i < type->num_params; i++) {
            args[type->num_params - i - 1] = da_pop(ctx->stack);
        }
        for (unsigned int i = 0; i < type->num_params; i++) {
            if (args[i]->type != type->params_type[i]) {
                panic();
            }
            qbe_param_type = to_qbe_simple_types(type->params_type[i]);
            fprintf(f, "%c %%t%d", qbe_param_type,  args[i]->ssa_var);
            if (i < type->num_params-1) {
                fprintf(f, ", ");
            }
            free(args[i]);
        }
        free(args);
        fprintf(f, ")\n");
        if (type->return_type != NO_TYPE) {
            stack_item_t *item = malloc_or_panic(sizeof(stack_item_t));
            item->ssa_var = var;
            item->type = type->return_type;
            da_push(ctx->stack, item);
        }
}

static void compile_instr_if(FILE *f, func_compile_ctx_t *ctx) {
    dalist_t *stack = ctx->stack;
    read_struct_t *r = &ctx->func->body;
    stack_item_t *item;
    unsigned char opcode, block_type;
    unsigned int then_label, else_label, end_label, result_var;
    dalist_t new_stack = {0};

    item = da_pop(stack);
    if (item == NULL || item->type != I32_VALTYPE) {
        panic();
    }
    read_u8(r, &block_type);
    then_label = ctx->label_count++;
    else_label = ctx->label_count++;
    end_label = ctx->label_count++;
    result_var = fresh_var();
    fprintf(f, "\tjnz %%t%d, @l%d, @l%d\n", item->ssa_var, then_label, else_label);
    free(item);
    fprintf(f, "@l%d\n", then_label);

    ctx->stack = &new_stack;
    read_u8(r, &opcode);
    while (opcode != ELSE_OPCODE && opcode != END_CODE) {
        compile_instr(f, ctx, opcode);
        read_u8(r, &opcode);
    }
    if (block_type != 0x40) {
        item = da_pop(&new_stack);
        if (item == NULL || item->type != block_type) {
            panic();
        }
        unsigned int qbe_type = to_qbe_simple_types(block_type);
        fprintf(f, "\t%%t%d =%c copy %%t%d\n", result_var, qbe_type, item->ssa_var);
        free(item);
    }
    if (new_stack.size != 0) {
        panic();
    }
    fprintf(f, "@l%d\n", else_label);
    if (opcode == ELSE_OPCODE) {
        read_u8(r, &opcode);
        while (opcode != END_CODE) {
            compile_instr(f, ctx, opcode);
            read_u8(r, &opcode);
        }
    }
    if (block_type != 0x40) {
        item = da_pop(&new_stack);
        if (item == NULL || item->type != block_type) {
            panic();
        }
        unsigned int qbe_type = to_qbe_simple_types(block_type);
        fprintf(f, "\t%%t%d =%c copy %%t%d\n", result_var, qbe_type, item->ssa_var);
        free(item);
    }
    if (new_stack.size != 0) {
        panic();
    }
    ctx->stack = stack;
    da_free(&new_stack);
    if (block_type != 0x40) {
        item = malloc_or_panic(sizeof(stack_item_t));
        item->ssa_var = result_var;
        item->type = block_type;
        da_push(stack, item);
    }
    fprintf(f, "@l%d\n", end_label);
}

static void compile_instr(FILE *f, func_compile_ctx_t *ctx, unsigned char opcode) {

    switch (opcode) {
        case UNREACHABLE_OPCODE:
            fprintf(f, "\thlt\n");
            break;
        case NOP_OPCODE:
            break;
        case LOCAL_GET_OPCODE: 
            compile_instr_local_get(f, ctx);
            break;
        case I32_ADD_OPCODE:
        case I32_SUB_OPCODE:
            compile_instr_i32_binop(f, ctx, opcode);
            break;
        case I32_CONST_OPCODE:
            compile_instr_i32_const(f, ctx);
            break;
        case CALL_OPCODE:
            compile_instr_call(f, ctx);
            break;
        case IF_OPCODE:
            compile_instr_if(f, ctx);
            break;
        default:
            printf("PANIC: opcode = 0x%02X\n", opcode);
            panic();
    }
}

static void compile_func(FILE *f, wasm_module_t *m, unsigned int func_index) {

    wasm_func_t *func;
    wasm_func_type_t *type;
    char return_type, param_type;
    unsigned int align, size, var, *ssa_params, *ssa_locals;
    dalist_t stack = {0};

    func = &m->funcs[func_index];
    type = func->type;
    ssa_params = ssa_locals = NULL;
    if (type->num_params > 0) {
        ssa_params = calloc_or_panic(type->num_params, sizeof(unsigned int));
    }
    if (func->num_locals > 0) {
        ssa_locals = calloc_or_panic(func->num_locals, sizeof(unsigned int));
    }

    return_type = to_qbe_simple_types(type->return_type);
    if (func->export_name != NULL) {
        fprintf(f, "export function %c $%s(", return_type, func->export_name);
    } else {
        fprintf(f, "function %c $f%d(", return_type, func_index);
    }
    for (unsigned int j = 0; j < type->num_params; j++) {
        param_type = to_qbe_simple_types(type->params_type[j]);
        var = fresh_var();
        fprintf(f, "%c %%t%d", param_type, var);
        ssa_params[j] = var;
        if (j < type->num_params-1) {
            fprintf(f, ", ");
        }
    }
    fprintf(f, ") {\n");
    fprintf(f, "@start\n");
    for (unsigned int j = 0; j < type->num_params; j++) {
        param_type = to_qbe_simple_types(type->params_type[j]);
        align = size = type_size_in_bytes(type->params_type[j]);
        var = fresh_var();
        fprintf(f, "\t%%t%d =l alloc%d %d\n", var, align, size);
        fprintf(f, "\tstore%c %%t%d, %%t%d\n", param_type, ssa_params[j], var);
        ssa_params[j] = var;
    }
    for (unsigned int j = 0; j < func->num_locals; j++) {
        align = size = type_size_in_bytes(func->locals_type[j]);
        var = fresh_var();
        fprintf(f, "\t%%t%d =l alloc%d %d\n", var, align, size);
        ssa_locals[j] = var;
    }

    func_compile_ctx_t ctx = {
        .m = m,
        .func = func,
        .stack = &stack,
        .ssa_params = ssa_params,
        .ssa_locals = ssa_locals,
        .label_count = 0,
    };

    unsigned char opcode;
    while (func->body.offset != func->body.end) {
        read_u8(&func->body, &opcode);
        compile_instr(f, &ctx, opcode);
    }

    stack_item_t *item = da_pop(&stack);
    if (item == NULL || item->type != func->type->return_type || stack.size != 0) {
        panic();
    }
    fprintf(f, "\tret %%t%d\n", item->ssa_var);
    fprintf(f, "}\n");
    free(item);
    if (ssa_params != NULL) free(ssa_params);
    if (ssa_locals != NULL) free(ssa_locals);
    da_free(&stack);
}

void compile(wasm_module_t *m, FILE *out) {
    for (unsigned int i = 0; i < m->funcs_len; i++) {
        compile_func(out, m, i);
    }
}
