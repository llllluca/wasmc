#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "wasm.h"

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

#define ERR_CHECK(x) if (x) return WASM_ERR

#define READ_TEMPLATE(buf, buf_end, out, type) \
    do {                                       \
        uint8_t *p = *(buf);                   \
        uint8_t *new = p + sizeof(type);       \
        if (p > (buf_end)) {                   \
            return WASM_ERR;                   \
        }                                      \
        *out = *((type *)p);                   \
        *(buf) = new;                          \
    } while (0)


WASMErr_t read_u32(uint8_t **buf, uint8_t *buf_end, uint32_t *out) {
    READ_TEMPLATE(buf, buf_end, out, uint32_t);
    return WASM_OK;
}

WASMErr_t read_u8(uint8_t **buf, uint8_t *buf_end, uint8_t *out) {
    READ_TEMPLATE(buf, buf_end, out, uint8_t);
    return WASM_OK;
}

/* Unsigned Litte Endian Base 128: https://en.wikipedia.org/wiki/LEB128
 * read a uint32_t number encoded in the ULED128 format. ULED128 is a
 * variable length encoding format, so the actual number of readed bytes
 * may be less that 4. */
WASMErr_t readULEB128_u32(uint8_t **buf, uint8_t *buf_end, uint32_t *out) {
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
    return WASM_OK;
}

/* Signed Litte Endian Base 128: https://en.wikipedia.org/wiki/LEB128
 * read a int32_t number encoded in the ULED128 format. ULED128 is a
 * variable length encoding format, so the actual number of readed bytes
 * may be less that 4. */
#pragma GCC diagnostic ignored "-Wshift-negative-value"
WASMErr_t readILEB128_i32(uint8_t **buf, uint8_t *buf_end, int32_t *out) {
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
    return WASM_OK;
}

#define xread_u8(buf, buf_end, out) \
    if (read_u8(buf, buf_end, out)) return WASM_ERR

#define xread_u32(buf, buf_end, out) \
    if (read_u32(buf, buf_end, out)) return WASM_ERR

#define xreadULEB128_u32(buf, buf_end, out) \
    if (readULEB128_u32((buf), (buf_end), (out)) != WASM_OK) return WASM_ERR

#define xreadILEB128_i32(buf, buf_end, out) \
    if (readILEB128_i32((buf), (buf_end), (out)) != WASM_OK) return WASM_ERR

static WASMErr_t parse_type_section(WASMModule *m, WASMSection *s) {
    /* The type sectoin decodes into a vector of function types */
    uint8_t *offset = s->start;
    uint32_t type_count;
    xreadULEB128_u32(&offset, s->end, &type_count);
    if (type_count == 0) return WASM_OK;
    m->types = calloc(type_count, sizeof(struct WASMFuncType));
    if (m->types == NULL) return WASM_ERR;
    m->type_count = type_count;

    for (uint32_t i = 0; i < type_count; i++) {
        /* Each function type start with the byte 0x60 */
        if (*offset++ != 0x60) {
            return WASM_ERR;
        }

        /* Read the length of the vector of parameter types */
        uint32_t param_count;
        xreadULEB128_u32(&offset, s->end, &param_count);
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
                return WASM_ERR;
            }
        }

        /* Read the length of the vector of result types */
        uint32_t result_count;
        xreadULEB128_u32(&offset, s->end, &result_count);
        if (result_count > 1) {
            return WASM_ERR;
        }
        m->types[i].result_count = result_count;
        m->types[i].result_type = 0;
        if (result_count > 0) {
            uint8_t t = *offset++;
            /* Check that the result type is correct value type */
            if (t != WASM_VALTYPE_I32 && t != WASM_VALTYPE_I64 &&
                t != WASM_VALTYPE_F32 && t != WASM_VALTYPE_F64) {
                return WASM_ERR;
            }
            m->types[i].result_type = t;
        }
    }

    /* Check the section size */
    if (offset != s->end) {
        return WASM_ERR;
    }

    return WASM_OK;
}

