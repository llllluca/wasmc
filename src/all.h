#ifndef ALL_H_INCLUDED
#define ALL_H_INCLUDED

#define ESP_HEAP_DEBUG 0

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include "dalist.h"
#include "libqbe.h"

typedef int err_t;
#define OK    0
#define FAIL -1

typedef uint8_t boolean;
#define TRUE 1
#define FALSE 0

typedef struct {
    /* It is needed at least min_page_num page of memory
     * in order to execute the WebAssembly module.
     * (WebAssembly currently defines a page to be 64KB) */
    uint32_t min_page_num;
    /* max_page_num is optional, 0 means unlimited. */
    uint32_t max_page_num;
    /* If min_page_num and max_page_num are both 0 the
     * WebAssembly module dosn't use memory */
} memory_info_t;

/* wasm_valtype enum occupies 1 byte */
typedef enum  __attribute__ ((__packed__)) wasm_valtype {
    NO_VALTYPE  = 0x00,
    I32_VALTYPE = 0x7F,
    I64_VALTYPE = 0x7E,
    F32_VALTYPE = 0x7D,
    F64_VALTYPE = 0x7C,
} wasm_valtype;

/* wasm_blocktype enum occupies 1 byte */
typedef enum  __attribute__ ((__packed__)) wasm_blocktype {
    BLOCK_TYPE_NONE = 0x40,
    BLOCK_TYPE_I32 = I32_VALTYPE,
    BLOCK_TYPE_I64 = I64_VALTYPE,
    BLOCK_TYPE_F32 = F32_VALTYPE,
    BLOCK_TYPE_F64 = F64_VALTYPE,
} wasm_blocktype;

typedef struct {
    unsigned char *start;
    unsigned char *offset;
    unsigned char *end;
    unsigned int len;
} read_struct_t;

typedef struct {
    wasm_valtype return_type;
    /* heap allocated array of length num_params. If a function has
     * no parameters params_type is NULL and num_params is 0. */
    wasm_valtype *params_type;
    uint32_t num_params;
} wasm_func_type_t;

typedef struct wasm_func_decl {
    wasm_func_type_t *type;
    char *name;
    boolean is_exported;
} wasm_func_decl;

typedef struct wasm_func_body {
    read_struct_t expr;
    uint32_t num_locals;
    wasm_valtype locals_type[];
} wasm_func_body;

typedef struct {
    wasm_valtype type;
    union {
        int32_t i32;
        float   f32;
        int64_t i64;
        double  f64;
    } as;
} const_expr_t;

#define GLOBAL_NAME_LEN 16
typedef struct {
    const_expr_t expr;
    boolean is_mutable;
    char *name[GLOBAL_NAME_LEN];
} global_t;

typedef struct {
    uint32_t offset;
    uint32_t init_len;
    unsigned char *init_bytes;
} data_segment_t;

typedef struct wasm_module {

    read_struct_t module;
    memory_info_t mem;

    /* Heap allocated array of length types_len. If a module has no functions
     * types in the type section, types is NULL and types_len is 0. */
    wasm_func_type_t *types;
    uint32_t types_len;

    uint32_t num_funcs;
    read_struct_t next_func_body;

    /* Heap allocated array of length num_funcs. If a module has no functions,
     * decls is NULL and num_funcs is 0. */
    wasm_func_decl *func_decls;

    /* Heap allocated array of length globals_len. If a module has no globals
     * in the global section, globals is NULL and globals_len is 0. */
    global_t *globals;
    uint32_t globals_len;

    /* Heap allocated array of length data_segments. If a module has no
     * data segment in the data section, data_segments is NULL and
     * num_data_segments is 0. */
    data_segment_t *data_segments;
    uint32_t num_data_segments;

} wasm_module;

enum stack_entry_kind {
    STACK_ENTRY_VALUE,
    STACK_ENTRY_BLOCK_END,
};

typedef struct {
    Ref qbe_temp;
    wasm_valtype wasm_type;
} value_t;

typedef struct {
    unsigned int dummy;
} block_end_t;

union value_or_block_end {
    value_t value;
    block_end_t block_end;
};

typedef struct stack_entry_t {
    enum stack_entry_kind kind;
    union value_or_block_end as;
} stack_entry_t;

typedef enum label_kind {
    IF_LABEL,
    BLOCK_LABEL,
    LOOP_LABEL,
} label_kind;

typedef struct phi_arg {
    Blk *label;
    Ref result;
} phi_arg;

typedef struct label_t {
    dalist_t results;
    Blk *qbe_block;
    wasm_blocktype wasm_type;
} label_t;

typedef enum skip_flag {
    NONE,
    BR_FLAG,
    RETURN_FLAG
} skip_flag;

typedef struct {
    wasm_module *m;
    wasm_func_decl *wasm_func_decl;
    wasm_func_body *wasm_func_body;
    Fn *qbe_func;
    dalist_t *value_stack;
    dalist_t *label_stack;
    uint32_t locals_len;
    skip_flag skip_flag;
    Blk *curr_block;
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
#define LOCAL_TEE_OPCODE  0x22
#define GLOBAL_GET_OPCODE 0x23
#define GLOBAL_SET_OPCODE 0x24

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


wasm_func_body *parse_next_func_body(wasm_module *m);
wasm_module* parse(unsigned char *start, unsigned int len);
void free_wasm_module(wasm_module *m);
void panic(void);
void *xcalloc(size_t nmemb, size_t size);
void *xmalloc(size_t size);
void compile(wasm_module *m);


/* ssa.c */
void write_local(Blk *b, uint32_t index, Ref value);
Ref read_local(func_compile_ctx_t *ctx, Blk *b, uint32_t index);
void seal_block(func_compile_ctx_t *ctx, Blk *b);

/* utils.c */
void read_u8(read_struct_t *r, unsigned char *out);
void read_u32(read_struct_t *r, uint32_t *out);
unsigned int readULEB128_u32(read_struct_t *r, uint32_t *out);
unsigned int readILEB128_i32(read_struct_t *r, int32_t *out);
wasm_valtype local_type(func_compile_ctx_t *ctx, uint32_t index);
simple_type cast(wasm_valtype t);

#endif
