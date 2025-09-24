#ifndef ALL_H_INCLUDED
#define ALL_H_INCLUDED

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "dalist.h"

typedef int err_t;
#define OK    0
#define FAIL -1

typedef uint8_t boolean;

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

/* wasm_valtype enum occupies 1 byte */
enum  __attribute__ ((__packed__)) wasm_valtype {
    NO_VALTYPE  = 0x00,
    I32_VALTYPE = 0x7F,
    I64_VALTYPE = 0x7E,
    F32_VALTYPE = 0x7D,
    F64_VALTYPE = 0x7C,
};

typedef struct {
    enum wasm_valtype return_type;
    /* heap allocated array of length num_params. If a function has
     * no parameters params_type is NULL and num_params is 0. */
    enum wasm_valtype *params_type;
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
    enum wasm_valtype *locals_type;
    unsigned int num_locals;
    /* Heap allocated and null terminated string. If a function is not
     * exported, export_name is NULL */
    char *export_name;
} wasm_func_t;

typedef struct {
    enum wasm_valtype type;
    union {
        int32_t i32;
        float   f32;
        int64_t i64;
        double  f64;
    } as;
} const_expr_t;

typedef struct {
    const_expr_t expr;
    boolean is_mutable;
} global_t;

typedef struct {
    uint32_t offset;
    uint32_t init_len;
    unsigned char *init_bytes;
} data_segment_t;

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
    global_t *globals;
    uint32_t globals_len;

    data_segment_t *data_segments;
    uint32_t num_data_segments;
} wasm_module_t;

enum stack_entry_kind {
    STACK_ENTRY_VALUE,
    STACK_ENTRY_BLOCK_END,
};

typedef struct {
    unsigned int qbe_varidx;
    unsigned char wasm_type;
} value_t;

typedef struct {
    unsigned int dummy;
} block_end_t;

union value_or_block_end {
    value_t value;
    block_end_t block_end;
};

typedef struct {
    enum stack_entry_kind kind;
    union value_or_block_end as;
} stack_entry_t;

typedef struct {
    unsigned int qbe_labelidx;
    unsigned int qbe_result_varidx;
    unsigned char wasm_type;
} label_t;

enum br_or_return_flag {
    NONE,
    RETURN_FLAG,
    BRANCH_FLAG
};

typedef struct {
    wasm_module_t *m;
    wasm_func_t *func;
    dalist_t *value_stack;
    dalist_t *label_stack;
    unsigned int label_count;
    unsigned int *ssa_params;
    unsigned int *ssa_locals;
    enum br_or_return_flag br_or_return_flag;
} func_compile_ctx_t;

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
#define DATA_SECTION_ID     11

// Control instructions
#define UNREACHABLE_OPCODE   0x00
#define NOP_OPCODE           0x01
#define BLOCK_OPCODE         0x02
#define LOOP_OPCODE          0x03
#define IF_OPCODE            0x04
#define ELSE_OPCODE          0x05
#define BRANCH_OPCODE        0x0C
#define BRANCH_IF_OPCODE     0x0D
#define BRANCH_TABLE_OPCODE  0x0E //not implemented
#define RETURN_OPCODE        0x0F
#define CALL_OPCODE          0x10
#define CALL_INDIRECT_OPCODE 0x11 //not implemented

// Parametric instruction
#define DROP_OPCODE   0x1A
#define SELECT_OPCODE 0x1B

// Variable instructions
#define LOCAL_GET_OPCODE  0x20
#define LOCAL_SET_OPCODE  0x21
#define LOCAL_TEE_OPCODE  0x22 //not implemented
#define GLOBAL_GET_OPCODE 0x23
#define GLOBAL_SET_OPCODE 0x25

// Memory instructions
#define I32_LOAD_OPCODE  0x28
#define I32_STORE_OPCODE 0x36

// Numeric instruction opcodes
#define I32_CONST_OPCODE  0x41
#define I64_CONST_OPCODE  0x42
#define F32_CONST_OPCODE  0x43
#define F64_CONST_OPCODE  0x44
#define I32_EQZ_OPCODE    0x45
#define I32_EQ_OPCODE     0x46
#define I32_NE_OPCODE     0x47
#define I32_LT_S_OPCODE   0x48
#define I32_LT_U_OPCODE   0x49
#define I32_GT_S_OPCODE   0x4A
#define I32_GT_U_OPCODE   0x4B
#define I32_LE_S_OPCODE   0x4C
#define I32_LE_U_OPCODE   0x4D
#define I32_GE_S_OPCODE   0x4E
#define I32_GE_U_OPCODE   0x4F
#define I32_CLZ_OPCODE    0x67 //not implemented
#define I32_CTZ_OPCODE    0x68 //not implemented
#define I32_POPCNT_OPCODE 0x69 //not implemented
#define I32_ADD_OPCODE    0x6A
#define I32_SUB_OPCODE    0x6B
#define I32_MUL_OPCODE    0x6C
#define I32_DIV_S_UPCODE  0x6D
#define I32_DIV_U_OPCODE  0x6E
#define I32_REM_S_OPCODE  0x6F
#define I32_REM_U_OPCODE  0x70
#define I32_AND_OPCODE    0x71
#define I32_OR_OPCODE     0x72
#define I32_XOR_OPCODE    0x73
#define I32_SHL_OPCODE    0x74
#define I32_SHR_S_OPCODE  0x75
#define I32_SHR_U_OPCODE  0x76
#define I32_ROTL_OPCODE   0x77
#define I32_ROTR_OPCODE   0x78


#define FUNCTION_START_CODE 0x60
#define END_CODE            0x0B
#define BLOCK_EMPTY_TYPE    0x40

#define ONLY_MIN_MEMORY_LIMIT 0x00
#define BOTH_MIN_AND_MAX_MEMORY_LIMIT 0x01
#define UNLIMITED_MAX_PAGE_NUM 0

#define BLOCK_TYPE_NONE 0x40
#define BLOCK_TYPE_I32 I32_VALTYPE
#define BLOCK_TYPE_I64 I64_VALTYPE
#define BLOCK_TYPE_F32 F32_VALTYPE 
#define BLOCK_TYPE_F64 F64_VALTYPE 

void read_u8(read_struct_t *r, unsigned char *out);
void read_u32(read_struct_t *r, uint32_t *out);
unsigned int readULEB128_u32(read_struct_t *r, uint32_t *out);
unsigned int readILEB128_i32(read_struct_t *r, int32_t *out);
wasm_module_t* parse(unsigned char *start, unsigned int len);
void free_wasm_module(wasm_module_t *m);
void panic(void);
void *calloc_or_panic(size_t nmemb, size_t size);
void *malloc_or_panic(size_t size);
void compile(wasm_module_t *m, FILE *out);

#endif