static WASMErr_t parse_function_section(WASMModule *m, WASMSection *s) {
    /* Function section decodes into a vector of type indices that represent
     * the type fields of the functions in the type section. The locals and
     * body fields of the respective functions are encoded separately in the
     * code section */
    uint8_t *offset = s->start;
    uint32_t function_count;
    xreadULEB128_u32(&offset, s->end, &function_count);
    if (function_count == 0) return WASM_OK;
    m->function_count = function_count;
    m->functions = calloc(function_count, sizeof(struct WASMFunction));
    if (m->functions == NULL) return WASM_ERR;

    for (uint32_t i = 0; i < function_count; i++) {
        uint32_t type_index;
        xreadULEB128_u32(&offset, s->end, &type_index);
        if (type_index >= m->type_count) {
            return WASM_ERR;
        }
        m->functions[i].type = &m->types[type_index];
        m->functions[i].typeidx = type_index;
    }

    /* Check the section size */
    if (offset != s->end) {
        return WASM_ERR;
    }

    return WASM_OK;
}

static WASMErr_t parse_memory_section(WASMModule *m, WASMSection *s) {

    uint8_t *offset = s->start;
    uint32_t memory_count;
    xreadULEB128_u32(&offset, s->end, &memory_count);
    if (memory_count > 1) {
        return WASM_ERR;
    }
    m->memories_count = memory_count;
    uint8_t flag;
    xread_u8(&offset, s->end, &flag);
    switch (flag) {
    case WASM_MEMORY_FLAG_ONLY_MIN_LIMIT:
        xreadULEB128_u32(&offset, s->end, &m->memory.init_page_count);
        m->memory.max_page_count = WASM_MEMORY_LIMIT_RANGE;
        break;
    case WASM_MEMORY_FLAG_MIN_AND_MAX_LIMIT:
        xreadULEB128_u32(&offset, s->end, &m->memory.init_page_count);
        xreadULEB128_u32(&offset, s->end, &m->memory.max_page_count);
        break;
    default:
        return WASM_ERR;
    }
    m->memory.num_bytes_per_page = 64 * 1024;

    if (m->memory.init_page_count > WASM_MEMORY_LIMIT_RANGE) return WASM_ERR;
    if (m->memory.max_page_count > WASM_MEMORY_LIMIT_RANGE) return WASM_ERR;
    if (m->memory.init_page_count > m->memory.max_page_count) return WASM_ERR;

    /* Check the section size */
    if (offset != s->end) {
        return WASM_ERR;
    }

    return WASM_OK;
}

static WASMErr_t parse_global_section(WASMModule *m, WASMSection *s) {
    /* The global section decodes into a vector of globals */
    uint8_t *offset = s->start;
    uint32_t global_count;
    xreadULEB128_u32(&offset, s->end, &global_count);
    if (global_count == 0) return WASM_OK;
    m->globals = calloc(global_count, sizeof(struct WASMGlobal));
    if (m->globals == NULL) return WASM_ERR;
    m->global_count = global_count;

    for (uint32_t i = 0; i < global_count; i++) {
        WASMGlobal *g = &m->globals[i];
        WASMValtype t = *offset++;
        if (t != WASM_VALTYPE_I32 && t != WASM_VALTYPE_I64 &&
            t != WASM_VALTYPE_F32 && t != WASM_VALTYPE_F64) {
                return WASM_ERR;
        }
        g->type = t;
        xread_u8(&offset, s->end, (uint8_t *)&g->is_mutable);
        if (g->is_mutable != 0 && g->is_mutable != 1) {
            return WASM_ERR;
        }
        uint8_t opcode;
        uint8_t type;
        xread_u8(&offset, s->end, &opcode);
        switch (opcode) {
        case WASM_OPCODE_I32_CONST:
            xreadILEB128_i32(&offset, s->end, &g->as.i32);
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
            return WASM_ERR;
        }
        /* Each const expression end with the byte 0x0B */
        if (*offset++ != WASM_OPCODE_END) return WASM_ERR;
        if (g->type != type) return WASM_ERR;
    }

    /* Check the section size */
    if (offset != s->end) {
        return WASM_ERR;
    }

    return WASM_OK;
}

