#ifndef WASM_H_INCLUDED
#define WASM_H_INCLUDED

#include <inttypes.h>
#include <stdbool.h>

typedef enum WASMErr_t {
    /* no error */
    WASM_OK = 0,
    /* generic error */
    WASM_ERR,
} WASMErr_t;

#define WASM_MAGIC_NUMBER 0x6d736100
#define WASM_VERSION 1

/* The WASMValtype need to occupy 1 byte because the param_types field of
 * WASMFuncType points to an array of value type directly into the WASM
 * module, in the WASM module each value type occupy 1 byte */
typedef enum __attribute__((__packed__)) WASMValtype {
    WASM_VALTYPE_F64 = 0x7C,
    WASM_VALTYPE_F32 = 0x7D,
    WASM_VALTYPE_I64 = 0x7E,
    WASM_VALTYPE_I32 = 0x7F,
} WASMValtype;

typedef struct WASMFuncType {
    uint32_t param_count;
    /* param_types points into the WASM module */
    WASMValtype *param_types;
    /* result_count can only be 0 or 1 */
    uint32_t result_count;
    /* result_type is valid if and only if result_count is 1 */
    WASMValtype result_type;
} WASMFuncType;

#define WASM_FUNC_NAME_LEN 16
#define WASM_FUNC_NAME_PREFIX "aot_func#"
typedef struct WASMFunction {
    char name[WASM_FUNC_NAME_LEN];
    WASMFuncType *type;
    uint32_t id;
    uint32_t typeidx;
    uint32_t local_count;
    /* locals points into the WASM module */
    uint8_t *locals;
    /* code_start points into the WASM module */
    uint8_t *code_start;
    /* code_end points into the WASM module */
    uint8_t *code_end;
} WASMFunction;

#define WASM_LIMIT_ONLY_MIN    0x00
#define WASM_LIMIT_MIN_AND_MAX 0x01

#define WASM_MEMORY_LIMIT_RANGE 65536
typedef struct WASMMemory {
    uint32_t flags;
    uint32_t num_bytes_per_page;
    uint32_t init_page_count;
    uint32_t max_page_count;
} WASMMemory;

#define TABLE_ELEMENT_TYPE_FUNCTION 0x70
#define WASM_TABLE_MAX_ENTRY_COUNT 4294967295
typedef struct WASMTable {
    uint8_t flags;
    uint8_t elemtype;
    uint32_t init_entry_count;
    uint32_t max_entry_count;
} WASMTable;

typedef struct WASMExport {
    uint32_t name_len;
    /* name points into the WASM module, name is a string but not '\0' terminated */
    uint8_t *name;
    enum {
        EXPORT_TYPE_FUNC,
        EXPORT_TYPE_TABLE,
        EXPORT_TYPE_MEM,
        EXPORT_TYPE_GLOBAL,
    } export_desc;
    uint32_t index;
} WASMExport;

typedef struct WASMGlobal {
    WASMValtype type;
    bool is_mutable;
    union {
        int32_t i32;
        float   f32;
        int64_t i64;
        double  f64;
    } as;
} WASMGlobal;

typedef struct WASMDataSegment {
    uint32_t offset;
    uint32_t len;
    /* bytes points into the WASM module */
    unsigned char *bytes;
} WASMDataSegment;

typedef struct WASMModule {

    /* Heap allocated array of length types_count. If a module has no functions
     * types in the type section, types is NULL and types_count is 0. */
    WASMFuncType *types;
    uint32_t type_count;

    /* Heap allocated array of length functions_count. If a module has no
     * functions in the function section, functions is NULL and functions_count
     * is 0. */
    uint32_t function_count;
    WASMFunction *functions;

    /* memories_count can only be 0 or 1 */
    uint32_t memories_count;
    /* memory is valid if and only if memories_count is 1 */
    WASMMemory memory;

    /* table_count can only be 0 or 1 */
    uint32_t table_count;
    /* table is valid if and only if table_count is 1 */
    WASMTable table;

    /* Heap allocated array of length global_count. If a module has no globals
     * in the globals section, globals is NULL and globals_count is 0. */
    uint32_t global_count;
    WASMGlobal *globals;

    /* Heap allocated array of length export_count. If a module has no exports
     * in the export section, exports is NULL and exports_count is 0. */
    uint32_t export_count;
    WASMExport *exports;

    /* Heap allocated array of length data_seg_count. If a module has no
     * data segment in the data section, data_segments is NULL and
     * data_seg_count is 0. */
    uint32_t data_seg_count;
    WASMDataSegment *data_segments;

} WASMModule;

