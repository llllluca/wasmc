#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "wasm.h"
#include "wasmc.h"

typedef enum __attribute__((__packed__)) WASMSectionType {
    WASM_SECTION_CUSTOM   = 0,
    WASM_SECTION_TYPE     = 1,
    WASM_SECTION_IMPORT   = 2,
    WASM_SECTION_FUNCTION = 3,
    WASM_SECTION_TABLE    = 4,
    WASM_SECTION_MEMORY   = 5,
    WASM_SECTION_GLOBAL   = 6,
    WASM_SECTION_EXPORT   = 7,
    WASM_SECTION_START    = 8,
    WASM_SECTION_ELEMENT  = 9,
    WASM_SECTION_CODE     = 10,
    WASM_SECTION_DATA     = 11,

    WASM_SECTIONS_COUNT,
} WASMSectionType;

typedef struct WASMSection {
    struct WASMSection *next;
    WASMSectionType type;
    uint8_t *start;
    uint8_t *end;
} WASMSection;

typedef enum __attribute__((__packed__)) OperandType {
    OPERAND_TYPE_UNKNOWN = 0,
    OPERAND_TYPE_F64 = WASM_VALTYPE_F64,
    OPERAND_TYPE_F32 = WASM_VALTYPE_F32,
    OPERAND_TYPE_I64 = WASM_VALTYPE_I64,
    OPERAND_TYPE_I32 = WASM_VALTYPE_I32,
} OperandType;

typedef struct OperandStackEntry {
    struct OperandStackEntry *next;
    OperandType type;
} OperandStackEntry;

typedef struct ControlStackEntry {
    struct ControlStackEntry *next;
    WASMBlocktype label_type;
    WASMBlocktype end_type;
    uint32_t height;
    bool unreachable;
} ControlStackEntry;

typedef struct ValidateCtx {
    WASMModule *m;
    WASMFunction *f;
    WASMFuncType *t;
    OperandStackEntry *opd_stack;
    uint32_t opd_size;
    ControlStackEntry *ctrl_stack;
    uint32_t ctrl_size;
    uint8_t *offset;
    uint8_t *code_end;
} ValidateCtx;

#define WASMC_OK 0

#define READ_TEMPLATE(buf, buf_end, out, type)      \
    do {                                            \
        uint8_t *p = *(buf);                        \
        uint8_t *new = p + sizeof(type);            \
        if (p > (buf_end)) {                        \
            return WASMC_ERR_MALFORMED_WASM_MODULE; \
        }                                           \
        *out = *((type *)p);                        \
        *(buf) = new;                               \
    } while (0)


int read_u32(uint8_t **buf, uint8_t *buf_end, uint32_t *out) {
    READ_TEMPLATE(buf, buf_end, out, uint32_t);
    return WASMC_OK;
}

int read_u8(uint8_t **buf, uint8_t *buf_end, uint8_t *out) {
    READ_TEMPLATE(buf, buf_end, out, uint8_t);
    return WASMC_OK;
}

/* Unsigned Litte Endian Base 128: https://en.wikipedia.org/wiki/LEB128
 * read a uint32_t number encoded in the ULED128 format. ULED128 is a
 * variable length encoding format, so the actual number of readed bytes
 * may be less that 4. */
int readULEB128_u32(uint8_t **buf, uint8_t *buf_end, uint32_t *out) {
    uint32_t result = 0;
    uint32_t shift = 0;
    unsigned char byte;
    unsigned int count = 0;
    do {
        READ_TEMPLATE(buf, buf_end, &byte, uint8_t);
        result |= (byte & 0x7f) << shift; /* low-order 7 bits of value */
        shift += 7;
        count++;
    } while ((byte & 0x80) != 0); /* get high-order bit of byte */

    *out = result;
    return WASMC_OK;
}

/* Signed Litte Endian Base 128: https://en.wikipedia.org/wiki/LEB128
 * read a int32_t number encoded in the ULED128 format. ULED128 is a
 * variable length encoding format, so the actual number of readed bytes
 * may be less that 4. */
#pragma GCC diagnostic ignored "-Wshift-negative-value"
int readILEB128_i32(uint8_t **buf, uint8_t *buf_end, int32_t *out) {
    int32_t result = 0;
    uint32_t shift = 0;
    /* the size in bits of the result variable, e.g., 32 if result's type is int32_t */
    uint32_t size = 32;
    unsigned char byte;
    unsigned int count = 0;
    do {
        READ_TEMPLATE(buf, buf_end, &byte, uint8_t);
        result |= (byte & 0x7f) << shift; /* low-order 7 bits of value */
        shift += 7;
        count++;
    } while ((byte & 0x80) != 0); /* get high-order bit of byte */

    /* sign bit of byte is second high-order bit (0x40) */
    if ((shift < size) && ((byte & 0x40) != 0))
      /* sign extend */
    result |= (~0 << shift);
    *out = result;
    return WASMC_OK;
}