static WASMErr_t parse_export_section(WASMModule *m, WASMSection *s) {
    /* The export section decodes into a vector of exports */
    uint8_t *offset = s->start;
    uint32_t export_count;
    xreadULEB128_u32(&offset, s->end, &export_count);
    if (export_count == 0) return WASM_OK;
    m->export_count = export_count;
    m->exports = calloc(export_count, sizeof(struct WASMExport));
    if (m->exports == NULL) return WASM_ERR;

    for (uint32_t i = 0; i < export_count; i++) {
        uint32_t name_len;
        xreadULEB128_u32(&offset, s->end, &name_len);
        uint8_t *name = offset;
        offset += name_len;
        unsigned char export_desc;
        xread_u8(&offset, s->end, &export_desc);
        uint32_t index;
        xreadULEB128_u32(&offset, s->end, &index);
        switch (export_desc) {
        case EXPORT_TYPE_FUNC:
            if (index >= m->function_count) return WASM_ERR;
            break;
        case EXPORT_TYPE_TABLE:
            /* Tables are not implemented */
            break;
        case EXPORT_TYPE_MEM:
            if (index >= m->memories_count) return WASM_ERR;
            break;
        case EXPORT_TYPE_GLOBAL:
            if (index >= m->global_count) return WASM_ERR;
            break;
        default:
            return WASM_ERR;
        }
        m->exports[i].name_len = name_len;
        m->exports[i].name = name;
        m->exports[i].export_desc = export_desc;
        m->exports[i].index = index;
    }

    /* Check the section size */
    if (offset != s->end) {
        return WASM_ERR;
    }

    return WASM_OK;
}

static WASMErr_t parse_code_section(WASMModule *m, WASMSection *s) {
    /* The code section decodes into a vector of code entries that are pairs of
     * value type vectors and expressions. They represent the locals and body
     * field of the functions. The type fields of the respective functions are
     * encoded separately in the function section. */

    uint8_t *offset = s->start;
    uint32_t code_entry_count;
    xreadULEB128_u32(&offset, s->end, &code_entry_count);
    if (code_entry_count != m->function_count) {
        return WASM_ERR;
    }

    for (uint32_t i = 0; i < code_entry_count; i++) {
        uint32_t code_entry_len;
        xreadULEB128_u32(&offset, s->end, &code_entry_len);
        uint8_t *code_entry_start = offset;
        uint32_t local_declaration_count;
        xreadULEB128_u32(&offset, s->end, &local_declaration_count);
        m->functions[i].locals = local_declaration_count > 0 ? offset : NULL;
        uint32_t sum = 0;
        /* Local declarations are compressed into a vector whose entries consist of
         * a u32 count and a value type denoting count locals of the same value type */
        for (uint32_t j = 0; j < local_declaration_count; j++) {
            uint32_t n;
            xreadULEB128_u32(&offset, s->end, &n);
            sum += n;
            uint8_t t = *offset++;
            if (t != WASM_VALTYPE_I32 && t != WASM_VALTYPE_I64 &&
                t != WASM_VALTYPE_F32 && t != WASM_VALTYPE_F64) {
                return WASM_ERR;
            }
        }
        m->functions[i].local_count = sum;

        m->functions[i].code_start = offset;
        uint32_t len = code_entry_len - (uint32_t)(offset - code_entry_start);
        /* Each expression end with the byte 0x0B */
        if (offset[len-1] != WASM_OPCODE_END) {
                return WASM_ERR;
        }
        m->functions[i].code_end = &offset[len-1];
        offset += len; 
    }

    /* Check the section size */
    if (offset != s->end) {
        return WASM_ERR;
    }

    return WASM_OK;
}

