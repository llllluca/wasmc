#ifndef ALL_H_INCLUDED
#define ALL_H_INCLUDED

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "dalist.h"

typedef struct {
    /* It is needed at least min_page_num page of memory
     * in order to execute the WebAssembly module.
     * (WebAssembly currently defines a page to be 64KB) */
    unsigned int min_page_num;
    /* max_page_num is optional, 0 means unlimited. */
    unsigned int max_page_num;
    /* If min_page_num and max_page_num are both 0 the
     * WebAssembly module dosn't use memory */
} memory_info_t;

typedef struct {
    unsigned char return_type;
    /* heap allocated array of length num_params. If a function has
     * no parameters params_type is NULL and num_params is 0. */
    unsigned char *params_type;
    unsigned int num_params;
} wasm_func_type_t;

typedef struct {
    unsigned char *start;
    unsigned char *offset;
    unsigned char *end;
    unsigned int len;
} read_struct_t;

typedef struct {
    read_struct_t body; 
    wasm_func_type_t *type; 
    /* Heap allocated array of length num_locals. If a function has
     * no locals variables locals_type is NULL and num_locals is 0.*/
    unsigned char *locals_type;
    unsigned int num_locals;
    /* Heap allocated and null terminated string. If a function is not
     * exported, export_name is NULL */
    char *export_name;
} wasm_func_t;

typedef struct {
    read_struct_t module;
    memory_info_t mem;
    /* Heap allocated array of length types_len. If a module has no functions
     * types in the type section, types is NULL and types_len is 0. */
    wasm_func_type_t *types;
    unsigned int types_len;
    /* Heap allocated array of length func_len. If a module has
     * no functions funcs is NULL and funcs_len is 0. */
    wasm_func_t *funcs;
    unsigned int funcs_len;
    /* types_len is not always equal to funcs_len, if two function has
     * the same type they share the same wasm_func_type_t struct. */
} wasm_module_t;

enum stack_entry_kind {
    STACK_ENTRY_LABEL,
    STACK_ENTRY_VALUE,
};

typedef struct {
    unsigned int qbe_labelidx;
} stack_label_t;

typedef struct {
    unsigned int qbe_varidx;
    unsigned char wasm_type;
} stack_value_t;

union stack_value_or_label {
    stack_value_t value;
    stack_label_t label;
};

typedef struct {
    enum stack_entry_kind kind;
    union stack_value_or_label as;
} stack_entry_t;

typedef struct {
    wasm_module_t *m;
    wasm_func_t *func;
    dalist_t *stack;
    unsigned int label_count;
    unsigned int *ssa_params;
    unsigned int *ssa_locals;
} func_compile_ctx_t;

typedef int err_t;
#define OK    0
#define FAIL -1

#define WASM_MAGIC_NUMBER 0x6d736100
#define WASM_VERSION 1

#define SECTIONS_NUMBER     12
#define CUSTOM_SECTION_ID    0
#define TYPE_SECTION_ID      1
#define IMPORT_SECTION_ID    2
#define FUNCTION_SECTION_ID  3
#define TABLE_SECTION_ID     4
#define MEMORY_SECTION_ID    5
#define GLOBAL_SECTION_ID    6
#define EXPORT_SECTION_ID    7
#define START_SECTION_ID     8
#define ELEMENT_SECTION_ID   9
#define CODE_SECTION_ID     10
#define DATA_SECTION_ID     10


// Variable instructions
#define LOCAL_GET_OPCODE  0x20
#define LOCAL_SET_OPCODE  0x21
#define LOCAL_TEE_OPCODE  0x22
#define GLOBAL_GET_OPCODE 0x23
#define GLOBAL_SET_OPCODE 0x24

// Control instructions
#define UNREACHABLE_OPCODE   0x00
#define NOP_OPCODE           0x01
#define BLOCK_OPCODE         0x02
#define LOOP_OPCODE          0x03
#define IF_OPCODE            0x04
#define ELSE_OPCODE          0x05
#define BRANCH_OPCODE        0x0C
#define BRANCH_IF_OPCODE     0x0D
#define BRANCH_TABLE_OPCODE  0x0E
#define RETURN_OPCODE        0x0F
#define CALL_OPCODE          0x10
#define CALL_INDIRECT_OPCODE 0x11

// Numeric instruction opcodes
#define I32_ADD_OPCODE   0x6A
#define I32_SUB_OPCODE   0x6B
#define I32_CONST_OPCODE 0x41

#define FUNCTION_START_CODE 0x60
#define FUNCTION_END_CODE   0x0B
#define END_CODE            0x0B
#define BLOCK_EMPTY_TYPE    0x40

#define ONLY_MIN_MEMORY_LIMIT 0x00
#define BOTH_MIN_AND_MAX_MEMORY_LIMIT 0x01
#define UNLIMITED_MAX_PAGE_NUM 0

#define NO_TYPE     0x00
#define I32_VALTYPE 0x7F
#define I64_VALTYPE 0x7E
#define F32_VALTYPE 0x7D
#define F64_VALTYPE 0x7C

void read_u8(read_struct_t *r, unsigned char *out);
void read_u32(read_struct_t *r, uint32_t *out);
unsigned int readULEB128_u32(read_struct_t *r, uint32_t *out);
unsigned int readULEB128_i32(read_struct_t *r, int32_t *out);
wasm_module_t* parse(unsigned char *start, unsigned int len);
void free_wasm_module(wasm_module_t *m);
void panic(void);
void *calloc_or_panic(size_t nmemb, size_t size);
void *malloc_or_panic(size_t size);
void compile(wasm_module_t *m, FILE *out);

#endif