static int parse_type_section(WASMModule *m, WASMSection *s) {
    /* The type sectoin decodes into a vector of function types */
    int err;
    uint8_t *offset = s->start;
    uint32_t type_count;
    err = readULEB128_u32(&offset, s->end, &type_count);
    if (err) return err;
    if (type_count == 0) return WASMC_OK;
    m->types = calloc(type_count, sizeof(struct WASMFuncType));
    if (m->types == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    m->type_count = type_count;

    for (uint32_t i = 0; i < type_count; i++) {
        /* Each function type start with the byte 0x60 */
        if (*offset++ != 0x60) {
            return WASMC_ERR_MALFORMED_WASM_MODULE;
        }

        /* Read the length of the vector of parameter types */
        uint32_t param_count;
        err = readULEB128_u32(&offset, s->end, &param_count);
        if (err) return err;
        m->types[i].param_count = param_count;
        m->types[i].param_types = NULL;
        if (param_count > 0) {
            m->types[i].param_types = offset;
        }
        /* Check that the param types are correct value type */
        for (uint32_t i = 0; i < param_count; i++) {
            uint8_t t = *offset++;
            if (t != WASM_VALTYPE_I32 && t != WASM_VALTYPE_I64 &&
                t != WASM_VALTYPE_F32 && t != WASM_VALTYPE_F64) {
                return WASMC_ERR_MALFORMED_WASM_MODULE;
            }
        }

        /* Read the length of the vector of result types */
        uint32_t result_count;
        err = readULEB128_u32(&offset, s->end, &result_count);
        if (err) return err;
        if (result_count > 1) {
            return WASMC_ERR_MALFORMED_WASM_MODULE;
        }
        m->types[i].result_count = result_count;
        m->types[i].result_type = 0;
        if (result_count > 0) {
            uint8_t t = *offset++;
            /* Check that the result type is correct value type */
            if (t != WASM_VALTYPE_I32 && t != WASM_VALTYPE_I64 &&
                t != WASM_VALTYPE_F32 && t != WASM_VALTYPE_F64) {
                return WASMC_ERR_MALFORMED_WASM_MODULE;
            }
            m->types[i].result_type = t;
        }
    }

    /* Check the section size */
    if (offset != s->end) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    return WASMC_OK;
}

static int parse_function_section(WASMModule *m, WASMSection *s) {
    /* Function section decodes into a vector of type indices that represent
     * the type fields of the functions in the type section. The locals and
     * body fields of the respective functions are encoded separately in the
     * code section */
    int err;
    uint8_t *offset = s->start;
    uint32_t function_count;
    err = readULEB128_u32(&offset, s->end, &function_count);
    if (err) return err;
    if (function_count == 0) return WASMC_OK;
    m->function_count = function_count;
    m->functions = calloc(function_count, sizeof(struct WASMFunction));
    if (m->functions == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;

    for (uint32_t i = 0; i < function_count; i++) {
        uint32_t type_index;
        err = readULEB128_u32(&offset, s->end, &type_index);
        if (err) return err;
        if (type_index >= m->type_count) {
            return WASMC_ERR_MALFORMED_WASM_MODULE;
        }
        m->functions[i].type = &m->types[type_index];
        m->functions[i].typeidx = type_index;
        m->functions[i].id = i;
    }

    /* Check the section size */
    if (offset != s->end) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    return WASMC_OK;
}

static int parse_table_section(WASMModule *m, WASMSection *s) {

    int err;
    uint8_t *offset = s->start;
    uint32_t table_count;
    err = readULEB128_u32(&offset, s->end, &table_count);
    if (err) return err;
    if (table_count > 1) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    m->table_count = table_count;
    uint8_t elemtype;
    err = read_u8(&offset, s->end, &elemtype);
    if (err) return err;
    if (elemtype != TABLE_ELEMENT_TYPE_FUNCTION) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    uint8_t flag;
    err = read_u8(&offset, s->end, &flag);
    if (err) return err;
    switch (flag) {
    case WASM_LIMIT_ONLY_MIN:
        err = readULEB128_u32(&offset, s->end, &m->table.init_entry_count);
        if (err) return err;
        m->table.max_entry_count = WASM_TABLE_MAX_ENTRY_COUNT;
        break;
    case WASM_LIMIT_MIN_AND_MAX:
        err = readULEB128_u32(&offset, s->end, &m->table.init_entry_count);
        if (err) return err;
        err = readULEB128_u32(&offset, s->end, &m->table.max_entry_count);
        if (err) return err;
        break;
    default:
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    if (m->table.init_entry_count > m->table.max_entry_count) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    /* Check the section size */
    if (offset != s->end) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    return WASMC_OK;
}

static int parse_memory_section(WASMModule *m, WASMSection *s) {

    int err;
    uint8_t *offset = s->start;
    uint32_t memory_count;
    err = readULEB128_u32(&offset, s->end, &memory_count);
    if (err) return err;
    if (memory_count > 1) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    m->memories_count = memory_count;
    uint8_t flag;
    err = read_u8(&offset, s->end, &flag);
    if (err) return err;
    switch (flag) {
    case WASM_LIMIT_ONLY_MIN:
        err = readULEB128_u32(&offset, s->end, &m->memory.init_page_count);
        if (err) return err;
        m->memory.max_page_count = WASM_MEMORY_LIMIT_RANGE;
        break;
    case WASM_LIMIT_MIN_AND_MAX:
        err = readULEB128_u32(&offset, s->end, &m->memory.init_page_count);
        if (err) return err;
        err = readULEB128_u32(&offset, s->end, &m->memory.max_page_count);
        if (err) return err;
        break;
    default:
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    m->memory.num_bytes_per_page = 64 * 1024;

    if (m->memory.init_page_count > WASM_MEMORY_LIMIT_RANGE) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    if (m->memory.max_page_count > WASM_MEMORY_LIMIT_RANGE) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    if (m->memory.init_page_count > m->memory.max_page_count) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    /* Check the section size */
    if (offset != s->end) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    return WASMC_OK;
}

static int parse_global_section(WASMModule *m, WASMSection *s) {
    /* The global section decodes into a vector of globals */
    int err;
    uint8_t *offset = s->start;
    uint32_t global_count;
    err = readULEB128_u32(&offset, s->end, &global_count);
    if (err) return err;
    if (global_count == 0) return WASMC_OK;
    m->globals = calloc(global_count, sizeof(struct WASMGlobal));
    if (m->globals == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    m->global_count = global_count;

    for (uint32_t i = 0; i < global_count; i++) {
        WASMGlobal *g = &m->globals[i];
        WASMValtype t = *offset++;
        if (t != WASM_VALTYPE_I32 && t != WASM_VALTYPE_I64 &&
            t != WASM_VALTYPE_F32 && t != WASM_VALTYPE_F64) {
                return WASMC_ERR_MALFORMED_WASM_MODULE;
        }
        g->type = t;
        err = read_u8(&offset, s->end, (uint8_t *)&g->is_mutable);
        if (err) return err;
        if (g->is_mutable != 0 && g->is_mutable != 1) {
            return WASMC_ERR_MALFORMED_WASM_MODULE;
        }
        uint8_t opcode;
        uint8_t type;
        err = read_u8(&offset, s->end, &opcode);
        if (err) return err;
        switch (opcode) {
        case WASM_OPCODE_I32_CONST:
            err = readILEB128_i32(&offset, s->end, &g->as.i32);
            if (err) return err;
            type = WASM_VALTYPE_I32;
            break;
        case WASM_OPCODE_I64_CONST:
            /* not implemented */
            assert(0);
        case WASM_OPCODE_F32_CONST:
            /* not implemented */
            assert(0);
        case WASM_OPCODE_F64_CONST:
            /* not implemented */
            assert(0);
        case WASM_OPCODE_GLOBAL_GET:
            /* not implemented */
            assert(0);
        default:
            return WASMC_ERR_MALFORMED_WASM_MODULE;
        }
        /* Each const expression end with the byte 0x0B */
        if (*offset++ != WASM_OPCODE_END) {
            return WASMC_ERR_MALFORMED_WASM_MODULE;
        }
        if (g->type != type) {
            return WASMC_ERR_MALFORMED_WASM_MODULE;
        }
    }

    /* Check the section size */
    if (offset != s->end) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    return WASMC_OK;
}

static int parse_export_section(WASMModule *m, WASMSection *s) {
    /* The export section decodes into a vector of exports */
    int err;
    uint8_t *offset = s->start;
    uint32_t export_count;
    err = readULEB128_u32(&offset, s->end, &export_count);
    if (err) return err;
    if (export_count == 0) return WASMC_OK;
    m->export_count = export_count;
    m->exports = calloc(export_count, sizeof(struct WASMExport));
    if (m->exports == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;

    for (uint32_t i = 0; i < export_count; i++) {
        uint32_t name_len;
        err = readULEB128_u32(&offset, s->end, &name_len);
        if (err) return err;
        uint8_t *name = offset;
        offset += name_len;
        unsigned char export_desc;
        err = read_u8(&offset, s->end, &export_desc);
        if (err) return err;
        uint32_t index;
        err = readULEB128_u32(&offset, s->end, &index);
        if (err) return err;
        switch (export_desc) {
        case EXPORT_TYPE_FUNC:
            if (index >= m->function_count) {
                return WASMC_ERR_MALFORMED_WASM_MODULE;;
            }
            break;
        case EXPORT_TYPE_TABLE:
            /* Tables are not implemented */
            break;
        case EXPORT_TYPE_MEM:
            if (index >= m->memories_count) {
                return WASMC_ERR_MALFORMED_WASM_MODULE;
            }
            break;
        case EXPORT_TYPE_GLOBAL:
            if (index >= m->global_count) {
                return WASMC_ERR_MALFORMED_WASM_MODULE;;
            }
            break;
        default:
            return WASMC_ERR_MALFORMED_WASM_MODULE;;
        }
        m->exports[i].name_len = name_len;
        m->exports[i].name = name;
        m->exports[i].export_desc = export_desc;
        m->exports[i].index = index;
    }

    /* Check the section size */
    if (offset != s->end) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;;
    }

    return WASMC_OK;
}

static int parse_code_section(WASMModule *m, WASMSection *s) {
    /* The code section decodes into a vector of code entries that are pairs of
     * value type vectors and expressions. They represent the locals and body
     * field of the functions. The type fields of the respective functions are
     * encoded separately in the function section. */
    int err;
    uint8_t *offset = s->start;
    uint32_t code_entry_count;
    err = readULEB128_u32(&offset, s->end, &code_entry_count);
    if (err) return err;
    if (code_entry_count != m->function_count) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    for (uint32_t i = 0; i < code_entry_count; i++) {
        uint32_t code_entry_len;
        err = readULEB128_u32(&offset, s->end, &code_entry_len);
        if (err) return err;
        uint8_t *code_entry_start = offset;
        uint32_t local_declaration_count;
        err = readULEB128_u32(&offset, s->end, &local_declaration_count);
        if (err) return err;
        m->functions[i].locals = local_declaration_count > 0 ? offset : NULL;
        uint32_t sum = 0;
        /* Local declarations are compressed into a vector whose entries consist of
         * a u32 count and a value type denoting count locals of the same value type */
        for (uint32_t j = 0; j < local_declaration_count; j++) {
            uint32_t n;
            err = readULEB128_u32(&offset, s->end, &n);
            if (err) return err;
            sum += n;
            uint8_t t = *offset++;
            if (t != WASM_VALTYPE_I32 && t != WASM_VALTYPE_I64 &&
                t != WASM_VALTYPE_F32 && t != WASM_VALTYPE_F64) {
                return WASMC_ERR_MALFORMED_WASM_MODULE;
            }
        }
        m->functions[i].local_count = sum;

        m->functions[i].code_start = offset;
        uint32_t len = code_entry_len - (uint32_t)(offset - code_entry_start);
        /* Each expression end with the byte 0x0B */
        if (offset[len-1] != WASM_OPCODE_END) {
                return WASMC_ERR_MALFORMED_WASM_MODULE;
        }
        m->functions[i].code_end = &offset[len-1];
        offset += len; 
    }

    /* Check the section size */
    if (offset != s->end) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    return WASMC_OK;
}

static int parse_data_section(WASMModule *m, WASMSection *s) {
    /* The data section decodes into a vector of data segments */
    int err;
    uint8_t *offset = s->start;
    uint32_t data_seg_count;
    err = readULEB128_u32(&offset, s->end, &data_seg_count);
    if (err) return err;
    if (data_seg_count == 0) return WASMC_OK;
    m->data_seg_count = data_seg_count;
    m->data_segments = calloc(data_seg_count, sizeof(struct WASMDataSegment));
    if (m->data_segments == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    for (uint32_t i = 0; i < data_seg_count; i++) {
        uint32_t memidx;
        err = readULEB128_u32(&offset, s->end, &memidx);
        if (err) return err;
        if (memidx != 0) {
            return WASMC_ERR_MALFORMED_WASM_MODULE;
        }
        WASMDataSegment *d = &m->data_segments[i];

        uint8_t opcode;
        err = read_u8(&offset, s->end, &opcode);
        if (err) return err;
        switch (opcode) {
        case WASM_OPCODE_I32_CONST: {
            int32_t const_expr;
            err = readILEB128_i32(&offset, s->end, &const_expr);
            if (err) return err;
            if (const_expr < 0) return WASMC_ERR_MALFORMED_WASM_MODULE;
            d->offset = (uint32_t) const_expr;
        } break;
        case WASM_OPCODE_GLOBAL_GET:
            /* not implemented */
            assert(0);
        default:
            return WASMC_ERR_MALFORMED_WASM_MODULE;
        }
        /* Each const expression end with the byte 0x0B */
        if (*offset++ != WASM_OPCODE_END) {
            return WASMC_ERR_MALFORMED_WASM_MODULE;
        }

        err = readULEB128_u32(&offset, s->end, &d->len);
        if (err) return err;
        d->bytes = offset;
        offset += d->len;
    }

    /* Check the section size */
    if (offset != s->end) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    return WASMC_OK;
}

static void free_sections(WASMSection *sections_list) {
    WASMSection *s = sections_list;
    while (s != NULL) {
        WASMSection *next = s->next;
        free(s);
        s = next;
    }
}

static int push_opd(ValidateCtx *ctx, OperandType type) {
    OperandStackEntry *opd = malloc(sizeof(struct OperandStackEntry));
    if (opd == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    opd->type = type;
    opd->next = ctx->opd_stack;
    ctx->opd_stack = opd;
    ctx->opd_size++;
    return WASMC_OK;
}

static int pop_opd(ValidateCtx *ctx, OperandType *out) {
    ControlStackEntry *ctrl = ctx->ctrl_stack;
    assert(ctrl != NULL);
    if (ctx->opd_size == ctrl->height && ctrl->unreachable) {
        if (out != NULL) *out = OPERAND_TYPE_UNKNOWN;
        return WASMC_OK;
    }
    if (ctx->opd_size == ctrl->height) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    assert(ctx->opd_stack != NULL);
    OperandStackEntry *opd = ctx->opd_stack;
    if (out != NULL) *out = opd->type;
    ctx->opd_stack = opd->next;
    ctx->opd_size--;
    free(opd);

    return WASMC_OK;
}

static int pop_expect_opd(ValidateCtx *ctx, OperandType expect,
                                OperandType *out) {

    int err;
    OperandType actual;
    err = pop_opd(ctx, &actual);
    if (err) return err;
    if (actual == OPERAND_TYPE_UNKNOWN) {
        if (out != NULL) *out = expect;
        return WASMC_OK;
    }
    if (expect == OPERAND_TYPE_UNKNOWN) {
        if (out != NULL) *out = actual;
        return WASMC_OK;
    }
    if (actual != expect) return WASMC_ERR_MALFORMED_WASM_MODULE;
    if (out != NULL) *out = actual;
    return WASMC_OK;
}

static int push_ctrl(ValidateCtx *ctx, WASMBlocktype label,
                           WASMBlocktype end) {

    ControlStackEntry *ctrl = malloc(sizeof(struct ControlStackEntry));
    if (ctrl == NULL) return WASMC_ERR_OUT_OF_HEAP_MEMORY;
    ctrl->label_type = label;
    ctrl->end_type = end;
    ctrl->unreachable = false;
    ctrl->height = ctx->opd_size;
    ctrl->next = ctx->ctrl_stack;
    ctx->ctrl_stack = ctrl;
    ctx->ctrl_size++;
    return WASMC_OK;
}

static int pop_ctrl(ValidateCtx *ctx, WASMBlocktype *out) {
    int err;
    if (ctx->ctrl_stack == NULL) return WASMC_ERR_MALFORMED_WASM_MODULE;
    ControlStackEntry *top = ctx->ctrl_stack;
    if (top->end_type != WASM_BLOCKTYPE_NONE) {
        err = pop_expect_opd(ctx, (OperandType)top->end_type, NULL);
        if (err) return err;
    }
    if (ctx->opd_size != top->height) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    if (out != NULL) *out = top->end_type;
    ctx->ctrl_stack = top->next;
    ctx->ctrl_size--;
    free(top);
    return WASMC_OK;
}

static int unreachable(ValidateCtx *ctx) {
    int err;
    if (ctx->ctrl_stack == NULL) return WASMC_ERR_MALFORMED_WASM_MODULE;
    ControlStackEntry *top = ctx->ctrl_stack;
    while (ctx->opd_size != top->height) {
        err = pop_opd(ctx, NULL);
        if (err) return err;
    }
    top->unreachable = true;
    return WASMC_OK;
}

static ControlStackEntry *ctrl_get(ValidateCtx *ctx, uint32_t index) {
    if (index >= ctx->ctrl_size - 1) return NULL;
    ControlStackEntry *iter = ctx->ctrl_stack;
    for (uint32_t i = 0; i < index; i++) {
        iter = iter->next;
    }
    return iter;
}

static int validate_const(ValidateCtx *ctx, OperandType type) {
    int err;
    switch (type) {
    case WASM_VALTYPE_I32: {
        int32_t n;
        err = readILEB128_i32(&ctx->offset, ctx->code_end, &n);
        if (err) return err;
    } break;
    case WASM_VALTYPE_F32:
    case WASM_VALTYPE_I64:
    case WASM_VALTYPE_F64:
        /* not implemented */
        assert(0);
    default:
        assert(0);
    }
    err = push_opd(ctx, type);
    if (err) return err;
    return WASMC_OK;
}

static int validate_binop(ValidateCtx *ctx, OperandType type) {
    int err;
    err = pop_expect_opd(ctx, type, NULL);
    if (err) return err;
    err = pop_expect_opd(ctx, type, NULL);
    if (err) return err;
    err = push_opd(ctx, type);
    if (err) return err;
    return WASMC_OK;
}

static int validate_testop(ValidateCtx *ctx, OperandType type) {
    int err;
    err = pop_expect_opd(ctx, type, NULL);
    if (err) return err;
    err = push_opd(ctx, OPERAND_TYPE_I32);
    if (err) return err;
    return WASMC_OK;
}

static int validate_relop(ValidateCtx *ctx, OperandType type) {
    int err;
    err = pop_expect_opd(ctx, type, NULL);
    if (err) return err;
    err = pop_expect_opd(ctx, type, NULL);
    if (err) return err;
    err = push_opd(ctx, OPERAND_TYPE_I32);
    if (err) return err;
    return WASMC_OK;
}

static int validate_load(ValidateCtx *ctx, OperandType type,
                              uint32_t align_upper_limit) {
    int err;
    if (ctx->m->memories_count == 0) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    uint32_t align, offset;
    err = readULEB128_u32(&ctx->offset, ctx->code_end, &align);
    if (err) return err;
    err = readULEB128_u32(&ctx->offset, ctx->code_end, &offset);
    if (err) return err;

    if (align > align_upper_limit) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    err = pop_expect_opd(ctx, OPERAND_TYPE_I32, NULL);
    if (err) return err;
    err = push_opd(ctx, type);
    if (err) return err;
    return WASMC_OK;
}

static int validate_store(ValidateCtx *ctx, OperandType type,
                              uint32_t align_upper_limit) {
    int err;
    if (ctx->m->memories_count == 0) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    uint32_t align, offset;
    err = readULEB128_u32(&ctx->offset, ctx->code_end, &align);
    if (err) return err;
    err = readULEB128_u32(&ctx->offset, ctx->code_end, &offset);
    if (err) return err;

    if (align > align_upper_limit) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    err = pop_expect_opd(ctx, OPERAND_TYPE_I32, NULL);
    if (err) return err;
    err = pop_expect_opd(ctx, type, NULL);
    if (err) return err;
    return WASMC_OK;
}

static int validate_global_get(ValidateCtx *ctx) {
    int err;
    uint32_t globalidx;
    err = readULEB128_u32(&ctx->offset, ctx->code_end, &globalidx);
    if (err) return err;
    if (globalidx >= ctx->m->global_count) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    WASMValtype t = ctx->m->globals[globalidx].type;
    err = pop_expect_opd(ctx, (OperandType)t, NULL);
    if (err) return err;
    return WASMC_OK;
}

static int validate_global_set(ValidateCtx *ctx) {
    int err;
    uint32_t globalidx;
    err = readULEB128_u32(&ctx->offset, ctx->code_end, &globalidx);
    if (err) return err;
    if (globalidx >= ctx->m->global_count) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    WASMGlobal *g = &ctx->m->globals[globalidx];
    if (!g->is_mutable) return WASMC_ERR_MALFORMED_WASM_MODULE;

    WASMValtype t = ctx->m->globals[globalidx].type;
    err = push_opd(ctx, (OperandType)t);
    if (err) return err;
    return WASMC_OK;
}

static int validate_local_set(ValidateCtx *ctx) {
    int err;
    uint32_t localidx;
    err = readULEB128_u32(&ctx->offset, ctx->code_end, &localidx);
    if (err) return err;
    uint32_t n = ctx->f->local_count + ctx->t->param_count;
    if (localidx >= n) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    WASMValtype t = wasm_valtype_of(ctx->f, localidx);
    err = pop_expect_opd(ctx, (OperandType)t, NULL);
    if (err) return err;
    return WASMC_OK;
}

static int validate_local_tee(ValidateCtx *ctx) {
    int err;
    uint32_t localidx;
    err = readULEB128_u32(&ctx->offset, ctx->code_end, &localidx);
    if (err) return err;
    uint32_t n = ctx->f->local_count + ctx->t->param_count;
    if (localidx >= n) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    WASMValtype t = wasm_valtype_of(ctx->f, localidx);
    err = pop_expect_opd(ctx, (OperandType)t, NULL);
    if (err) return err;
    err = push_opd(ctx, (OperandType)t);
    if (err) return err;
    return WASMC_OK;
}

static int validate_local_get(ValidateCtx *ctx) {
    int err;
    uint32_t localidx;
    err = readULEB128_u32(&ctx->offset, ctx->code_end, &localidx);
    if (err) return err;
    uint32_t n = ctx->f->local_count + ctx->t->param_count;
    if (localidx >= n) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    WASMValtype valtype = wasm_valtype_of(ctx->f, localidx);
    err = push_opd(ctx, (OperandType)valtype);
    if (err) return err;
    return WASMC_OK;
}

static int validate_select(ValidateCtx *ctx) {
    int err;
    err =  pop_expect_opd(ctx, OPERAND_TYPE_I32, NULL);
    if (err) return err;
    OperandType t1, t2;
    err = pop_opd(ctx, &t1);
    if (err) return err;
    err = pop_expect_opd(ctx, t1, &t2);
    if (err) return err;
    err = push_opd(ctx, t2);
    if (err) return err;
    return WASMC_OK;
}

static int validate_call(ValidateCtx *ctx) {
    int err;
    uint32_t funcidx;
    err = readULEB128_u32(&ctx->offset, ctx->code_end, &funcidx);
    if (err) return err;
    if (funcidx >= ctx->m->function_count) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }

    WASMFunction *called_func = &ctx->m->functions[funcidx];
    WASMFuncType *t = called_func->type;
    for (uint32_t i = 0; i < t->param_count; i++) {
        WASMValtype valtype = t->param_types[t->param_count - i - 1];
        err = pop_expect_opd(ctx, (OperandType)valtype, NULL);
        if (err) return err;
    }
    if (t->result_count > 0) {
        err = push_opd(ctx, (OperandType)t->result_type);
        if (err) return err;
    }
    return WASMC_OK;
}

static int validate_return(ValidateCtx *ctx) {
    int err;
    if (ctx->t->result_count > 0) {
        WASMValtype t = ctx->t->result_type;
        err = pop_expect_opd(ctx, (OperandType)t, NULL);
        if (err) return err;
    }
    err = unreachable(ctx);
    if (err) return err;
    return WASMC_OK;
}

static int validate_br_if(ValidateCtx *ctx) {
    int err;
    uint32_t labelidx;
    err = readULEB128_u32(&ctx->offset, ctx->code_end, &labelidx);
    if (err) return err;
    ControlStackEntry *ctrl = ctrl_get(ctx, labelidx);
    if (ctrl == NULL) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    err = pop_expect_opd(ctx, OPERAND_TYPE_I32, NULL);
    if (err) return err;
    if (ctrl->label_type != WASM_BLOCKTYPE_NONE) {
        err = pop_expect_opd(ctx, (OperandType)ctrl->label_type, NULL);
        if (err) return err;
        err = push_opd(ctx, (OperandType)ctrl->label_type);
        if (err) return err;
    }
    return WASMC_OK;
}

static int validate_br(ValidateCtx *ctx) {
    int err;
    uint32_t labelidx;
    err = readULEB128_u32(&ctx->offset, ctx->code_end, &labelidx);
    if (err) return err;
    ControlStackEntry *ctrl = ctrl_get(ctx, labelidx);
    if (ctrl == NULL) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    if (ctrl->label_type != WASM_BLOCKTYPE_NONE) {
        err = pop_expect_opd(ctx, (OperandType)ctrl->label_type, NULL);
        if (err) return err;
    }
    err = unreachable(ctx);
    if (err) return err;
    return WASMC_OK;
}

static int validate_end(ValidateCtx *ctx) {
    int err;
    WASMBlocktype result;
    err = pop_ctrl(ctx, &result);
    if (err) return err;
    if (result != WASM_BLOCKTYPE_NONE) {
        err = push_opd(ctx, (OperandType)result);
        if (err) return err;
    }
    return WASMC_OK;
}

static int validate_else(ValidateCtx *ctx) {
    int err;
    WASMBlocktype result;
    err = pop_ctrl(ctx, &result);
    if (err) return err;
    err = push_ctrl(ctx, result, result);
    if (err) return err;
    return WASMC_OK;
}

static int validate_if(ValidateCtx *ctx) {
    int err;
    WASMBlocktype blktype;
    err = read_u8(&ctx->offset, ctx->code_end, &blktype);
    if (err) return err;
    if (!IS_WASM_BLOCK_TYPE(blktype)) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    err = pop_expect_opd(ctx, OPERAND_TYPE_I32, NULL);
    if (err) return err;
    err = push_ctrl(ctx, blktype, blktype);
    if (err) return err;
    return WASMC_OK;
}

static int validate_loop(ValidateCtx *ctx) {
    int err;
    WASMBlocktype blktype;
    err = read_u8(&ctx->offset, ctx->code_end, &blktype);
    if (err) return err;
    if (!IS_WASM_BLOCK_TYPE(blktype)) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    err = push_ctrl(ctx, WASM_BLOCKTYPE_NONE, blktype);
    if (err) return err;
    return WASMC_OK;
}

static int validate_block(ValidateCtx *ctx) {
    int err;
    WASMBlocktype blktype;
    err = read_u8(&ctx->offset, ctx->code_end, &blktype);
    if (err) return err;
    if (!IS_WASM_BLOCK_TYPE(blktype)) {
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
    err = push_ctrl(ctx, blktype, blktype);
    if (err) return err;
    return WASMC_OK;
}


static int validate_instr(ValidateCtx *ctx, uint8_t opcode) {

    switch (opcode) {
    /* Control instructions */
    case WASM_OPCODE_UNREACHABLE:
        return unreachable(ctx);
    case WASM_OPCODE_NOP:
        return WASMC_OK;
    case WASM_OPCODE_BLOCK:
        return validate_block(ctx);
    case WASM_OPCODE_LOOP:
        return validate_loop(ctx);
    case WASM_OPCODE_IF:
        return validate_if(ctx);
    case WASM_OPCODE_ELSE:
        return validate_else(ctx);
    case WASM_OPCODE_END:
        return validate_end(ctx);
    case WASM_OPCODE_BRANCH:
        return validate_br(ctx);
    case WASM_OPCODE_BRANCH_IF:
        return validate_br_if(ctx);
    case WASM_OPCODE_RETURN:
        return validate_return(ctx);
    case WASM_OPCODE_CALL:
        return validate_call(ctx);

    /* Parametric instruction */
    case WASM_OPCODE_DROP:
        return pop_opd(ctx, NULL);
    case WASM_OPCODE_SELECT:
        return validate_select(ctx);

    /* Variable instructions */
    case WASM_OPCODE_LOCAL_GET:
        return validate_local_get(ctx);
    case WASM_OPCODE_LOCAL_SET:
        return validate_local_set(ctx);
    case WASM_OPCODE_LOCAL_TEE:
        return validate_local_tee(ctx);
    case WASM_OPCODE_GLOBAL_GET:
        return validate_global_set(ctx);
    case WASM_OPCODE_GLOBAL_SET:
        return validate_global_get(ctx);

    /* Memory instructions */
    case WASM_OPCODE_I32_LOAD:
        return validate_load(ctx, OPERAND_TYPE_I32, 2);
    case WASM_OPCODE_I32_STORE:
        return validate_store(ctx, OPERAND_TYPE_I32, 2);
    case WASM_OPCODE_I32_LOAD8_S:
        return validate_load(ctx, OPERAND_TYPE_I32, 1);
    case WASM_OPCODE_I32_LOAD8_U:
        return validate_load(ctx, OPERAND_TYPE_I32, 1);
    case WASM_OPCODE_I32_STORE8:
        return validate_store(ctx, OPERAND_TYPE_I32, 1);

    /* Numeric instruction */
    case WASM_OPCODE_I32_CONST:
        return validate_const(ctx, OPERAND_TYPE_I32);
    case WASM_OPCODE_I64_CONST:
        return validate_const(ctx, OPERAND_TYPE_I64);
    case WASM_OPCODE_F32_CONST:
        return validate_const(ctx, OPERAND_TYPE_F32);
    case WASM_OPCODE_F64_CONST:
        return validate_const(ctx, OPERAND_TYPE_F64);
    case WASM_OPCODE_I32_EQZ:
        return validate_testop(ctx, OPERAND_TYPE_I32);

    case WASM_OPCODE_I32_EQ:
    case WASM_OPCODE_I32_NE:
    case WASM_OPCODE_I32_LT_S:
    case WASM_OPCODE_I32_LT_U:
    case WASM_OPCODE_I32_GT_S:
    case WASM_OPCODE_I32_GT_U:
    case WASM_OPCODE_I32_LE_S:
    case WASM_OPCODE_I32_LE_U:
    case WASM_OPCODE_I32_GE_S:
    case WASM_OPCODE_I32_GE_U:
        return validate_relop(ctx, OPERAND_TYPE_I32);

    case WASM_OPCODE_I32_ADD:
    case WASM_OPCODE_I32_SUB:
    case WASM_OPCODE_I32_MUL:
    case WASM_OPCODE_I32_DIV_S:
    case WASM_OPCODE_I32_DIV_U:
    case WASM_OPCODE_I32_REM_S:
    case WASM_OPCODE_I32_REM_U:
    case WASM_OPCODE_I32_AND:
    case WASM_OPCODE_I32_OR:
    case WASM_OPCODE_I32_XOR:
    case WASM_OPCODE_I32_SHL:
    case WASM_OPCODE_I32_SHR_S:
    case WASM_OPCODE_I32_SHR_U:
        return validate_binop(ctx, OPERAND_TYPE_I32);
    default:
        fprintf(stderr,
            "WASM validation error: 0x%x unknown opcode\n", opcode);
        return WASMC_ERR_MALFORMED_WASM_MODULE;
    }
}

static void validate_cleanup(ValidateCtx *ctx) {
    while (ctx->opd_size > 0) {
        pop_opd(ctx, NULL);
    }
    while (ctx->ctrl_size > 0) {
        pop_ctrl(ctx, NULL);
    }
}

static int validate_fn(WASMModule *m, uint32_t funcidx) {
    assert(funcidx < m->function_count);
    WASMFunction *f = &m->functions[funcidx];
    WASMFuncType *t = f->type;

    ValidateCtx ctx = {0};
    ctx.offset = f->code_start;
    ctx.code_end = f->code_end;
    ctx.m = m;
    ctx.f = f;
    ctx.t = t;

    int err;
    err = push_ctrl(&ctx, WASM_BLOCKTYPE_NONE, WASM_BLOCKTYPE_NONE);
    if (err) goto ERROR;

    uint8_t opcode;
    while (ctx.offset < ctx.code_end) {
        err =read_u8(&ctx.offset, ctx.code_end, &opcode);
        if (err) goto ERROR;
        err = validate_instr(&ctx, opcode);
        if (err) goto ERROR;
    }

    if (t->result_count > 0) {
        err = pop_expect_opd(&ctx, (OperandType)t->result_type, NULL);
        if (err) goto ERROR;
    }
    err = pop_ctrl(&ctx, NULL);
    if (err) goto ERROR;

    assert(ctx.opd_stack == NULL && ctx.opd_size == 0);
    assert(ctx.ctrl_stack == NULL && ctx.ctrl_size == 0);
    return WASMC_OK;

ERROR:
    validate_cleanup(&ctx);
    return err;
}

void wasm_free(WASMModule *m) {
    if (m->types != NULL) {
        free(m->types);
    }
    if (m->functions != NULL) {
        free(m->functions);
    }
    if (m->globals != NULL) {
        free(m->globals);
    }
    if (m->data_segments != NULL) {
        free(m->data_segments);
    }
    if (m->exports != NULL) {
        free(m->exports);
    }
}

int wasm_decode(WASMModule *m, uint8_t *start, uint32_t len) {
    assert(m != NULL && start != NULL);
    memset(m, 0, sizeof(struct WASMModule));

    WASMSection *sections_list = NULL;
    WASMSection *list_tail = NULL;

    int err;
    uint8_t *offset = start;
    uint8_t *end = start + len;
    uint32_t magic, version;
    err = read_u32(&offset, end, &magic);
    if (err) goto ERROR;
    err = read_u32(&offset, end, &version);
    if (err) goto ERROR;
    if (magic != WASM_MAGIC_NUMBER || version != WASM_VERSION){
        err = WASMC_ERR_MALFORMED_WASM_MODULE;
        goto ERROR;
    }

    /* Get the start and end of sections in the WASM module */
    uint8_t section_type;
    uint32_t section_size;
    while (offset < end) {
        err = read_u8(&offset, end, &section_type);
        if (err) goto ERROR;
        err = readULEB128_u32(&offset, end, &section_size);
        if (err) goto ERROR;
        WASMSection *s = malloc(sizeof(struct WASMSection));
        if (s == NULL) {
            err = WASMC_ERR_OUT_OF_HEAP_MEMORY;
            goto ERROR;
        }

        /* initialize the WASMSection struct */
        s->next = NULL;
        s->type = section_type;
        s->start = offset;
        s->end = s->start + section_size;

        /* append The WASMSection struct at the end of section_list */
        if (sections_list == NULL) {
            sections_list = s;
        } else {
            list_tail->next = s;
        }
        list_tail = s;

        /* skip the section body in the WASM module */
        offset += section_size;
    }

    /* Check that the sum of the section lengths
     * is equal to the length of the WASM module */
    if (offset != end) {
        err = WASMC_ERR_MALFORMED_WASM_MODULE;
        goto ERROR;
    }

    /* Check that the sections are in the right order */
    WASMSectionType prev = WASM_SECTION_CUSTOM;
    for (WASMSection *s = sections_list; s != NULL; s = s->next) {
        if (s->type == WASM_SECTION_CUSTOM) continue;
        if (s->type <= prev) {
            err = WASMC_ERR_MALFORMED_WASM_MODULE;
            goto ERROR;
        }
        prev = s->type;
    }

    /* Parse each section */
    for (WASMSection *s = sections_list; s != NULL; s = s->next) {
        switch (s->type) {
        case WASM_SECTION_CUSTOM:
            /* Custom section is not implemented */
            break;
        case WASM_SECTION_TYPE:
            err = parse_type_section(m, s);
            break;
        case WASM_SECTION_IMPORT:
            /* Import section is not implemented */
            break;
        case WASM_SECTION_FUNCTION:
            err = parse_function_section(m, s);
            break;
        case WASM_SECTION_TABLE:
            err = parse_table_section(m, s);
            break;
        case WASM_SECTION_MEMORY:
            err = parse_memory_section(m, s);
            break;
        case WASM_SECTION_GLOBAL:
            err =parse_global_section(m, s);
            break;
        case WASM_SECTION_EXPORT:
            err = parse_export_section(m, s);
            break;
        case WASM_SECTION_START:
            /* Start section is not implemented */
            break;
        case WASM_SECTION_ELEMENT:
            /* Element section is not implemented */
            break;
        case WASM_SECTION_CODE:
            err = parse_code_section(m, s);
            break;
        case WASM_SECTION_DATA:
            parse_data_section(m, s);
            break;
        default:
            err = WASMC_ERR_MALFORMED_WASM_MODULE;
        }
        if (err != WASMC_OK) goto ERROR;
    }

    free_sections(sections_list);
    sections_list = NULL;

    for (uint32_t i = 0; i < m->function_count; i++) {
        err = validate_fn(m, i);
        if (err) goto ERROR;
    }

    /* Function internal name */
    for (uint32_t i = 0; i < m->function_count; i++) {
        int n = snprintf(
            m->functions[i].name,
            WASM_FUNC_NAME_LEN,
            WASM_FUNC_NAME_PREFIX"%"PRIu32, i);
        if (n >= WASM_FUNC_NAME_LEN) {
            goto ERROR;
        }
    }

    return WASMC_OK;

ERROR:
    free_sections(sections_list);
    wasm_free(m);
    return err;

}

WASMValtype wasm_valtype_of(WASMFunction *f, uint32_t localidx) {
    WASMFuncType *t = f->type;
    assert(localidx < t->param_count + f->local_count);
    if (localidx < t->param_count) {
        return t->param_types[localidx];
    } else {
        uint8_t *offset = f->locals;
        uint32_t sum = t->param_count;
        WASMValtype t;
        do {
            uint32_t n = 0;
            /* this call to readULEB128_u32 can't fail because in the function
             * parse_code_section had already checked that the local declaration part
             * of this function in the WASM module is well formed */
            readULEB128_u32(&offset, f->code_start, &n);
            t = *offset++;
            sum += n;
        } while (sum < localidx);
        return t;
    }
}