static WASMErr_t parse_data_section(WASMModule *m, WASMSection *s) {
    /* The data section decodes into a vector of data segments */
    uint8_t *offset = s->start;
    uint32_t data_seg_count;

    xreadULEB128_u32(&offset, s->end, &data_seg_count);
    if (data_seg_count == 0) return WASM_OK;
    m->data_seg_count = data_seg_count;
    m->data_segments = calloc(data_seg_count, sizeof(struct WASMDataSegment));
    if (m->data_segments == NULL) return WASM_ERR;
    for (uint32_t i = 0; i < data_seg_count; i++) {
        uint32_t memidx;
        xreadULEB128_u32(&offset, s->end, &memidx);
        if (memidx != 0) {
            return WASM_ERR;
        }
        WASMDataSegment *d = &m->data_segments[i];

        uint8_t opcode;
        xread_u8(&offset, s->end, &opcode);
        switch (opcode) {
        case WASM_OPCODE_I32_CONST: {
            int32_t const_expr;
            xreadILEB128_i32(&offset, s->end, &const_expr);
            if (const_expr < 0) return WASM_ERR;
            d->offset = (uint32_t) const_expr;
        } break;
        case WASM_OPCODE_GLOBAL_GET:
            /* not implemented */
            assert(0);
        default:
            return WASM_ERR;
        }
        /* Each const expression end with the byte 0x0B */
        if (*offset++ != WASM_OPCODE_END) return WASM_ERR;

        xreadULEB128_u32(&offset, s->end, &d->len);
        d->bytes = offset;
        offset += d->len;
    }

    /* Check the section size */
    if (offset != s->end) {
        return WASM_ERR;
    }

    return WASM_OK;
}

static void free_sections(WASMSection *sections_list) {
    WASMSection *s = sections_list;
    while (s != NULL) {
        WASMSection *next = s->next;
        free(s);
        s = next;
    }
}

static WASMErr_t push_opd(ValidateCtx *ctx, OperandType type) {
    OperandStackEntry *opd = malloc(sizeof(struct OperandStackEntry));
    if (opd == NULL) return WASM_ERR;
    opd->type = type;
    opd->next = ctx->opd_stack;
    ctx->opd_stack = opd;
    ctx->opd_size++;
    return WASM_OK;
}

static WASMErr_t pop_opd(ValidateCtx *ctx, OperandType *out) {
    ControlStackEntry *ctrl = ctx->ctrl_stack;
    assert(ctrl != NULL);
    if (ctx->opd_size == ctrl->height && ctrl->unreachable) {
        if (out != NULL) *out = OPERAND_TYPE_UNKNOWN;
        return WASM_OK;
    }
    if (ctx->opd_size == ctrl->height) {
        return WASM_ERR;
    }
    assert(ctx->opd_stack != NULL);
    OperandStackEntry *opd = ctx->opd_stack;
    if (out != NULL) *out = opd->type;
    ctx->opd_stack = opd->next;
    ctx->opd_size--;
    free(opd);

    return WASM_OK;
}

static WASMErr_t pop_expect_opd(ValidateCtx *ctx, OperandType expect,
                                OperandType *out) {

    OperandType actual;
    ERR_CHECK(pop_opd(ctx, &actual));
    if (actual == OPERAND_TYPE_UNKNOWN) {
        if (out != NULL) *out = expect;
        return WASM_OK;
    }
    if (expect == OPERAND_TYPE_UNKNOWN) {
        if (out != NULL) *out = actual;
        return WASM_OK;
    }
    if (actual != expect) return WASM_ERR;
    if (out != NULL) *out = actual;
    return WASM_OK;
}

static WASMErr_t push_ctrl(ValidateCtx *ctx, WASMBlocktype label,
                           WASMBlocktype end) {

    ControlStackEntry *ctrl = malloc(sizeof(struct ControlStackEntry));
    if (ctrl == NULL) return WASM_ERR;
    ctrl->label_type = label;
    ctrl->end_type = end;
    ctrl->unreachable = false;
    ctrl->height = ctx->opd_size;
    ctrl->next = ctx->ctrl_stack;
    ctx->ctrl_stack = ctrl;
    ctx->ctrl_size++;
    return WASM_OK;
}

