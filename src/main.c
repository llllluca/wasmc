// this code dosn't work on big endian system
#include "all.h"
#include <stdio.h>
#include <string.h>

void print_value_type(unsigned int val) {
    switch (val) {
        case I32_VALTYPE:
            printf("I32_VALTYPE");
            break;
        case I64_VALTYPE:
            printf("I64_VALTYPE");
            break;
        case F32_VALTYPE:
            printf("F32_VALTYPE");
            break;
        case F64_VALTYPE:
            printf("F64_VALTYPE");
            break;
        default:
            printf("ERROR");
    }
}
void print_wasm_module(wasm_module_t *m) {
    // print types
    printf("types length: %d\n", m->types_len);
    for (unsigned int i = 0; i < m->types_len; i++) {
        wasm_func_type_t *type = &m->types[i];
        printf("%d: ", i);
        for (unsigned int i = 0; i < type->num_params; i++) {
            unsigned char param = type->params_type[i];
            print_value_type(param);
            printf(" ");
        }
        printf("-> ");
        print_value_type(type->return_type);
        printf("\n");
    }

    //print funcs
    printf("functions length: %d\n", m->funcs_len);
    for (unsigned int i = 0; i < m->funcs_len; i++) {
        wasm_func_t *func = &m->funcs[i];
        unsigned int type_index = func->type - m->types;
        printf("function %d has type %d\n", i, type_index);
        if (func->export_name != NULL) {
            printf("function %d is exported with name %s\n", i, func->export_name);
        }
    }
    printf("memory limit: min %d, max %d\n", m->mem.min_page_num, m->mem.max_page_num);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        return 0;
    }
    FILE *input = fopen(argv[1], "r");
    if (input == NULL) {
        return 1;
    }
    fseek(input, 0L, SEEK_END);
    unsigned int wasm_len = ftell(input);
    fseek(input, 0L, SEEK_SET);
    unsigned char *wasm_buf = calloc(wasm_len, sizeof(char));
    if (wasm_buf == NULL) {
        return 1;
    }
    fread(wasm_buf, sizeof(char), wasm_len, input);
    fclose(input);

    wasm_module_t *m = parse(wasm_buf, wasm_len);
    //print_wasm_module(m);

    unsigned int len = strlen(argv[1]) + 4 + 1;
    char *output_path = calloc(len, sizeof(char));
    snprintf(output_path, len, "%s.ssa", argv[1]);
    FILE *output = fopen(output_path, "w");
    compile(m, output);
    fclose(output);

    free_wasm_module(m);
    free(wasm_buf);
    free(output_path);
    return 0;
}