typedef enum WASMOpcode {
    /* Control instructions */
    WASM_OPCODE_UNREACHABLE   = 0x00,
    WASM_OPCODE_NOP           = 0x01,
    WASM_OPCODE_BLOCK         = 0x02,
    WASM_OPCODE_LOOP          = 0x03,
    WASM_OPCODE_IF            = 0x04,
    WASM_OPCODE_ELSE          = 0x05,
    WASM_OPCODE_END           = 0x0B,
    WASM_OPCODE_BRANCH        = 0x0C,
    WASM_OPCODE_BRANCH_IF     = 0x0D,
    WASM_OPCODE_BRANCH_TABLE  = 0x0E,
    WASM_OPCODE_RETURN        = 0x0F,
    WASM_OPCODE_CALL          = 0x10,
    WASM_OPCODE_CALL_INDIRECT = 0x11,

    /* Parametric instruction */
    WASM_OPCODE_DROP   = 0x1A,
    WASM_OPCODE_SELECT = 0x1B,

    /* Variable instructions */
    WASM_OPCODE_LOCAL_GET  = 0x20,
    WASM_OPCODE_LOCAL_SET  = 0x21,
    WASM_OPCODE_LOCAL_TEE  = 0x22,
    WASM_OPCODE_GLOBAL_GET = 0x23,
    WASM_OPCODE_GLOBAL_SET = 0x24,

    /* Memory instructions */
    WASM_OPCODE_I32_LOAD     = 0x28,
    WASM_OPCODE_I64_LOAD     = 0x29,
    WASM_OPCODE_F32_LOAD     = 0x2A,
    WASM_OPCODE_F64_LOAD     = 0x2B,
    WASM_OPCODE_I32_LOAD8_S  = 0x2C,
    WASM_OPCODE_I32_LOAD8_U  = 0x2D,
    WASM_OPCODE_I32_LOAD16_S = 0x2E,
    WASM_OPCODE_I32_LOAD16_U = 0x2F,
    WASM_OPCODE_I64_LOAD8_S  = 0x30,
    WASM_OPCODE_I64_LOAD8_U  = 0x31,
    WASM_OPCODE_I64_LOAD16_S = 0x32,
    WASM_OPCODE_I64_LOAD16_U = 0x33,
    WASM_OPCODE_I64_LOAD32_S = 0x34,
    WASM_OPCODE_I64_LOAD32_U = 0x35,
    WASM_OPCODE_I32_STORE    = 0x36,
    WASM_OPCODE_I64_STORE    = 0x37,
    WASM_OPCODE_F32_STORE    = 0x38,
    WASM_OPCODE_F64_STORE    = 0x39,
    WASM_OPCODE_I32_STORE8   = 0x3A,
    WASM_OPCODE_I32_STORE16  = 0x3B,
    WASM_OPCODE_I64_STORE8   = 0x3C,
    WASM_OPCODE_I64_STORE16  = 0x3D,
    WASM_OPCODE_I64_STORE32  = 0x3E,
    WASM_OPCODE_MEMORY_SIZE  = 0x3F,
    WASM_OPCODE_MEMORY_GROW  = 0x40,

    /* Numeric instruction */
    WASM_OPCODE_I32_CONST  = 0x41,
    WASM_OPCODE_I64_CONST  = 0x42,
    WASM_OPCODE_F32_CONST  = 0x43,
    WASM_OPCODE_F64_CONST  = 0x44,
    WASM_OPCODE_I32_EQZ    = 0x45,
    WASM_OPCODE_I32_EQ     = 0x46,
    WASM_OPCODE_I32_NE     = 0x47,
    WASM_OPCODE_I32_LT_S   = 0x48,
    WASM_OPCODE_I32_LT_U   = 0x49,
    WASM_OPCODE_I32_GT_S   = 0x4A,
    WASM_OPCODE_I32_GT_U   = 0x4B,
    WASM_OPCODE_I32_LE_S   = 0x4C,
    WASM_OPCODE_I32_LE_U   = 0x4D,
    WASM_OPCODE_I32_GE_S   = 0x4E,
    WASM_OPCODE_I32_GE_U   = 0x4F,
    WASM_OPCODE_I32_CLZ    = 0x67,
    WASM_OPCODE_I32_CTZ    = 0x68,
    WASM_OPCODE_I32_POPCNT = 0x69,
    WASM_OPCODE_I32_ADD    = 0x6A,
    WASM_OPCODE_I32_SUB    = 0x6B,
    WASM_OPCODE_I32_MUL    = 0x6C,
    WASM_OPCODE_I32_DIV_S  = 0x6D,
    WASM_OPCODE_I32_DIV_U  = 0x6E,
    WASM_OPCODE_I32_REM_S  = 0x6F,
    WASM_OPCODE_I32_REM_U  = 0x70,
    WASM_OPCODE_I32_AND    = 0x71,
    WASM_OPCODE_I32_OR     = 0x72,
    WASM_OPCODE_I32_XOR    = 0x73,
    WASM_OPCODE_I32_SHL    = 0x74,
    WASM_OPCODE_I32_SHR_S  = 0x75,
    WASM_OPCODE_I32_SHR_U  = 0x76,
    WASM_OPCODE_I32_ROTL   = 0x77,
    WASM_OPCODE_I32_ROTR   = 0x78,
} WASMOpcode;

/* wasm_blocktype enum occupies 1 byte */
typedef enum  __attribute__ ((__packed__)) WASMBlocktype {
    WASM_BLOCKTYPE_NONE = 0x40,
    WASM_BLOCKTYPE_I32 = WASM_VALTYPE_I32,
    WASM_BLOCKTYPE_I64 = WASM_VALTYPE_I64,
    WASM_BLOCKTYPE_F32 = WASM_VALTYPE_F32,
    WASM_BLOCKTYPE_F64 = WASM_VALTYPE_F64,
} WASMBlocktype;

#define IS_WASM_BLOCK_TYPE(t)                                  \
    ((t) == WASM_BLOCKTYPE_NONE ||                             \
     (t) == WASM_BLOCKTYPE_I32 || (t) == WASM_BLOCKTYPE_I64 || \
     (t) == WASM_BLOCKTYPE_F32 || (t) == WASM_BLOCKTYPE_F64)

WASMErr_t read_u32(uint8_t **buf, uint8_t *buf_end, uint32_t *out);
WASMErr_t read_u8(uint8_t **buf, uint8_t *buf_end, uint8_t *out);
WASMErr_t readULEB128_u32(uint8_t **buf, uint8_t *buf_end, uint32_t *out);
WASMErr_t readILEB128_i32(uint8_t **buf, uint8_t *buf_end, int32_t *out);
WASMErr_t wasm_decode(WASMModule *m, uint8_t *start, uint32_t len);
WASMValtype wasm_valtype_of(WASMFunction *f, uint32_t localidx);
void wasm_free(WASMModule *m);

#endif