static WASMErr_t pop_ctrl(ValidateCtx *ctx, WASMBlocktype *out) {
    if (ctx->ctrl_stack == NULL) return WASM_ERR;
    ControlStackEntry *top = ctx->ctrl_stack;
    if (top->end_type != WASM_BLOCKTYPE_NONE) {
        ERR_CHECK(pop_expect_opd(ctx, (OperandType)top->end_type, NULL));
    }
    if (ctx->opd_size != top->height) {
        return WASM_ERR;
    }
    if (out != NULL) *out = top->end_type;
    ctx->ctrl_stack = top->next;
    ctx->ctrl_size--;
    free(top);
    return WASM_OK;
}

static WASMErr_t unreachable(ValidateCtx *ctx) {
    if (ctx->ctrl_stack == NULL) return WASM_ERR;
    ControlStackEntry *top = ctx->ctrl_stack;
    while (ctx->opd_size != top->height) {
        ERR_CHECK(pop_opd(ctx, NULL));
    }
    top->unreachable = true;
    return WASM_OK;
}

static ControlStackEntry *ctrl_get(ValidateCtx *ctx, uint32_t index) {
    if (index >= ctx->ctrl_size - 1) return NULL;
    ControlStackEntry *iter = ctx->ctrl_stack;
    for (uint32_t i = 0; i < index; i++) {
        iter = iter->next;
    }
    return iter;
}

static WASMErr_t validate_const(ValidateCtx *ctx, OperandType type) {
    switch (type) {
    case WASM_VALTYPE_I32: {
        int32_t n;
        ERR_CHECK(readILEB128_i32(&ctx->offset, ctx->code_end, &n));
    } break;
    case WASM_VALTYPE_F32:
    case WASM_VALTYPE_I64:
    case WASM_VALTYPE_F64:
        /* not implemented */
        assert(0);
    default:
        assert(0);
    }
    ERR_CHECK(push_opd(ctx, type));
    return WASM_OK;
}

static WASMErr_t validate_binop(ValidateCtx *ctx, OperandType type) {
    ERR_CHECK(pop_expect_opd(ctx, type, NULL));
    ERR_CHECK(pop_expect_opd(ctx, type, NULL));
    ERR_CHECK(push_opd(ctx, type));
    return WASM_OK;
}

static WASMErr_t validate_testop(ValidateCtx *ctx, OperandType type) {
    ERR_CHECK(pop_expect_opd(ctx, type, NULL));
    ERR_CHECK(push_opd(ctx, OPERAND_TYPE_I32));
    return WASM_OK;
}

static WASMErr_t validate_relop(ValidateCtx *ctx, OperandType type) {
    ERR_CHECK(pop_expect_opd(ctx, type, NULL));
    ERR_CHECK(pop_expect_opd(ctx, type, NULL));
    ERR_CHECK(push_opd(ctx, OPERAND_TYPE_I32));
    return WASM_OK;
}

static WASMErr_t validate_load(ValidateCtx *ctx, OperandType type,
                              uint32_t align_upper_limit) {

    if (ctx->m->memories_count == 0) return WASM_ERR;

    uint32_t align, offset;
    ERR_CHECK(readULEB128_u32(&ctx->offset, ctx->code_end, &align));
    ERR_CHECK(readULEB128_u32(&ctx->offset, ctx->code_end, &offset));

    if (align > align_upper_limit) return WASM_ERR;

    ERR_CHECK(pop_expect_opd(ctx, OPERAND_TYPE_I32, NULL));
    ERR_CHECK(push_opd(ctx, type));
    return WASM_OK;
}

static WASMErr_t validate_store(ValidateCtx *ctx, OperandType type,
                              uint32_t align_upper_limit) {

    if (ctx->m->memories_count == 0) return WASM_ERR;

    uint32_t align, offset;
    ERR_CHECK(readULEB128_u32(&ctx->offset, ctx->code_end, &align));
    ERR_CHECK(readULEB128_u32(&ctx->offset, ctx->code_end, &offset));

    if (align > align_upper_limit) return WASM_ERR;

    ERR_CHECK(pop_expect_opd(ctx, OPERAND_TYPE_I32, NULL));
    ERR_CHECK(pop_expect_opd(ctx, type, NULL));
    return WASM_OK;
}

