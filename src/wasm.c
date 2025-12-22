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
        if (*offset++ != 0x0B) return WASM_ERR;
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
        if (offset[len-1] != 0x0B) {
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
        if (*offset++ != 0x0B) return WASM_ERR;

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

void free_wasm_module(WASMModule *m) {
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

static void free_sections(WASMSection *sections_list) {
    WASMSection *s = sections_list;
    while (s != NULL) {
        WASMSection *next = s->next;
        free(s);
        s = next;
    }
}

WASMErr_t load_wasm_module(WASMModule *m, uint8_t *start, uint32_t len) {
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
    free_wasm_module(m);
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