static WASMErr_t validate_global_get(ValidateCtx *ctx) {
    uint32_t globalidx;
    ERR_CHECK(readULEB128_u32(&ctx->offset, ctx->code_end, &globalidx));
    if (globalidx >= ctx->m->global_count) return WASM_ERR;
    WASMValtype t = ctx->m->globals[globalidx].type;
    ERR_CHECK(pop_expect_opd(ctx, (OperandType)t, NULL));
    return WASM_OK;
}

static WASMErr_t validate_global_set(ValidateCtx *ctx) {
    uint32_t globalidx;
    ERR_CHECK(readULEB128_u32(&ctx->offset, ctx->code_end, &globalidx));
    if (globalidx >= ctx->m->global_count) return WASM_ERR;
    WASMValtype t = ctx->m->globals[globalidx].type;
    ERR_CHECK(push_opd(ctx, (OperandType)t));
    return WASM_OK;
}

static WASMErr_t validate_local_set(ValidateCtx *ctx) {
    uint32_t localidx;
    ERR_CHECK(readULEB128_u32(&ctx->offset, ctx->code_end, &localidx));
    uint32_t n = ctx->f->local_count + ctx->t->param_count;
    if (localidx >= n) return WASM_ERR;
    WASMValtype t = wasm_valtype_of(ctx->f, localidx);
    ERR_CHECK(pop_expect_opd(ctx, (OperandType)t, NULL));
    return WASM_OK;
}

static WASMErr_t validate_local_tee(ValidateCtx *ctx) {
    uint32_t localidx;
    ERR_CHECK(readULEB128_u32(&ctx->offset, ctx->code_end, &localidx));
    uint32_t n = ctx->f->local_count + ctx->t->param_count;
    if (localidx >= n) return WASM_ERR;
    WASMValtype t = wasm_valtype_of(ctx->f, localidx);
    ERR_CHECK(pop_expect_opd(ctx, (OperandType)t, NULL));
    ERR_CHECK(push_opd(ctx, (OperandType)t));
    return WASM_OK;
}

static WASMErr_t validate_local_get(ValidateCtx *ctx) {
    uint32_t localidx;
    ERR_CHECK(readULEB128_u32(&ctx->offset, ctx->code_end, &localidx));
    uint32_t n = ctx->f->local_count + ctx->t->param_count;
    if (localidx >= n) return WASM_ERR;
    WASMValtype valtype = wasm_valtype_of(ctx->f, localidx);
    ERR_CHECK(push_opd(ctx, (OperandType)valtype));
    return WASM_OK;
}

static WASMErr_t validate_select(ValidateCtx *ctx) {
    ERR_CHECK(pop_expect_opd(ctx, OPERAND_TYPE_I32, NULL));
    OperandType t1, t2;
    ERR_CHECK(pop_opd(ctx, &t1));
    ERR_CHECK(pop_expect_opd(ctx, t1, &t2));
    ERR_CHECK(push_opd(ctx, t2));
    return WASM_OK;
}

static WASMErr_t validate_call(ValidateCtx *ctx) {
    uint32_t funcidx;
    ERR_CHECK(readULEB128_u32(&ctx->offset, ctx->code_end, &funcidx));
    if (funcidx >= ctx->m->function_count) return WASM_ERR;

    WASMFunction *called_func = &ctx->m->functions[funcidx];
    WASMFuncType *t = called_func->type;
    for (uint32_t i = 0; i < t->param_count; i++) {
        WASMValtype valtype = t->param_types[t->param_count - i - 1];
        ERR_CHECK(pop_expect_opd(ctx, (OperandType)valtype, NULL));
    }
    if (t->result_count > 0) {
        ERR_CHECK(push_opd(ctx, (OperandType)t->result_type));
    }
    return WASM_OK;
}

static WASMErr_t validate_return(ValidateCtx *ctx) {
    if (ctx->t->result_count > 0) {
        WASMValtype t = ctx->t->result_type;
        ERR_CHECK(pop_expect_opd(ctx, (OperandType)t, NULL));
    }
    ERR_CHECK(unreachable(ctx));
    return WASM_OK;
}

static WASMErr_t validate_br_if(ValidateCtx *ctx) {
    uint32_t labelidx;
    ERR_CHECK(readULEB128_u32(&ctx->offset, ctx->code_end, &labelidx));
    ControlStackEntry *ctrl = ctrl_get(ctx, labelidx);
    if (ctrl == NULL) return WASM_ERR;
    ERR_CHECK(pop_expect_opd(ctx, OPERAND_TYPE_I32, NULL));
    if (ctrl->label_type != WASM_BLOCKTYPE_NONE) {
        ERR_CHECK(pop_expect_opd(ctx, (OperandType)ctrl->label_type, NULL));
        ERR_CHECK(push_opd(ctx, (OperandType)ctrl->label_type));
    }
    return WASM_OK;
}

static WASMErr_t validate_br(ValidateCtx *ctx) {
    uint32_t labelidx;
    ERR_CHECK(readULEB128_u32(&ctx->offset, ctx->code_end, &labelidx));
    ControlStackEntry *ctrl = ctrl_get(ctx, labelidx);
    if (ctrl == NULL) return WASM_ERR;
    if (ctrl->label_type != WASM_BLOCKTYPE_NONE) {
        pop_expect_opd(ctx, (OperandType)ctrl->label_type, NULL);
    }
    ERR_CHECK(unreachable(ctx));
    return WASM_OK;
}

static WASMErr_t validate_end(ValidateCtx *ctx) {
    WASMBlocktype result;
    ERR_CHECK(pop_ctrl(ctx, &result));
    if (result != WASM_BLOCKTYPE_NONE) {
        ERR_CHECK(push_opd(ctx, (OperandType)result));
    }
    return WASM_OK;
}

static WASMErr_t validate_else(ValidateCtx *ctx) {
    WASMBlocktype result;
    ERR_CHECK(pop_ctrl(ctx, &result));
    ERR_CHECK(push_ctrl(ctx, result, result));
    return WASM_OK;
}

static WASMErr_t validate_if(ValidateCtx *ctx) {
    WASMBlocktype blktype;
    ERR_CHECK(read_u8(&ctx->offset, ctx->code_end, &blktype));
    if (!IS_WASM_BLOCK_TYPE(blktype)) return WASM_ERR;
    ERR_CHECK(pop_expect_opd(ctx, OPERAND_TYPE_I32, NULL));
    ERR_CHECK(push_ctrl(ctx, blktype, blktype));
    return WASM_OK;
}

static WASMErr_t validate_loop(ValidateCtx *ctx) {
    WASMBlocktype blktype;
    ERR_CHECK(read_u8(&ctx->offset, ctx->code_end, &blktype));
    if (!IS_WASM_BLOCK_TYPE(blktype)) return WASM_ERR;
    ERR_CHECK(push_ctrl(ctx, WASM_BLOCKTYPE_NONE, blktype));
    return WASM_OK;
}

static WASMErr_t validate_block(ValidateCtx *ctx) {
    WASMBlocktype blktype;
    ERR_CHECK(read_u8(&ctx->offset, ctx->code_end, &blktype));
    if (!IS_WASM_BLOCK_TYPE(blktype)) return WASM_ERR;
    ERR_CHECK(push_ctrl(ctx, blktype, blktype));
    return WASM_OK;
}


static WASMErr_t validate_instr(ValidateCtx *ctx, uint8_t opcode) {

    switch (opcode) {
    /* Control instructions */
    case WASM_OPCODE_UNREACHABLE:
        return unreachable(ctx);
    case WASM_OPCODE_NOP:
        return WASM_OK;
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
        return WASM_ERR;
    }
}

static void validate_cleanup(ValidateCtx *ctx) {
    WASMErr_t err;
    while (ctx->opd_size > 0) {
        err = pop_opd(ctx, NULL);
        assert(!err);
    }
    while (ctx->ctrl_size > 0) {
        err = pop_ctrl(ctx, NULL);
        assert(!err);
    }
}

static WASMErr_t validate_fn(WASMModule *m, uint32_t funcidx) {
    assert(funcidx < m->function_count);
    WASMFunction *f = &m->functions[funcidx];
    WASMFuncType *t = f->type;

    ValidateCtx ctx = {0};
    ctx.offset = f->code_start;
    ctx.code_end = f->code_end;
    ctx.m = m;
    ctx.f = f;
    ctx.t = t;

    ERR_CHECK(push_ctrl(&ctx, WASM_BLOCKTYPE_NONE, WASM_BLOCKTYPE_NONE));

    uint8_t opcode;
    while (ctx.offset < ctx.code_end) {
        if (read_u8(&ctx.offset, ctx.code_end, &opcode)) goto ERROR;
        if (validate_instr(&ctx, opcode)) goto ERROR;
    }

    if (t->result_count > 0) {
        pop_expect_opd(&ctx, (OperandType)t->result_type, NULL);
    }
    ERR_CHECK(pop_ctrl(&ctx, NULL));

    assert(ctx.opd_stack == NULL && ctx.opd_size == 0);
    assert(ctx.ctrl_stack == NULL && ctx.ctrl_size == 0);
    return WASM_OK;

ERROR:
    validate_cleanup(&ctx);
    return WASM_ERR;
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

WASMErr_t wasm_decode(WASMModule *m, uint8_t *start, uint32_t len) {
    assert(m != NULL && start != NULL);
    memset(m, 0, sizeof(struct WASMModule));

    WASMSection *sections_list = NULL;
    WASMSection *list_tail = NULL;

    uint8_t *offset = start;
    uint8_t *end = start + len;
    uint32_t magic, version;
    if (read_u32(&offset, end, &magic)) goto ERROR;
    if (read_u32(&offset, end, &version)) goto ERROR;
    if (magic != WASM_MAGIC_NUMBER || version != WASM_VERSION){
        goto ERROR;
    }

    /* Get the start and end of sections in the WASM module */
    uint8_t section_type;
    uint32_t section_size;
    while (offset < end) {
        if (read_u8(&offset, end, &section_type)) goto ERROR;
        if (readULEB128_u32(&offset, end, &section_size)) goto ERROR;
        WASMSection *s = malloc(sizeof(struct WASMSection));
        if (s == NULL) goto ERROR;

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
        goto ERROR;
    }

    /* Check that the sections are in the right order */
    WASMSectionType prev = WASM_SECTION_CUSTOM;
    for (WASMSection *s = sections_list; s != NULL; s = s->next) {
        if (s->type == WASM_SECTION_CUSTOM) continue;
        if (s->type <= prev) goto ERROR;
        prev = s->type;
    }

    /* Parse each section */
    WASMErr_t err = WASM_OK;
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
            /* Table section is not implemented */
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
            goto ERROR;
        }
        if (err != WASM_OK) goto ERROR;
    }

    free_sections(sections_list);
    sections_list = NULL;

    for (uint32_t i = 0; i < m->function_count; i++) {
        if (validate_fn(m, i)) goto ERROR;
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

    return WASM_OK;

ERROR:
    free_sections(sections_list);
    wasm_free(m);
    return WASM_ERR;

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
            uint32_t n;
            /* this call to readULEB128_u32 can't fail because in the function
             * parse_code_section had already checked that the local declaration part
             * of this function in the WASM module is well formed */
            WASMErr_t err = readULEB128_u32(&offset, f->code_start, &n);
            assert(err == WASM_OK);
            t = *offset++;
            sum += n;
        } while (sum < localidx);
        return t;
    }
}

