/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * wasm_decode.c - WebAssembly 1.0 binary decoder/validator implementation.
 *
 * See wasm_decode.h for the overall design (no allocation, decode/validate
 * split, memory-safety guarantees). Implementation notes specific to this file:
 *
 *   - Every read goes through a tiny bounds-checked "reader" (rd_t) whose
 *     `end` is always some pointer that was itself proven to lie within
 *     the original input buffer, so no read anywhere in this file can
 *     ever step outside caller-owned memory, regardless of how the input
 *     is corrupted.
 *
 *   - "Item decoders" (decode_functype, decode_import_entry, ...) are the
 *     single source of truth for the shape of one section entry; both the
 *     public iterators and wdec_validate() call them, so there is exactly
 *     one place that knows how to parse e.g. an import.
 *
 *   - Numeric and memory instructions are dispatched through small static
 *     tables rather than ~150 near-identical switch cases, to keep the
 *     code (and its flash footprint) compact - the operator families in
 *     WebAssembly 1.0 (testop/relop/unop/binop/cvtop) are regular enough
 *     that this is a strict simplification, not a cleverness tax.
 */

#include "wasm_decode.h"
#include "leb128.h"
#include "wasmc/error.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TABLE_LIMIT_MAX_RANGE 0xFFFFFFFFu
/* memory limits are valid "within range 2^16" (spec 3.2.4): min and max
 * (if present) may each be as large as 65536, not 65535. */
#define MEM_LIMIT_MAX_RANGE 0x10000u
#define MAX_FUNCTION_LOCALS 0xFFFFFFFFu
/* type indices fit 16 bits (record_section()) */
#define WDEC_MAX_TYPES 0x10000u

enum {
    SEC_CUSTOM = 0,
    SEC_TYPE = 1,
    SEC_IMPORT = 2,
    SEC_FUNCTION = 3,
    SEC_TABLE = 4,
    SEC_MEMORY = 5,
    SEC_GLOBAL = 6,
    SEC_EXPORT = 7,
    SEC_START = 8,
    SEC_ELEMENT = 9,
    SEC_CODE = 10,
    SEC_DATA = 11
};

/* Bounds-checked reader */
typedef struct {
    const uint8_t *cur;
    const uint8_t *end;
} rd_t;

static wasmc_err_t rd_u8(rd_t *reader, uint8_t *out)
{
    if (reader->cur >= reader->end) {
        return WASMC_ERR_TRUNCATED;
    }
    *out = *reader->cur++;
    return WASMC_OK;
}

static wasmc_err_t rd_bytes(rd_t *reader, uint32_t n, const uint8_t **out)
{
    if (n > (uint32_t)(reader->end - reader->cur)) {
        return WASMC_ERR_TRUNCATED;
    }
    *out = reader->cur;
    reader->cur += n;
    return WASMC_OK;
}

/* the value bits of the last byte of a LEB128 with `used_bits` bits left */
static uint8_t leb_last_mask(unsigned used_bits)
{
    if (used_bits >= LEB_PAYLOAD_BITS) {
        return LEB_PAYLOAD_MASK;
    }
    return (uint8_t)((1U << used_bits) - 1U);
}

/* Generic bounds-checked unsigned LEB128 reader, per spec 5.2.2: at most
 * ceil(maxbits/7) bytes, and any bits in the final byte beyond maxbits
 * must be zero. */
static wasmc_err_t rd_uleb(rd_t *reader, unsigned maxbits, uint64_t *out)
{
    uint64_t result = 0;
    unsigned shift = 0;
    unsigned max_bytes = (maxbits + LEB_PAYLOAD_BITS - 1) / LEB_PAYLOAD_BITS;
    uint8_t byte;
    for (unsigned idx = 0; idx + 1 < max_bytes; idx++) {
        wasmc_err_t err = rd_u8(reader, &byte);
        if (err) {
            return err;
        }
        result |= (uint64_t)(byte & LEB_PAYLOAD_MASK) << shift;
        if (!(byte & LEB_MORE)) {
            *out = result;
            return WASMC_OK;
        }
        shift += LEB_PAYLOAD_BITS;
    }
    /* the last byte: no bits beyond maxbits, no further byte */
    wasmc_err_t err = rd_u8(reader, &byte);
    if (err) {
        return err;
    }
    uint8_t mask = leb_last_mask(maxbits - shift);
    if ((byte & LEB_PAYLOAD_MASK & (uint8_t)~mask) != 0 || (byte & LEB_MORE)) {
        return WASMC_ERR_LEB128_OVERFLOW;
    }
    *out = result | (uint64_t)(byte & LEB_PAYLOAD_MASK) << shift;
    return WASMC_OK;
}

/* Generic bounds-checked signed LEB128 reader, per spec 5.2.2. */
static wasmc_err_t rd_sleb(rd_t *reader, unsigned maxbits, int64_t *out)
{
    uint64_t result = 0;
    unsigned shift = 0;
    unsigned max_bytes = (maxbits + LEB_PAYLOAD_BITS - 1) / LEB_PAYLOAD_BITS;
    uint8_t byte;
    for (unsigned idx = 0; idx + 1 < max_bytes; idx++) {
        wasmc_err_t err = rd_u8(reader, &byte);
        if (err) {
            return err;
        }
        result |= (uint64_t)(byte & LEB_PAYLOAD_MASK) << shift;
        shift += LEB_PAYLOAD_BITS;
        if (!(byte & LEB_MORE)) {
            if (shift < LEB_BITS64 && (byte & LEB_SIGN)) {
                result |= (~(uint64_t)0) << shift;
            }
            *out = (int64_t)result;
            return WASMC_OK;
        }
    }
    /* the last byte: its bits beyond maxbits must all copy the sign bit,
     * and no further byte may follow */
    wasmc_err_t err = rd_u8(reader, &byte);
    if (err) {
        return err;
    }
    unsigned used_bits = maxbits - shift;
    uint8_t val_mask = leb_last_mask(used_bits);
    uint8_t low = byte & LEB_PAYLOAD_MASK;
    uint8_t sign_bit = (uint8_t)((low >> (used_bits - 1)) & 1);
    uint8_t expect =
        sign_bit ? (uint8_t)(LEB_PAYLOAD_MASK & (uint8_t)~val_mask) : 0;
    if ((uint8_t)(low & (uint8_t)~val_mask) != expect || (byte & LEB_MORE)) {
        return WASMC_ERR_LEB128_OVERFLOW;
    }
    result |= (uint64_t)low << shift;
    if (maxbits < LEB_BITS64 && sign_bit) {
        result |= (~(uint64_t)0) << maxbits;
    }
    *out = (int64_t)result;
    return WASMC_OK;
}

static wasmc_err_t rd_u32(rd_t *reader, uint32_t *out)
{
    uint64_t val = 0;
    wasmc_err_t err = rd_uleb(reader, LEB_BITS32, &val);
    if (err) {
        return err;
    }
    *out = (uint32_t)val;
    return WASMC_OK;
}

static wasmc_err_t rd_i32(rd_t *reader, int32_t *out)
{
    int64_t val;
    wasmc_err_t err = rd_sleb(reader, LEB_BITS32, &val);
    if (err) {
        return err;
    }
    *out = (int32_t)val;
    return WASMC_OK;
}

static wasmc_err_t rd_i64(rd_t *reader, int64_t *out)
{
    return rd_sleb(reader, LEB_BITS64, out);
}

static int is_valid_valtype(uint8_t byte)
{
    return byte == WDEC_TYPE_I32 || byte == WDEC_TYPE_I64 ||
           byte == WDEC_TYPE_F32 || byte == WDEC_TYPE_F64;
}

static wasmc_err_t check_valtypes(const uint8_t *ptr, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (!is_valid_valtype(ptr[i])) {
            return WASMC_ERR_BAD_VALTYPE;
        }
    }
    return WASMC_OK;
}

#define FUNCTYPE_TAG 0x60 /* first byte of a function type (spec 5.3.3) */

/* UTF-8 sequences of 2 to 4 bytes: the lead byte's marker bits and value
 * bits, and the smallest code point that needs that length (a smaller one
 * would be an overlong encoding) */
static const struct {
    uint32_t min;
    uint8_t mask;
    uint8_t marker;
    uint8_t payload;
} UTF8_LEADS[] = {
    {0x80, 0xE0, 0xC0, 0x1F},
    {0x800, 0xF0, 0xE0, 0x0F},
    {0x10000, 0xF8, 0xF0, 0x07},
};
#define UTF8_ASCII_END    0x80
#define UTF8_CONT_MASK    0xC0 /* continuation bytes are 10xxxxxx */
#define UTF8_CONT_MARKER  0x80
#define UTF8_CONT_PAYLOAD 0x3F
#define UTF8_CONT_BITS    6
#define UNICODE_MAX       0x10FFFF
#define SURROGATE_FIRST   0xD800
#define SURROGATE_LAST    0xDFFF

/* number of continuation bytes after `lead` (1 to 3), 0 if invalid */
static uint32_t utf8_extra(uint8_t lead)
{
    for (uint32_t i = 0; i < sizeof UTF8_LEADS / sizeof UTF8_LEADS[0]; i++) {
        if ((lead & UTF8_LEADS[i].mask) == UTF8_LEADS[i].marker) {
            return i + 1;
        }
    }
    return 0;
}

/* Minimal but strict UTF-8 validator (rejects overlong encodings,
 * surrogates and out-of-range code points). No allocation. */
static wasmc_err_t check_utf8(const uint8_t *str, uint32_t n)
{
    uint32_t idx = 0;
    while (idx < n) {
        uint8_t lead = str[idx];
        if (lead < UTF8_ASCII_END) {
            idx++;
            continue;
        }
        uint32_t extra = utf8_extra(lead);
        if (!extra || idx + extra >= n) {
            return WASMC_ERR_BAD_UTF8;
        }
        uint32_t code_point = lead & UTF8_LEADS[extra - 1].payload;
        for (uint32_t j = 1; j <= extra; j++) {
            uint8_t cont = str[idx + j];
            if ((cont & UTF8_CONT_MASK) != UTF8_CONT_MARKER) {
                return WASMC_ERR_BAD_UTF8;
            }
            code_point =
                (code_point << UTF8_CONT_BITS) | (cont & UTF8_CONT_PAYLOAD);
        }
        if (code_point < UTF8_LEADS[extra - 1].min ||
            code_point > UNICODE_MAX ||
            (code_point >= SURROGATE_FIRST && code_point <= SURROGATE_LAST)) {
            return WASMC_ERR_BAD_UTF8;
        }
        idx += extra + 1;
    }
    return WASMC_OK;
}

static wasmc_err_t decode_limits(rd_t *reader, uint64_t range_max,
                                 wdec_limits_t *out)
{
    uint8_t flag;
    wasmc_err_t err = rd_u8(reader, &flag);
    if (err) {
        return err;
    }
    if (flag > 1) {
        return WASMC_ERR_BAD_LIMITS;
    }
    uint32_t min_v;
    err = rd_u32(reader, &min_v);
    if (err) {
        return err;
    }
    out->min = min_v;
    out->has_max = 0;
    out->max = 0;
    if (flag == 1) {
        uint32_t max_v;
        err = rd_u32(reader, &max_v);
        if (err) {
            return err;
        }
        out->has_max = 1;
        out->max = max_v;
        if ((uint64_t)max_v > range_max || (uint64_t)min_v > range_max ||
            min_v > max_v) {
            return WASMC_ERR_BAD_LIMITS;
        }
    } else {
        if ((uint64_t)min_v > range_max) {
            return WASMC_ERR_BAD_LIMITS;
        }
    }
    return WASMC_OK;
}

/* bytes [start, end) of the input */
typedef struct {
    const uint8_t *start;
    const uint8_t *end;
} byte_span;

/* Decodes a WebAssembly 1.0 constant expression: exactly one of
 * {i32,i64,f32,f64}.const or global.get, followed by 'end' (0x0B). Unlike
 * function bodies, const exprs carry no length prefix in the binary
 * format, so a decoder has no choice but to understand this (small,
 * fixed) instruction shape just to find where the expression ends. */
static wasmc_err_t decode_const_expr(rd_t *reader, byte_span *out)
{
    const uint8_t *start = reader->cur;
    uint8_t op_byte;
    wasmc_err_t err = rd_u8(reader, &op_byte);
    if (err) {
        return err;
    }
    wdec_opcode_t opcode = (wdec_opcode_t)op_byte;
    switch (opcode) {
    case WDEC_OP_I32_CONST: {
        int32_t val;
        err = rd_i32(reader, &val);
        if (err) {
            return err;
        }
        break;
    }
    case WDEC_OP_I64_CONST: {
        int64_t val;
        err = rd_i64(reader, &val);
        if (err) {
            return err;
        }
        break;
    }
    case WDEC_OP_F32_CONST: {
        const uint8_t *ptr;
        err = rd_bytes(reader, sizeof(uint32_t), &ptr);
        if (err) {
            return err;
        }
        break;
    }
    case WDEC_OP_F64_CONST: {
        const uint8_t *ptr;
        err = rd_bytes(reader, sizeof(uint64_t), &ptr);
        if (err) {
            return err;
        }
        break;
    }
    case WDEC_OP_GLOBAL_GET: {
        uint32_t idx;
        err = rd_u32(reader, &idx);
        if (err) {
            return err;
        }
        break;
    }
    default:
        return WASMC_ERR_BAD_CONST_EXPR;
    }
    uint8_t end_op;
    err = rd_u8(reader, &end_op);
    if (err) {
        return err;
    }
    if (end_op != WDEC_OP_END) {
        return WASMC_ERR_BAD_CONST_EXPR;
    }
    out->start = start;
    out->end = reader->cur;
    return WASMC_OK;
}

static wasmc_err_t read_blocktype(rd_t *reader, wdec_valtype_t *out)
{
    uint8_t byte;
    wasmc_err_t err = rd_u8(reader, &byte);
    if (err) {
        return err;
    }
    if (byte == WDEC_TYPE_EMPTY) {
        *out = WDEC_TYPE_EMPTY;
        return WASMC_OK;
    }
    if (!is_valid_valtype(byte)) {
        return WASMC_ERR_BAD_BLOCKTYPE;
    }
    *out = byte;
    return WASMC_OK;
}

/* Decodes a br_table instruction's immediate: `count` label indices
 * followed by one mandatory default label index (spec 5.4.1). Leaves
 * reader->cur positioned right after the whole immediate. Used by the raw
 * instruction decoder below; the validator's validate_br_table() later walks
 * the recorded label range again to compare each label's type against the
 * default's. */
static wasmc_err_t decode_br_table_header(rd_t *reader, wdec_br_table_t *out)
{
    wasmc_err_t err;
    uint32_t count;
    err = rd_u32(reader, &count);
    if (err) {
        return err;
    }
    const uint8_t *labels = reader->cur;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t tmp;
        err = rd_u32(reader, &tmp);
        if (err) {
            return err;
        }
    }
    const uint8_t *labels_end = reader->cur;
    uint32_t def;
    err = rd_u32(reader, &def);
    if (err) {
        return err;
    }
    out->count = count;
    out->labels = labels;
    out->labels_end = labels_end;
    out->default_label = def;
    return WASMC_OK;
}

/* a reserved byte, which must be 0x00 */
static wasmc_err_t rd_zero_byte(rd_t *reader)
{
    uint8_t byte;
    wasmc_err_t err = rd_u8(reader, &byte);
    if (!err && byte != 0) {
        err = WASMC_ERR_BAD_RESERVED_BYTE;
    }
    return err;
}

/* Decodes exactly one instruction: its opcode plus whatever immediate
 * operand(s) the WebAssembly 1.0 binary format defines for it (spec
 * 5.4). This is purely syntactic - bounds-checked reads plus the fixed
 * grammar checks that are part of the binary format itself (blocktype
 * byte validity, reserved bytes equal to zero), but no type-checking and
 * no notion of the operand/control stack. It is the single place that
 * knows how each opcode is encoded; both wdec_instr_iter_next() and the
 * function-body validator below decode instructions through it, so the
 * two can never disagree about an instruction's shape. */
static wasmc_err_t decode_instr(rd_t *reader, wdec_instr_t *out)
{
    const uint8_t *pos = reader->cur;
    uint8_t op_byte;
    wasmc_err_t err = rd_u8(reader, &op_byte);
    if (err) {
        return err;
    }
    wdec_opcode_t opcode = (wdec_opcode_t)op_byte;
    out->at = pos;
    out->opcode = opcode;

    switch (opcode) {
    case WDEC_OP_UNREACHABLE:
    case WDEC_OP_NOP:
    case WDEC_OP_ELSE:
    case WDEC_OP_END:
    case WDEC_OP_RETURN:
    case WDEC_OP_DROP:
    case WDEC_OP_SELECT:
        break;
    case WDEC_OP_BLOCK:
    case WDEC_OP_LOOP:
    case WDEC_OP_IF:
        err = read_blocktype(reader, &out->imm.blocktype);
        break;
    case WDEC_OP_BR:
    case WDEC_OP_BR_IF:
    case WDEC_OP_CALL:
    case WDEC_OP_LOCAL_GET:
    case WDEC_OP_LOCAL_SET:
    case WDEC_OP_LOCAL_TEE:
    case WDEC_OP_GLOBAL_GET:
    case WDEC_OP_GLOBAL_SET:
        err = rd_u32(reader, &out->imm.index);
        break;
    case WDEC_OP_BR_TABLE:
        err = decode_br_table_header(reader, &out->imm.br_table);
        break;
    case WDEC_OP_CALL_INDIRECT: /* typeidx, then a reserved 0x00 byte that
                                 * is part of the grammar itself (spec
                                 * 5.4.1). */
        err = rd_u32(reader, &out->imm.index);
        if (!err) {
            err = rd_zero_byte(reader);
        }
        break;
    case WDEC_OP_I32_LOAD:
    case WDEC_OP_I64_LOAD:
    case WDEC_OP_F32_LOAD:
    case WDEC_OP_F64_LOAD:
    case WDEC_OP_I32_LOAD8_S:
    case WDEC_OP_I32_LOAD8_U:
    case WDEC_OP_I32_LOAD16_S:
    case WDEC_OP_I32_LOAD16_U:
    case WDEC_OP_I64_LOAD8_S:
    case WDEC_OP_I64_LOAD8_U:
    case WDEC_OP_I64_LOAD16_S:
    case WDEC_OP_I64_LOAD16_U:
    case WDEC_OP_I64_LOAD32_S:
    case WDEC_OP_I64_LOAD32_U:
    case WDEC_OP_I32_STORE:
    case WDEC_OP_I64_STORE:
    case WDEC_OP_F32_STORE:
    case WDEC_OP_F64_STORE:
    case WDEC_OP_I32_STORE8:
    case WDEC_OP_I32_STORE16:
    case WDEC_OP_I64_STORE8:
    case WDEC_OP_I64_STORE16:
    case WDEC_OP_I64_STORE32:
        err = rd_u32(reader, &out->imm.memarg.align);
        if (!err) {
            err = rd_u32(reader, &out->imm.memarg.offset);
        }
        break;
    case WDEC_OP_MEMORY_SIZE:
    case WDEC_OP_MEMORY_GROW:
        err = rd_zero_byte(reader);
        break;
    case WDEC_OP_I32_CONST:
        err = rd_i32(reader, &out->imm.i32);
        break;
    case WDEC_OP_I64_CONST:
        err = rd_i64(reader, &out->imm.i64);
        break;
    case WDEC_OP_F32_CONST:
        err = rd_bytes(reader, sizeof(uint32_t), &out->imm.f32_bytes);
        break;
    case WDEC_OP_F64_CONST:
        err = rd_bytes(reader, sizeof(uint64_t), &out->imm.f64_bytes);
        break;
    default:
        if (opcode < WDEC_OP_I32_EQZ || opcode > WDEC_OP_F64_REINTERPRET_I64) {
            err = WASMC_ERR_BAD_OPCODE;
        }
        break; /* numeric instructions: opcode only, no immediate */
    }
    return err;
}

/* Item decoders */

static wasmc_err_t decode_functype(rd_t *reader, wdec_functype_t *out)
{
    uint8_t tag;
    wasmc_err_t err = rd_u8(reader, &tag);
    if (err) {
        return err;
    }
    if (tag != FUNCTYPE_TAG) {
        return WASMC_ERR_BAD_FUNCTYPE_TAG;
    }

    uint32_t nparams;
    err = rd_u32(reader, &nparams);
    if (err) {
        return err;
    }
    const uint8_t *params = NULL;
    if (nparams) {
        err = rd_bytes(reader, nparams, &params);
        if (err) {
            return err;
        }
        err = check_valtypes(params, nparams);
        if (err) {
            return err;
        }
    }

    uint32_t nresults;
    err = rd_u32(reader, &nresults);
    if (err) {
        return err;
    }
    if (nresults > 1) {
        return WASMC_ERR_TOO_MANY_RESULTS;
    }
    const uint8_t *results = NULL;
    if (nresults) {
        err = rd_bytes(reader, nresults, &results);
        if (err) {
            return err;
        }
        err = check_valtypes(results, nresults);
        if (err) {
            return err;
        }
    }

    out->param_count = nparams;
    out->params = params;
    out->result_count = nresults;
    out->results = results;
    return WASMC_OK;
}

/* a name: u32 length, then the bytes (not NUL-terminated) */
static wasmc_err_t rd_name(rd_t *reader, const char **name, uint32_t *len)
{
    wasmc_err_t err = rd_u32(reader, len);
    if (err) {
        return err;
    }
    const uint8_t *ptr = NULL;
    if (*len) {
        err = rd_bytes(reader, *len, &ptr);
        if (err) {
            return err;
        }
    }
    *name = (const char *)ptr;
    return WASMC_OK;
}

/* globaltype (spec 5.3.7): value type, then mutability */
static wasmc_err_t decode_globaltype(rd_t *reader, wdec_globaltype_t *out)
{
    wasmc_err_t err = rd_u8(reader, &out->valtype);
    if (err) {
        return err;
    }
    if (!is_valid_valtype(out->valtype)) {
        return WASMC_ERR_BAD_VALTYPE;
    }
    err = rd_u8(reader, &out->mut);
    if (err) {
        return err;
    }
    if (out->mut > 1) {
        return WASMC_ERR_BAD_MUTABILITY;
    }
    return WASMC_OK;
}

/* tabletype (spec 5.3.6): funcref, then the limits */
static wasmc_err_t decode_tabletype(rd_t *reader, uint8_t *elemtype,
                                    wdec_limits_t *lim)
{
    wasmc_err_t err = rd_u8(reader, elemtype);
    if (err) {
        return err;
    }
    if (*elemtype != WDEC_TYPE_FUNCREF) {
        return WASMC_ERR_BAD_ELEMTYPE;
    }
    return decode_limits(reader, TABLE_LIMIT_MAX_RANGE, lim);
}

/* the import descriptor of `out->kind` */
static wasmc_err_t decode_import_desc(rd_t *reader, wdec_import_t *out)
{
    switch (out->kind) {
    case WDEC_IMPKIND_FUNC:
        return rd_u32(reader, &out->desc.typeidx);
    case WDEC_IMPKIND_TABLE:
        return decode_tabletype(reader, &out->desc.table.elemtype,
                                &out->desc.table.limits);
    case WDEC_IMPKIND_MEM:
        return decode_limits(reader, MEM_LIMIT_MAX_RANGE, &out->desc.mem);
    case WDEC_IMPKIND_GLOBAL:
        return decode_globaltype(reader, &out->desc.global);
    default:
        return WASMC_ERR_BAD_IMPORT_KIND;
    }
}

static wasmc_err_t decode_import_entry(rd_t *reader, wdec_import_t *out)
{
    wasmc_err_t err = rd_name(reader, &out->module, &out->module_len);
    if (err) {
        return err;
    }
    err = rd_name(reader, &out->name, &out->name_len);
    if (err) {
        return err;
    }
    err = rd_u8(reader, &out->kind);
    if (err) {
        return err;
    }
    return decode_import_desc(reader, out);
}

static wasmc_err_t decode_code_entry(rd_t *reader, uint32_t typeidx,
                                     wdec_func_t *out)
{
    wasmc_err_t err;
    uint32_t size;
    err = rd_u32(reader, &size);
    if (err) {
        return err;
    }
    if (size > (uint32_t)(reader->end - reader->cur)) {
        return WASMC_ERR_TRUNCATED;
    }
    const uint8_t *entry_start = reader->cur;
    const uint8_t *entry_end = entry_start + size;
    rd_t branch = {entry_start, entry_end};

    uint32_t decl_count;
    err = rd_u32(&branch, &decl_count);
    if (err) {
        return err;
    }
    const uint8_t *locals_start = branch.cur;
    uint64_t total_locals = 0;
    for (uint32_t i = 0; i < decl_count; i++) {
        uint32_t cnt;
        err = rd_u32(&branch, &cnt);
        if (err) {
            return err;
        }
        uint8_t valtype;
        err = rd_u8(&branch, &valtype);
        if (err) {
            return err;
        }
        if (!is_valid_valtype(valtype)) {
            return WASMC_ERR_BAD_VALTYPE;
        }
        total_locals += cnt;
        if (total_locals > MAX_FUNCTION_LOCALS) {
            return WASMC_ERR_TOO_MANY_LOCALS;
        }
    }
    const uint8_t *locals_end = branch.cur;

    out->typeidx = typeidx;
    out->locals_start = decl_count ? locals_start : NULL;
    out->locals_end = decl_count ? locals_end : NULL;
    out->local_decl_count = decl_count;
    out->local_count = (uint32_t)total_locals;
    out->body = branch.cur;
    out->body_end = entry_end;

    reader->cur = entry_end;
    return WASMC_OK;
}

static wasmc_err_t decode_table_entry(rd_t *reader, wdec_table_t *out)
{
    return decode_tabletype(reader, &out->elemtype, &out->limits);
}

static wasmc_err_t decode_mem_entry(rd_t *reader, wdec_limits_t *out)
{
    return decode_limits(reader, MEM_LIMIT_MAX_RANGE, out);
}

static wasmc_err_t decode_global_entry(rd_t *reader, wdec_global_t *out)
{
    wdec_globaltype_t type;
    wasmc_err_t err = decode_globaltype(reader, &type);
    if (err) {
        return err;
    }
    byte_span expr;
    err = decode_const_expr(reader, &expr);
    if (err) {
        return err;
    }
    out->valtype = type.valtype;
    out->mut = type.mut;
    out->init_expr = expr.start;
    out->init_expr_end = expr.end;
    return WASMC_OK;
}

static wasmc_err_t decode_export_entry(rd_t *reader, wdec_export_t *out)
{
    const char *name;
    uint32_t nlen;
    wasmc_err_t err = rd_name(reader, &name, &nlen);
    if (err) {
        return err;
    }
    uint8_t kind;
    err = rd_u8(reader, &kind);
    if (err) {
        return err;
    }
    if (kind > WDEC_EXTKIND_GLOBAL) {
        return WASMC_ERR_BAD_EXPORT_KIND;
    }
    uint32_t idx;
    err = rd_u32(reader, &idx);
    if (err) {
        return err;
    }
    out->name = name;
    out->name_len = nlen;
    out->kind = kind;
    out->index = idx;
    return WASMC_OK;
}

static wasmc_err_t decode_elem_entry(rd_t *reader, wdec_elem_t *out)
{
    uint32_t tidx;
    wasmc_err_t err = rd_u32(reader, &tidx);
    if (err) {
        return err;
    }
    byte_span expr;
    err = decode_const_expr(reader, &expr);
    if (err) {
        return err;
    }
    uint32_t fcount;
    err = rd_u32(reader, &fcount);
    if (err) {
        return err;
    }
    const uint8_t *fstart = reader->cur;
    for (uint32_t i = 0; i < fcount; i++) {
        uint32_t val;
        err = rd_u32(reader, &val);
        if (err) {
            return err;
        }
    }
    out->tableidx = tidx;
    out->offset_expr = expr.start;
    out->offset_expr_end = expr.end;
    out->func_count = fcount;
    out->funcidxs = fstart;
    out->funcidxs_end = reader->cur;
    return WASMC_OK;
}

static wasmc_err_t decode_data_entry(rd_t *reader, wdec_data_t *out)
{
    uint32_t midx;
    wasmc_err_t err = rd_u32(reader, &midx);
    if (err) {
        return err;
    }
    byte_span expr;
    err = decode_const_expr(reader, &expr);
    if (err) {
        return err;
    }
    uint32_t dlen;
    err = rd_u32(reader, &dlen);
    if (err) {
        return err;
    }
    const uint8_t *dst_ptr = NULL;
    if (dlen) {
        err = rd_bytes(reader, dlen, &dst_ptr);
        if (err) {
            return err;
        }
    }
    out->memidx = midx;
    out->offset_expr = expr.start;
    out->offset_expr_end = expr.end;
    out->data_len = dlen;
    out->data = dst_ptr;
    return WASMC_OK;
}

static wasmc_err_t decode_magic(rd_t *reader)
{
    static const uint8_t magic[4] = {0x00, 'a', 's', 'm'};
    const uint8_t *ptr;
    wasmc_err_t err = rd_bytes(reader, 4, &ptr);
    if (err) {
        return err;
    }
    if (memcmp(ptr, magic, 4) != 0) {
        return WASMC_ERR_BAD_MAGIC;
    }
    return WASMC_OK;
}

static wasmc_err_t decode_version(rd_t *reader)
{
    static const uint8_t version[4] = {0x01, 0x00, 0x00, 0x00};
    const uint8_t *ptr;
    wasmc_err_t err = rd_bytes(reader, 4, &ptr);
    if (err) {
        return err;
    }
    if (memcmp(ptr, version, 4) != 0) {
        return WASMC_ERR_BAD_VERSION;
    }
    return WASMC_OK;
}

/*
 * customsec ::= name byte* (spec 5.5.3). The name is the only part of a
 * custom section with defined structure - and per spec 5.2.4 a name must be
 * valid UTF-8 - so it is read and checked here, at zero extra asymptotic
 * cost since wdec_decode() already visits every section once anyway. The bytes
 * that follow are fully opaque custom content and are only ever skipped,
 * never examined: per spec, errors in content an implementation does not
 * interpret must not invalidate the module.
 */
static wasmc_err_t check_custom_section(rd_t sec_rd)
{
    uint32_t name_len;
    wasmc_err_t err = rd_u32(&sec_rd, &name_len);
    if (err) {
        return err;
    }
    const uint8_t *name;
    err = rd_bytes(&sec_rd, name_len, &name);
    if (err) {
        return err;
    }
    return check_utf8(name, name_len);
}

/* the vector of the module for a known section other than start */
static wdec_vec_t *section_vec(wdec_module_t *mod, uint8_t ident)
{
    switch (ident) {
    case SEC_TYPE:
        return &mod->types;
    case SEC_IMPORT:
        return &mod->imports;
    case SEC_FUNCTION:
        return &mod->funcs;
    case SEC_TABLE:
        return &mod->tables;
    case SEC_MEMORY:
        return &mod->mems;
    case SEC_GLOBAL:
        return &mod->globals;
    case SEC_EXPORT:
        return &mod->exports;
    case SEC_ELEMENT:
        return &mod->elems;
    case SEC_CODE:
        return &mod->codes;
    default:
        return &mod->data;
    }
}

/* Records a known section: the start function, or the element count and
 * the bounds of a vector section. */
static wasmc_err_t record_section(wdec_module_t *mod, uint8_t ident,
                                  rd_t sec_rd)
{
    if (ident == SEC_START) {
        uint32_t fidx;
        wasmc_err_t err = rd_u32(&sec_rd, &fidx);
        if (err) {
            return err;
        }
        mod->has_start = 1;
        mod->start_funcidx = fidx;
        return WASMC_OK;
    }
    wdec_vec_t *vec = section_vec(mod, ident);
    uint32_t count;
    wasmc_err_t err = rd_u32(&sec_rd, &count);
    if (err) {
        return err;
    }
    /* an implementation limit (the spec allows them, 2.5.1): the compiler
     * and WAMR's AOT format (an import's type) keep type indices in 16 bits,
     * and a canonical type index (aotw.c) is at most its own index */
    if (ident == SEC_TYPE && count > WDEC_MAX_TYPES) {
        return WASMC_ERR_TOO_MANY_TYPES;
    }
    vec->count = count;
    vec->start = sec_rd.cur;
    vec->end = sec_rd.end;
    return WASMC_OK;
}

/* Sections other than custom ones come once each, in id order. */
static wasmc_err_t check_section_order(uint8_t ident, int last_id)
{
    if (ident > SEC_DATA) {
        return WASMC_ERR_BAD_SECTION_ID;
    }
    if ((int)ident == last_id) {
        return WASMC_ERR_DUPLICATE_SECTION;
    }
    if ((int)ident < last_id) {
        return WASMC_ERR_BAD_SECTION_ORDER;
    }
    return WASMC_OK;
}

/* Skims the import section once to count how many of each kind it
 * declares. This is needed to map indices into the combined
 * (imports-first) index spaces used throughout the rest of the module. */
static wasmc_err_t count_imports(wdec_module_t *mod)
{
    wdec_import_iter_t iter;
    wdec_import_t imp;
    WDEC_FOR_EACH_IMPORT(iter, imp, mod) {
        switch (imp.kind) {
        case WDEC_EXTKIND_FUNC:
            mod->num_import_funcs++;
            break;
        case WDEC_EXTKIND_TABLE:
            mod->num_import_tables++;
            break;
        case WDEC_EXTKIND_MEM:
            mod->num_import_mems++;
            break;
        case WDEC_EXTKIND_GLOBAL:
            mod->num_import_globals++;
            break;
        default:
            return WASMC_ERR_BAD_IMPORT_KIND;
        }
    }
    return wdec_import_iter_err(&iter);
}

wasmc_err_t wdec_decode(wdec_module_t *mod, const uint8_t *buf, uint32_t len)
{
    if (!mod || !buf) {
        return WASMC_ERR_INVALID_ARG;
    }
    memset(mod, 0, sizeof(*mod));
    mod->buf = buf;
    mod->len = len;
    rd_t reader = {buf, buf + len};

    wasmc_err_t err = decode_magic(&reader);
    if (!err) {
        err = decode_version(&reader);
    }
    int last_id = -1;
    while (!err && reader.cur < reader.end) {
        uint8_t ident;
        uint32_t size;
        err = rd_u8(&reader, &ident);
        if (!err) {
            err = rd_u32(&reader, &size);
        }
        if (err) {
            return err;
        }
        if (size > (uint32_t)(reader.end - reader.cur)) {
            return WASMC_ERR_TRUNCATED;
        }
        const rd_t sec_rd = {reader.cur, reader.cur + size};
        reader.cur = sec_rd.end;
        if (ident == SEC_CUSTOM) {
            err = check_custom_section(sec_rd);
            continue;
        }
        err = check_section_order(ident, last_id);
        if (!err) {
            last_id = (int)ident;
            err = record_section(mod, ident, sec_rd);
        }
    }
    return err ? err : count_imports(mod);
}

/* Types section */

void wdec_type_iter_init(wdec_type_iter_t *iter, const wdec_module_t *mod)
{
    iter->cur = mod->types.start;
    iter->end = mod->types.end;
    iter->err = WASMC_OK;
}

int wdec_type_iter_next(wdec_type_iter_t *iter, wdec_functype_t *out)
{
    if (iter->cur >= iter->end) {
        return 0;
    }
    rd_t reader = {iter->cur, iter->end};
    wasmc_err_t status = decode_functype(&reader, out);
    if (status) {
        iter->err = status;
        iter->cur = iter->end;
        return 0;
    }
    iter->cur = reader.cur;
    return 1;
}

uint32_t wdec_type_count(const wdec_module_t *mod)
{
    return mod->types.count;
}

wasmc_err_t wdec_get_type(const wdec_module_t *mod, uint32_t typeidx,
                          wdec_functype_t *out)
{
    if (typeidx >= mod->types.count) {
        return WASMC_ERR_TYPE_INDEX;
    }
    wdec_type_iter_t iter;
    wdec_type_iter_init(&iter, mod);
    wdec_functype_t tmp;
    memset(&tmp, 0, sizeof tmp);
    for (uint32_t i = 0; i <= typeidx; i++) {
        if (!wdec_type_iter_next(&iter, &tmp)) {
            const wasmc_err_t err = wdec_type_iter_err(&iter);
            return err ? err : WASMC_ERR_TYPE_INDEX;
        }
    }
    *out = tmp;
    return WASMC_OK;
}

/* Import section */

void wdec_import_iter_init(wdec_import_iter_t *iter, const wdec_module_t *mod)
{
    iter->cur = mod->imports.start;
    iter->end = mod->imports.end;
    iter->err = WASMC_OK;
}

int wdec_import_iter_next(wdec_import_iter_t *iter, wdec_import_t *out)
{
    if (iter->cur >= iter->end) {
        return 0;
    }
    rd_t reader = {iter->cur, iter->end};
    wasmc_err_t status = decode_import_entry(&reader, out);
    if (status) {
        iter->err = status;
        iter->cur = iter->end;
        return 0;
    }
    iter->cur = reader.cur;
    return 1;
}

uint32_t wdec_import_count(const wdec_module_t *mod)
{
    return mod->imports.count;
}

wasmc_err_t wdec_get_import(const wdec_module_t *mod, uint32_t idx,
                            wdec_import_t *out)
{
    if (idx >= mod->imports.count) {
        return WASMC_ERR_INVALID_ARG;
    }
    wdec_import_iter_t iter;
    wdec_import_iter_init(&iter, mod);
    wdec_import_t tmp;
    memset(&tmp, 0, sizeof tmp);
    for (uint32_t i = 0; i <= idx; i++) {
        if (!wdec_import_iter_next(&iter, &tmp)) {
            const wasmc_err_t err = wdec_import_iter_err(&iter);
            return err ? err : WASMC_ERR_INVALID_ARG;
        }
    }
    *out = tmp;
    return WASMC_OK;
}

/* Functions (function + code sections) */

void wdec_locals_iter_init(wdec_locals_iter_t *iter, const wdec_func_t *func)
{
    iter->cur = func->locals_start;
    iter->end = func->locals_end;
    iter->err = WASMC_OK;
}

int wdec_locals_iter_next(wdec_locals_iter_t *iter, uint32_t *count,
                          wdec_valtype_t *valtype)
{
    if (!iter->cur || iter->cur >= iter->end) {
        return 0;
    }
    rd_t reader = {iter->cur, iter->end};
    uint32_t cnt;
    wasmc_err_t status = rd_u32(&reader, &cnt);
    if (status) {
        iter->err = status;
        iter->cur = iter->end;
        return 0;
    }
    uint8_t vtype;
    status = rd_u8(&reader, &vtype);
    if (status) {
        iter->err = status;
        iter->cur = iter->end;
        return 0;
    }
    if (!is_valid_valtype(vtype)) {
        iter->err = WASMC_ERR_BAD_VALTYPE;
        iter->cur = iter->end;
        return 0;
    }
    *count = cnt;
    *valtype = vtype;
    iter->cur = reader.cur;
    return 1;
}

void wdec_func_iter_init(wdec_func_iter_t *iter, const wdec_module_t *mod)
{
    iter->fcur = mod->funcs.start;
    iter->fend = mod->funcs.end;
    iter->ccur = mod->codes.start;
    iter->cend = mod->codes.end;
    iter->err = WASMC_OK;
}

int wdec_func_iter_next(wdec_func_iter_t *iter, wdec_func_t *out)
{
    if (iter->fcur >= iter->fend || iter->ccur >= iter->cend) {
        return 0;
    }
    rd_t func_rd = {iter->fcur, iter->fend};
    uint32_t typeidx;
    wasmc_err_t status = rd_u32(&func_rd, &typeidx);
    if (status) {
        iter->err = status;
        iter->fcur = iter->fend;
        iter->ccur = iter->cend;
        return 0;
    }
    rd_t code_rd = {iter->ccur, iter->cend};
    wdec_func_t tmp;
    status = decode_code_entry(&code_rd, typeidx, &tmp);
    if (status) {
        iter->err = status;
        iter->fcur = iter->fend;
        iter->ccur = iter->cend;
        return 0;
    }
    iter->fcur = func_rd.cur;
    iter->ccur = code_rd.cur;
    *out = tmp;
    return 1;
}

uint32_t wdec_func_count(const wdec_module_t *mod)
{
    return mod->num_import_funcs + mod->funcs.count;
}

uint32_t wdec_imported_func_count(const wdec_module_t *mod)
{
    return mod->num_import_funcs;
}

uint32_t wdec_defined_func_count(const wdec_module_t *mod)
{
    return mod->funcs.count;
}

wasmc_err_t wdec_get_func(const wdec_module_t *mod, uint32_t funcidx,
                          wdec_func_t *out)
{
    if (funcidx < mod->num_import_funcs) {
        return WASMC_ERR_FUNC_IS_IMPORTED;
    }
    uint32_t local_idx = funcidx - mod->num_import_funcs;
    if (local_idx >= mod->funcs.count) {
        return WASMC_ERR_FUNC_INDEX;
    }
    wdec_func_iter_t iter;
    wdec_func_iter_init(&iter, mod);
    wdec_func_t tmp;
    for (uint32_t i = 0; i <= local_idx; i++) {
        if (!wdec_func_iter_next(&iter, &tmp)) {
            const wasmc_err_t err = wdec_func_iter_err(&iter);
            return err ? err : WASMC_ERR_FUNC_INDEX;
        }
    }
    *out = tmp;
    return WASMC_OK;
}

wasmc_err_t wdec_func_typeidx(const wdec_module_t *mod, uint32_t funcidx,
                              uint32_t *out)
{
    if (funcidx < mod->num_import_funcs) {
        wdec_import_iter_t iter;
        wdec_import_t imp;
        uint32_t seen = 0;
        WDEC_FOR_EACH_IMPORT(iter, imp, mod) {
            if (imp.kind == WDEC_IMPKIND_FUNC) {
                if (seen == funcidx) {
                    *out = imp.desc.typeidx;
                    return WASMC_OK;
                }
                seen++;
            }
        }
        wasmc_err_t err = wdec_import_iter_err(&iter);
        return err ? err : WASMC_ERR_FUNC_INDEX;
    }
    uint32_t local_idx = funcidx - mod->num_import_funcs;
    if (local_idx >= mod->funcs.count) {
        return WASMC_ERR_FUNC_INDEX;
    }
    rd_t reader = {mod->funcs.start, mod->funcs.end};
    uint32_t typeidx = 0;
    for (uint32_t i = 0; i <= local_idx; i++) {
        wasmc_err_t status = rd_u32(&reader, &typeidx);
        if (status) {
            return status;
        }
    }
    *out = typeidx;
    return WASMC_OK;
}

wasmc_err_t wdec_func_type(const wdec_module_t *mod, uint32_t funcidx,
                           wdec_functype_t *out)
{
    uint32_t typeidx;
    wasmc_err_t err = wdec_func_typeidx(mod, funcidx, &typeidx);
    if (err) {
        return err;
    }
    return wdec_get_type(mod, typeidx, out);
}

static wasmc_err_t local_type_of(const wdec_functype_t *ftype,
                                 const wdec_func_t *func, uint32_t localidx,
                                 wdec_valtype_t *out)
{
    if (localidx < ftype->param_count) {
        *out = ftype->params[localidx];
        return WASMC_OK;
    }
    uint32_t rem = localidx - ftype->param_count;
    wdec_locals_iter_t iter;
    uint32_t cnt;
    wdec_valtype_t valtype;
    WDEC_FOR_EACH_LOCALS(iter, cnt, valtype, func) {
        if (rem < cnt) {
            *out = valtype;
            return WASMC_OK;
        }
        rem -= cnt;
    }
    wasmc_err_t err = wdec_locals_iter_err(&iter);
    return err ? err : WASMC_ERR_LOCAL_INDEX;
}

wasmc_err_t wdec_local_type(const wdec_module_t *mod, const wdec_func_t *func,
                            uint32_t localidx, wdec_valtype_t *out)
{
    wdec_functype_t ftype;
    wasmc_err_t err = wdec_get_type(mod, func->typeidx, &ftype);
    if (err) {
        return err;
    }
    return local_type_of(&ftype, func, localidx, out);
}

void wdec_br_table_label_iter_init(wdec_br_table_label_iter_t *iter,
                                   const wdec_br_table_t *tbl)
{
    iter->cur = tbl->labels;
    iter->end = tbl->labels_end;
    iter->err = WASMC_OK;
}

int wdec_br_table_label_iter_next(wdec_br_table_label_iter_t *iter,
                                  uint32_t *out)
{
    if (iter->cur >= iter->end) {
        return 0;
    }
    rd_t reader = {iter->cur, iter->end};
    wasmc_err_t status = rd_u32(&reader, out);
    if (status) {
        iter->err = status;
        iter->cur = iter->end;
        return 0;
    }
    iter->cur = reader.cur;
    return 1;
}

void wdec_instr_iter_init(wdec_instr_iter_t *iter, const wdec_func_t *func)
{
    iter->cur = func->body;
    iter->end = func->body_end;
    iter->err = WASMC_OK;
}

int wdec_instr_iter_next(wdec_instr_iter_t *iter, wdec_instr_t *out)
{
    if (iter->cur >= iter->end) {
        return 0;
    }
    rd_t reader = {iter->cur, iter->end};
    wasmc_err_t status = decode_instr(&reader, out);
    if (status) {
        iter->err = status;
        iter->cur = iter->end;
        return 0;
    }
    iter->cur = reader.cur;
    return 1;
}

/* Tables section */

void wdec_table_iter_init(wdec_table_iter_t *iter, const wdec_module_t *mod)
{
    iter->cur = mod->tables.start;
    iter->end = mod->tables.end;
    iter->err = WASMC_OK;
}

int wdec_table_iter_next(wdec_table_iter_t *iter, wdec_table_t *out)
{
    if (iter->cur >= iter->end) {
        return 0;
    }
    rd_t reader = {iter->cur, iter->end};
    wasmc_err_t status = decode_table_entry(&reader, out);
    if (status) {
        iter->err = status;
        iter->cur = iter->end;
        return 0;
    }
    iter->cur = reader.cur;
    return 1;
}

uint32_t wdec_table_count(const wdec_module_t *mod)
{
    return mod->tables.count;
}

wasmc_err_t wdec_get_table(const wdec_module_t *mod, uint32_t idx,
                           wdec_table_t *out)
{
    if (idx >= mod->tables.count) {
        return WASMC_ERR_TABLE_INDEX;
    }
    wdec_table_iter_t iter;
    wdec_table_iter_init(&iter, mod);
    wdec_table_t tmp;
    memset(&tmp, 0, sizeof tmp);
    for (uint32_t i = 0; i <= idx; i++) {
        if (!wdec_table_iter_next(&iter, &tmp)) {
            const wasmc_err_t err = wdec_table_iter_err(&iter);
            return err ? err : WASMC_ERR_TABLE_INDEX;
        }
    }
    *out = tmp;
    return WASMC_OK;
}

/* Memories section */

void wdec_mem_iter_init(wdec_mem_iter_t *iter, const wdec_module_t *mod)
{
    iter->cur = mod->mems.start;
    iter->end = mod->mems.end;
    iter->err = WASMC_OK;
}

int wdec_mem_iter_next(wdec_mem_iter_t *iter, wdec_limits_t *out)
{
    if (iter->cur >= iter->end) {
        return 0;
    }
    rd_t reader = {iter->cur, iter->end};
    wasmc_err_t status = decode_mem_entry(&reader, out);
    if (status) {
        iter->err = status;
        iter->cur = iter->end;
        return 0;
    }
    iter->cur = reader.cur;
    return 1;
}

uint32_t wdec_mem_count(const wdec_module_t *mod)
{
    return mod->mems.count;
}

wasmc_err_t wdec_get_mem(const wdec_module_t *mod, uint32_t idx,
                         wdec_limits_t *out)
{
    if (idx >= mod->mems.count) {
        return WASMC_ERR_MEM_INDEX;
    }
    wdec_mem_iter_t iter;
    wdec_mem_iter_init(&iter, mod);
    wdec_limits_t tmp;
    memset(&tmp, 0, sizeof tmp);
    for (uint32_t i = 0; i <= idx; i++) {
        if (!wdec_mem_iter_next(&iter, &tmp)) {
            const wasmc_err_t err = wdec_mem_iter_err(&iter);
            return err ? err : WASMC_ERR_MEM_INDEX;
        }
    }
    *out = tmp;
    return WASMC_OK;
}

/* Globals section */

void wdec_global_iter_init(wdec_global_iter_t *iter, const wdec_module_t *mod)
{
    iter->cur = mod->globals.start;
    iter->end = mod->globals.end;
    iter->err = WASMC_OK;
}

int wdec_global_iter_next(wdec_global_iter_t *iter, wdec_global_t *out)
{
    if (iter->cur >= iter->end) {
        return 0;
    }
    rd_t reader = {iter->cur, iter->end};
    wasmc_err_t status = decode_global_entry(&reader, out);
    if (status) {
        iter->err = status;
        iter->cur = iter->end;
        return 0;
    }
    iter->cur = reader.cur;
    return 1;
}

uint32_t wdec_global_count(const wdec_module_t *mod)
{
    return mod->globals.count;
}

wasmc_err_t wdec_get_global(const wdec_module_t *mod, uint32_t idx,
                            wdec_global_t *out)
{
    if (idx >= mod->globals.count) {
        return WASMC_ERR_GLOBAL_INDEX;
    }
    wdec_global_iter_t iter;
    wdec_global_iter_init(&iter, mod);
    wdec_global_t tmp;
    memset(&tmp, 0, sizeof tmp);
    for (uint32_t i = 0; i <= idx; i++) {
        if (!wdec_global_iter_next(&iter, &tmp)) {
            const wasmc_err_t err = wdec_global_iter_err(&iter);
            return err ? err : WASMC_ERR_GLOBAL_INDEX;
        }
    }
    *out = tmp;
    return WASMC_OK;
}

wasmc_err_t wdec_global_type(const wdec_module_t *mod, uint32_t globalidx,
                             wdec_globaltype_t *out)
{
    if (globalidx < mod->num_import_globals) {
        wdec_import_iter_t iter;
        wdec_import_t imp;
        uint32_t seen = 0;
        WDEC_FOR_EACH_IMPORT(iter, imp, mod) {
            if (imp.kind == WDEC_EXTKIND_GLOBAL) {
                if (seen == globalidx) {
                    *out = imp.desc.global;
                    return WASMC_OK;
                }
                seen++;
            }
        }
        wasmc_err_t err = wdec_import_iter_err(&iter);
        return err ? err : WASMC_ERR_GLOBAL_INDEX;
    }
    wdec_global_t glob;
    wasmc_err_t status =
        wdec_get_global(mod, globalidx - mod->num_import_globals, &glob);
    if (status) {
        return status;
    }
    out->valtype = glob.valtype;
    out->mut = glob.mut;
    return WASMC_OK;
}

/* Exports section */

void wdec_export_iter_init(wdec_export_iter_t *iter, const wdec_module_t *mod)
{
    iter->cur = mod->exports.start;
    iter->end = mod->exports.end;
    iter->err = WASMC_OK;
}

int wdec_export_iter_next(wdec_export_iter_t *iter, wdec_export_t *out)
{
    if (iter->cur >= iter->end) {
        return 0;
    }
    rd_t reader = {iter->cur, iter->end};
    wasmc_err_t status = decode_export_entry(&reader, out);
    if (status) {
        iter->err = status;
        iter->cur = iter->end;
        return 0;
    }
    iter->cur = reader.cur;
    return 1;
}

uint32_t wdec_export_count(const wdec_module_t *mod)
{
    return mod->exports.count;
}

wasmc_err_t wdec_get_export(const wdec_module_t *mod, uint32_t idx,
                            wdec_export_t *out)
{
    if (idx >= mod->exports.count) {
        return WASMC_ERR_INVALID_ARG;
    }
    wdec_export_iter_t iter;
    wdec_export_iter_init(&iter, mod);
    wdec_export_t tmp;
    memset(&tmp, 0, sizeof tmp);
    for (uint32_t i = 0; i <= idx; i++) {
        if (!wdec_export_iter_next(&iter, &tmp)) {
            const wasmc_err_t err = wdec_export_iter_err(&iter);
            return err ? err : WASMC_ERR_INVALID_ARG;
        }
    }
    *out = tmp;
    return WASMC_OK;
}

wasmc_err_t wdec_find_export(const wdec_module_t *mod, const char *name,
                             uint32_t name_len, uint8_t kind,
                             wdec_export_t *out)
{
    wdec_export_iter_t iter;
    wdec_export_t exp;
    WDEC_FOR_EACH_EXPORT(iter, exp, mod) {
        if (exp.kind == kind && exp.name_len == name_len &&
            (!name_len || memcmp(exp.name, name, name_len) == 0)) {
            *out = exp;
            return WASMC_OK;
        }
    }
    wasmc_err_t err = wdec_export_iter_err(&iter);
    return err ? err : WASMC_ERR_NOT_FOUND;
}

/* Element section */

void wdec_elem_iter_init(wdec_elem_iter_t *iter, const wdec_module_t *mod)
{
    iter->cur = mod->elems.start;
    iter->end = mod->elems.end;
    iter->err = WASMC_OK;
}

int wdec_elem_iter_next(wdec_elem_iter_t *iter, wdec_elem_t *out)
{
    if (iter->cur >= iter->end) {
        return 0;
    }
    rd_t reader = {iter->cur, iter->end};
    wasmc_err_t status = decode_elem_entry(&reader, out);
    if (status) {
        iter->err = status;
        iter->cur = iter->end;
        return 0;
    }
    iter->cur = reader.cur;
    return 1;
}

uint32_t wdec_elem_count(const wdec_module_t *mod)
{
    return mod->elems.count;
}

wasmc_err_t wdec_get_elem(const wdec_module_t *mod, uint32_t idx,
                          wdec_elem_t *out)
{
    if (idx >= mod->elems.count) {
        return WASMC_ERR_INVALID_ARG;
    }
    wdec_elem_iter_t iter;
    wdec_elem_iter_init(&iter, mod);
    wdec_elem_t tmp;
    memset(&tmp, 0, sizeof tmp);
    for (uint32_t i = 0; i <= idx; i++) {
        if (!wdec_elem_iter_next(&iter, &tmp)) {
            const wasmc_err_t err = wdec_elem_iter_err(&iter);
            return err ? err : WASMC_ERR_INVALID_ARG;
        }
    }
    *out = tmp;
    return WASMC_OK;
}

void wdec_elem_funcidx_iter_init(wdec_elem_funcidx_iter_t *iter,
                                 const wdec_elem_t *elem)
{
    iter->cur = elem->funcidxs;
    iter->end = elem->funcidxs_end;
    iter->err = WASMC_OK;
}

int wdec_elem_funcidx_iter_next(wdec_elem_funcidx_iter_t *iter, uint32_t *out)
{
    if (iter->cur >= iter->end) {
        return 0;
    }
    rd_t reader = {iter->cur, iter->end};
    wasmc_err_t status = rd_u32(&reader, out);
    if (status) {
        iter->err = status;
        iter->cur = iter->end;
        return 0;
    }
    iter->cur = reader.cur;
    return 1;
}

/* Data section */

void wdec_data_iter_init(wdec_data_iter_t *iter, const wdec_module_t *mod)
{
    iter->cur = mod->data.start;
    iter->end = mod->data.end;
    iter->err = WASMC_OK;
}

int wdec_data_iter_next(wdec_data_iter_t *iter, wdec_data_t *out)
{
    if (iter->cur >= iter->end) {
        return 0;
    }
    rd_t reader = {iter->cur, iter->end};
    wasmc_err_t status = decode_data_entry(&reader, out);
    if (status) {
        iter->err = status;
        iter->cur = iter->end;
        return 0;
    }
    iter->cur = reader.cur;
    return 1;
}

uint32_t wdec_data_count(const wdec_module_t *mod)
{
    return mod->data.count;
}

wasmc_err_t wdec_get_data(const wdec_module_t *mod, uint32_t idx,
                          wdec_data_t *out)
{
    if (idx >= mod->data.count) {
        return WASMC_ERR_INVALID_ARG;
    }
    wdec_data_iter_t iter;
    wdec_data_iter_init(&iter, mod);
    wdec_data_t tmp;
    memset(&tmp, 0, sizeof tmp);
    for (uint32_t i = 0; i <= idx; i++) {
        if (!wdec_data_iter_next(&iter, &tmp)) {
            const wasmc_err_t err = wdec_data_iter_err(&iter);
            return err ? err : WASMC_ERR_INVALID_ARG;
        }
    }
    *out = tmp;
    return WASMC_OK;
}

/* Start section */

int wdec_has_start(const wdec_module_t *mod)
{
    return mod->has_start;
}
uint32_t wdec_start_funcidx(const wdec_module_t *mod)
{
    return mod->start_funcidx;
}

/* Function-body validation */

void wdec_validator_init(wdec_validator_t *validator, uint8_t *buf,
                         uint32_t buf_len)
{
    /* Round the end of buf down so that every control frame, laid out
     * downwards from there, is properly aligned. */
    uintptr_t base = (uintptr_t)buf;
    uintptr_t end =
        (base + buf_len) & ~(uintptr_t)(_Alignof(wdec_ctrl_frame_t) - 1);

    validator->buf = buf;
    validator->buf_len = buf_len;
    validator->ctrl_end = end > base ? (uint32_t)(end - base) : 0;
    validator->opd_top = 0;
    validator->ctrl_top = 0;
    validator->opd_max = 0;
    validator->ctrl_max = 0;
}

wdec_stack_depth wdec_validator_max_depth(const wdec_validator_t *validator)
{
    wdec_stack_depth depth = {validator->opd_max, validator->ctrl_max};
    return depth;
}

/* Bytes still free between the top of the operand stack (growing up) and
 * the top of the control stack (growing down). */
static uint32_t stack_free(const wdec_validator_t *validator)
{
    return validator->ctrl_end -
           (validator->ctrl_top * (uint32_t)sizeof(wdec_ctrl_frame_t)) -
           validator->opd_top;
}

/* n-th frame from the bottom of the control stack (0 = outermost). */
static wdec_ctrl_frame_t *ctrl_slot(const wdec_validator_t *validator,
                                    uint32_t n)
{
    return (wdec_ctrl_frame_t *)(validator->buf + validator->ctrl_end) - 1 - n;
}

static wdec_ctrl_frame_t *cur_ctrl(const wdec_validator_t *validator)
{
    return ctrl_slot(validator, validator->ctrl_top - 1);
}

static wdec_ctrl_frame_t *ctrl_at(const wdec_validator_t *validator, uint32_t n)
{
    if (n >= validator->ctrl_top) {
        return NULL;
    }
    return ctrl_slot(validator, validator->ctrl_top - 1 - n);
}

static wasmc_err_t push_opd(wdec_validator_t *validator, wdec_valtype_t type)
{
    if (stack_free(validator) < sizeof(wdec_valtype_t)) {
        return WASMC_ERR_OPD_STACK_OVERFLOW;
    }
    validator->buf[validator->opd_top++] = type;
    if (validator->opd_top > validator->opd_max) {
        validator->opd_max = validator->opd_top;
    }
    return WASMC_OK;
}

static wasmc_err_t pop_opd_any(wdec_validator_t *validator, wdec_valtype_t *out)
{
    const wdec_ctrl_frame_t *frame = cur_ctrl(validator);
    if (validator->opd_top == frame->height && frame->unreachable) {
        *out = WDEC_TYPE_UNKNOWN;
        return WASMC_OK;
    }
    if (validator->opd_top == frame->height) {
        return WASMC_ERR_OPD_STACK_UNDERFLOW;
    }
    *out = validator->buf[--validator->opd_top];
    return WASMC_OK;
}

static wasmc_err_t pop_opd_expect(wdec_validator_t *validator,
                                  wdec_valtype_t expect, wdec_valtype_t *out)
{
    wdec_valtype_t actual;
    wasmc_err_t err = pop_opd_any(validator, &actual);
    if (err) {
        return err;
    }
    if (actual == WDEC_TYPE_UNKNOWN) {
        *out = expect;
        return WASMC_OK;
    }
    if (expect == WDEC_TYPE_UNKNOWN) {
        *out = actual;
        return WASMC_OK;
    }
    if (actual != expect) {
        return WASMC_ERR_TYPE_MISMATCH;
    }
    *out = actual;
    return WASMC_OK;
}

/* Enters a block, loop or if whose result type is `result`. A branch to a
 * loop goes back to its start, so its label carries no value. */
static wasmc_err_t push_ctrl(wdec_validator_t *validator, wdec_opcode_t opcode,
                             wdec_valtype_t result)
{
    if (stack_free(validator) < sizeof(wdec_ctrl_frame_t)) {
        return WASMC_ERR_CTRL_STACK_OVERFLOW;
    }
    wdec_ctrl_frame_t *frame = ctrl_slot(validator, validator->ctrl_top++);
    if (validator->ctrl_top > validator->ctrl_max) {
        validator->ctrl_max = validator->ctrl_top;
    }
    frame->opcode = opcode;
    frame->label_type = opcode == WDEC_OP_LOOP ? WDEC_TYPE_EMPTY : result;
    frame->end_type = result;
    frame->else_seen = 0;
    frame->unreachable = 0;
    frame->height = validator->opd_top;
    return WASMC_OK;
}

static wasmc_err_t pop_ctrl(wdec_validator_t *validator,
                            wdec_valtype_t *end_type_out)
{
    if (validator->ctrl_top == 0) {
        return WASMC_ERR_CTRL_STACK_UNDERFLOW;
    }
    const wdec_ctrl_frame_t *frame = cur_ctrl(validator);
    if (frame->end_type != WDEC_TYPE_EMPTY) {
        wdec_valtype_t type;
        wasmc_err_t err = pop_opd_expect(validator, frame->end_type, &type);
        if (err) {
            return err;
        }
    }
    if (validator->opd_top != frame->height) {
        return WASMC_ERR_TYPE_MISMATCH;
    }
    *end_type_out = frame->end_type;
    validator->ctrl_top--;
    return WASMC_OK;
}

static void mark_unreachable(wdec_validator_t *validator)
{
    wdec_ctrl_frame_t *frame = cur_ctrl(validator);
    validator->opd_top = frame->height;
    frame->unreachable = 1;
}

/*
 * Load/store metadata, indexed by MEMOP(opcode), the opcode's offset from
 * i32.load (0x28..0x3E, spec 5.4.4). For each instruction:
 *
 *   type      - the value type loaded (pushed) or stored (popped);
 *   max_align - log2 of the access width in bytes: memarg.align may not
 *               exceed it (spec 3.3.4, "2^align must not be larger than
 *               the bit width of t divided by 8");
 *   is_store  - a store pops [i32 address, value]; a load pops the i32
 *               address and pushes the value.
 *
 * The narrow forms (i32.load8_s, i64.store32, ...) take the type of the
 * full register they extend into or truncate from, and the width of the
 * memory access.
 */
struct memop {
    wdec_valtype_t type;
    uint8_t max_align;
    bool is_store;
};
#define MEMOP(op)          (WDEC_OP_##op - WDEC_OP_I32_LOAD)
#define LOAD(type, align)  {WDEC_TYPE_##type, align, false}
#define STORE(type, align) {WDEC_TYPE_##type, align, true}
static const struct memop MEMOPS[] = {
    /* clang-format off */
    [MEMOP(I32_LOAD)]     = LOAD(I32, 2),
    [MEMOP(I64_LOAD)]     = LOAD(I64, 3),
    [MEMOP(F32_LOAD)]     = LOAD(F32, 2),
    [MEMOP(F64_LOAD)]     = LOAD(F64, 3),
    [MEMOP(I32_LOAD8_S)]  = LOAD(I32, 0),
    [MEMOP(I32_LOAD8_U)]  = LOAD(I32, 0),
    [MEMOP(I32_LOAD16_S)] = LOAD(I32, 1),
    [MEMOP(I32_LOAD16_U)] = LOAD(I32, 1),
    [MEMOP(I64_LOAD8_S)]  = LOAD(I64, 0),
    [MEMOP(I64_LOAD8_U)]  = LOAD(I64, 0),
    [MEMOP(I64_LOAD16_S)] = LOAD(I64, 1),
    [MEMOP(I64_LOAD16_U)] = LOAD(I64, 1),
    [MEMOP(I64_LOAD32_S)] = LOAD(I64, 2),
    [MEMOP(I64_LOAD32_U)] = LOAD(I64, 2),
    [MEMOP(I32_STORE)]    = STORE(I32, 2),
    [MEMOP(I64_STORE)]    = STORE(I64, 3),
    [MEMOP(F32_STORE)]    = STORE(F32, 2),
    [MEMOP(F64_STORE)]    = STORE(F64, 3),
    [MEMOP(I32_STORE8)]   = STORE(I32, 0),
    [MEMOP(I32_STORE16)]  = STORE(I32, 1),
    [MEMOP(I64_STORE8)]   = STORE(I64, 0),
    [MEMOP(I64_STORE16)]  = STORE(I64, 1),
    [MEMOP(I64_STORE32)]  = STORE(I64, 2),
    /* clang-format on */
};
_Static_assert(sizeof MEMOPS / sizeof MEMOPS[0] ==
                   WDEC_OP_I64_STORE32 - WDEC_OP_I32_LOAD + 1,
               "MEMOPS must cover every load and store opcode");
#undef MEMOP
#undef LOAD
#undef STORE

/*
 * Numeric instruction metadata, indexed by NUMOP(opcode), the opcode's
 * offset from i32.eqz (0x45..0xBF, spec 5.4.5). Every numeric instruction
 * pops `pops` operands of type `in` and pushes one result of type `out`,
 * so the shape of each operator family (spec 3.3.1) is one small macro:
 *
 *   TESTOP(t)       t      -> i32   (eqz)
 *   RELOP(t)        t t    -> i32   (eq, lt_s, ge, ...)
 *   UNOP(t)         t      -> t     (clz, neg, sqrt, ...)
 *   BINOP(t)        t t    -> t     (add, shl, copysign, ...)
 *   CVTOP(t1, t2)   t1     -> t2    (wrap, trunc, convert, reinterpret, ...)
 */
struct numop {
    wdec_valtype_t in;
    uint8_t pops;
    wdec_valtype_t out;
};
#define NUMOP(op)       (WDEC_OP_##op - WDEC_OP_I32_EQZ)
#define TESTOP(t)       {WDEC_TYPE_##t, 1, WDEC_TYPE_I32}
#define RELOP(t)        {WDEC_TYPE_##t, 2, WDEC_TYPE_I32}
#define UNOP(t)         {WDEC_TYPE_##t, 1, WDEC_TYPE_##t}
#define BINOP(t)        {WDEC_TYPE_##t, 2, WDEC_TYPE_##t}
#define CVTOP(from, to) {WDEC_TYPE_##from, 1, WDEC_TYPE_##to}
static const struct numop NUMOPS[] = {
    /* clang-format off */
    /* testops and relops */
    [NUMOP(I32_EQZ)]             = TESTOP(I32),
    [NUMOP(I32_EQ)]              = RELOP(I32),
    [NUMOP(I32_NE)]              = RELOP(I32),
    [NUMOP(I32_LT_S)]            = RELOP(I32),
    [NUMOP(I32_LT_U)]            = RELOP(I32),
    [NUMOP(I32_GT_S)]            = RELOP(I32),
    [NUMOP(I32_GT_U)]            = RELOP(I32),
    [NUMOP(I32_LE_S)]            = RELOP(I32),
    [NUMOP(I32_LE_U)]            = RELOP(I32),
    [NUMOP(I32_GE_S)]            = RELOP(I32),
    [NUMOP(I32_GE_U)]            = RELOP(I32),
    [NUMOP(I64_EQZ)]             = TESTOP(I64),
    [NUMOP(I64_EQ)]              = RELOP(I64),
    [NUMOP(I64_NE)]              = RELOP(I64),
    [NUMOP(I64_LT_S)]            = RELOP(I64),
    [NUMOP(I64_LT_U)]            = RELOP(I64),
    [NUMOP(I64_GT_S)]            = RELOP(I64),
    [NUMOP(I64_GT_U)]            = RELOP(I64),
    [NUMOP(I64_LE_S)]            = RELOP(I64),
    [NUMOP(I64_LE_U)]            = RELOP(I64),
    [NUMOP(I64_GE_S)]            = RELOP(I64),
    [NUMOP(I64_GE_U)]            = RELOP(I64),
    [NUMOP(F32_EQ)]              = RELOP(F32),
    [NUMOP(F32_NE)]              = RELOP(F32),
    [NUMOP(F32_LT)]              = RELOP(F32),
    [NUMOP(F32_GT)]              = RELOP(F32),
    [NUMOP(F32_LE)]              = RELOP(F32),
    [NUMOP(F32_GE)]              = RELOP(F32),
    [NUMOP(F64_EQ)]              = RELOP(F64),
    [NUMOP(F64_NE)]              = RELOP(F64),
    [NUMOP(F64_LT)]              = RELOP(F64),
    [NUMOP(F64_GT)]              = RELOP(F64),
    [NUMOP(F64_LE)]              = RELOP(F64),
    [NUMOP(F64_GE)]              = RELOP(F64),
    /* unops and binops */
    [NUMOP(I32_CLZ)]             = UNOP(I32),
    [NUMOP(I32_CTZ)]             = UNOP(I32),
    [NUMOP(I32_POPCNT)]          = UNOP(I32),
    [NUMOP(I32_ADD)]             = BINOP(I32),
    [NUMOP(I32_SUB)]             = BINOP(I32),
    [NUMOP(I32_MUL)]             = BINOP(I32),
    [NUMOP(I32_DIV_S)]           = BINOP(I32),
    [NUMOP(I32_DIV_U)]           = BINOP(I32),
    [NUMOP(I32_REM_S)]           = BINOP(I32),
    [NUMOP(I32_REM_U)]           = BINOP(I32),
    [NUMOP(I32_AND)]             = BINOP(I32),
    [NUMOP(I32_OR)]              = BINOP(I32),
    [NUMOP(I32_XOR)]             = BINOP(I32),
    [NUMOP(I32_SHL)]             = BINOP(I32),
    [NUMOP(I32_SHR_S)]           = BINOP(I32),
    [NUMOP(I32_SHR_U)]           = BINOP(I32),
    [NUMOP(I32_ROTL)]            = BINOP(I32),
    [NUMOP(I32_ROTR)]            = BINOP(I32),
    [NUMOP(I64_CLZ)]             = UNOP(I64),
    [NUMOP(I64_CTZ)]             = UNOP(I64),
    [NUMOP(I64_POPCNT)]          = UNOP(I64),
    [NUMOP(I64_ADD)]             = BINOP(I64),
    [NUMOP(I64_SUB)]             = BINOP(I64),
    [NUMOP(I64_MUL)]             = BINOP(I64),
    [NUMOP(I64_DIV_S)]           = BINOP(I64),
    [NUMOP(I64_DIV_U)]           = BINOP(I64),
    [NUMOP(I64_REM_S)]           = BINOP(I64),
    [NUMOP(I64_REM_U)]           = BINOP(I64),
    [NUMOP(I64_AND)]             = BINOP(I64),
    [NUMOP(I64_OR)]              = BINOP(I64),
    [NUMOP(I64_XOR)]             = BINOP(I64),
    [NUMOP(I64_SHL)]             = BINOP(I64),
    [NUMOP(I64_SHR_S)]           = BINOP(I64),
    [NUMOP(I64_SHR_U)]           = BINOP(I64),
    [NUMOP(I64_ROTL)]            = BINOP(I64),
    [NUMOP(I64_ROTR)]            = BINOP(I64),
    [NUMOP(F32_ABS)]             = UNOP(F32),
    [NUMOP(F32_NEG)]             = UNOP(F32),
    [NUMOP(F32_CEIL)]            = UNOP(F32),
    [NUMOP(F32_FLOOR)]           = UNOP(F32),
    [NUMOP(F32_TRUNC)]           = UNOP(F32),
    [NUMOP(F32_NEAREST)]         = UNOP(F32),
    [NUMOP(F32_SQRT)]            = UNOP(F32),
    [NUMOP(F32_ADD)]             = BINOP(F32),
    [NUMOP(F32_SUB)]             = BINOP(F32),
    [NUMOP(F32_MUL)]             = BINOP(F32),
    [NUMOP(F32_DIV)]             = BINOP(F32),
    [NUMOP(F32_MIN)]             = BINOP(F32),
    [NUMOP(F32_MAX)]             = BINOP(F32),
    [NUMOP(F32_COPYSIGN)]        = BINOP(F32),
    [NUMOP(F64_ABS)]             = UNOP(F64),
    [NUMOP(F64_NEG)]             = UNOP(F64),
    [NUMOP(F64_CEIL)]            = UNOP(F64),
    [NUMOP(F64_FLOOR)]           = UNOP(F64),
    [NUMOP(F64_TRUNC)]           = UNOP(F64),
    [NUMOP(F64_NEAREST)]         = UNOP(F64),
    [NUMOP(F64_SQRT)]            = UNOP(F64),
    [NUMOP(F64_ADD)]             = BINOP(F64),
    [NUMOP(F64_SUB)]             = BINOP(F64),
    [NUMOP(F64_MUL)]             = BINOP(F64),
    [NUMOP(F64_DIV)]             = BINOP(F64),
    [NUMOP(F64_MIN)]             = BINOP(F64),
    [NUMOP(F64_MAX)]             = BINOP(F64),
    [NUMOP(F64_COPYSIGN)]        = BINOP(F64),
    /* cvtops */
    [NUMOP(I32_WRAP_I64)]        = CVTOP(I64, I32),
    [NUMOP(I32_TRUNC_F32_S)]     = CVTOP(F32, I32),
    [NUMOP(I32_TRUNC_F32_U)]     = CVTOP(F32, I32),
    [NUMOP(I32_TRUNC_F64_S)]     = CVTOP(F64, I32),
    [NUMOP(I32_TRUNC_F64_U)]     = CVTOP(F64, I32),
    [NUMOP(I64_EXTEND_I32_S)]    = CVTOP(I32, I64),
    [NUMOP(I64_EXTEND_I32_U)]    = CVTOP(I32, I64),
    [NUMOP(I64_TRUNC_F32_S)]     = CVTOP(F32, I64),
    [NUMOP(I64_TRUNC_F32_U)]     = CVTOP(F32, I64),
    [NUMOP(I64_TRUNC_F64_S)]     = CVTOP(F64, I64),
    [NUMOP(I64_TRUNC_F64_U)]     = CVTOP(F64, I64),
    [NUMOP(F32_CONVERT_I32_S)]   = CVTOP(I32, F32),
    [NUMOP(F32_CONVERT_I32_U)]   = CVTOP(I32, F32),
    [NUMOP(F32_CONVERT_I64_S)]   = CVTOP(I64, F32),
    [NUMOP(F32_CONVERT_I64_U)]   = CVTOP(I64, F32),
    [NUMOP(F32_DEMOTE_F64)]      = CVTOP(F64, F32),
    [NUMOP(F64_CONVERT_I32_S)]   = CVTOP(I32, F64),
    [NUMOP(F64_CONVERT_I32_U)]   = CVTOP(I32, F64),
    [NUMOP(F64_CONVERT_I64_S)]   = CVTOP(I64, F64),
    [NUMOP(F64_CONVERT_I64_U)]   = CVTOP(I64, F64),
    [NUMOP(F64_PROMOTE_F32)]     = CVTOP(F32, F64),
    [NUMOP(I32_REINTERPRET_F32)] = CVTOP(F32, I32),
    [NUMOP(I64_REINTERPRET_F64)] = CVTOP(F64, I64),
    [NUMOP(F32_REINTERPRET_I32)] = CVTOP(I32, F32),
    [NUMOP(F64_REINTERPRET_I64)] = CVTOP(I64, F64),
    /* clang-format on */
};
_Static_assert(sizeof NUMOPS / sizeof NUMOPS[0] ==
                   WDEC_OP_F64_REINTERPRET_I64 - WDEC_OP_I32_EQZ + 1,
               "NUMOPS must cover every numeric opcode");
#undef NUMOP
#undef TESTOP
#undef RELOP
#undef UNOP
#undef BINOP
#undef CVTOP

/* the function whose body is being validated */
typedef struct {
    const wdec_module_t *mod;
    const wdec_func_t *func;
    const wdec_functype_t *ftype;
    wdec_validator_t *validator;
} body_ctx;

/* pops an operand that must be of type `expect` */
static wasmc_err_t pop_expect(wdec_validator_t *validator,
                              wdec_valtype_t expect)
{
    wdec_valtype_t type;
    return pop_opd_expect(validator, expect, &type);
}

static wasmc_err_t validate_if(wdec_validator_t *validator,
                               wdec_valtype_t blocktype)
{
    wasmc_err_t err = pop_expect(validator, WDEC_TYPE_I32);
    if (err) {
        return err;
    }
    return push_ctrl(validator, WDEC_OP_IF, blocktype);
}

static wasmc_err_t validate_else(wdec_validator_t *validator)
{
    const wdec_ctrl_frame_t *frame = cur_ctrl(validator);
    if (frame->opcode != WDEC_OP_IF || frame->else_seen) {
        return WASMC_ERR_UNBALANCED_ELSE;
    }
    wdec_valtype_t res_type;
    wasmc_err_t err = pop_ctrl(validator, &res_type);
    if (!err) {
        err = push_ctrl(validator, WDEC_OP_IF, res_type);
    }
    if (err) {
        return err;
    }
    cur_ctrl(validator)->else_seen = 1;
    return WASMC_OK;
}

static wasmc_err_t validate_end(wdec_validator_t *validator)
{
    const wdec_ctrl_frame_t *frame = cur_ctrl(validator);
    if (frame->opcode == WDEC_OP_IF && !frame->else_seen &&
        frame->end_type != WDEC_TYPE_EMPTY) {
        return WASMC_ERR_MISSING_ELSE;
    }
    wdec_valtype_t res_type;
    wasmc_err_t err = pop_ctrl(validator, &res_type);
    if (err || res_type == WDEC_TYPE_EMPTY) {
        return err;
    }
    return push_opd(validator, res_type);
}

static wasmc_err_t validate_br(wdec_validator_t *validator, uint32_t n)
{
    const wdec_ctrl_frame_t *frame = ctrl_at(validator, n);
    if (!frame) {
        return WASMC_ERR_LABEL_INDEX;
    }
    if (frame->label_type != WDEC_TYPE_EMPTY) {
        wdec_valtype_t type;
        wasmc_err_t err = pop_opd_expect(validator, frame->label_type, &type);
        if (err) {
            return err;
        }
    }
    mark_unreachable(validator);
    return WASMC_OK;
}

static wasmc_err_t validate_br_if(wdec_validator_t *validator, uint32_t n)
{
    const wdec_ctrl_frame_t *frame = ctrl_at(validator, n);
    if (!frame) {
        return WASMC_ERR_LABEL_INDEX;
    }
    wdec_valtype_t type;
    wasmc_err_t err = pop_opd_expect(validator, WDEC_TYPE_I32, &type);
    if (err) {
        return err;
    }
    if (frame->label_type != WDEC_TYPE_EMPTY) {
        err = pop_opd_expect(validator, frame->label_type, &type);
        if (err) {
            return err;
        }
        err = push_opd(validator, frame->label_type);
        if (err) {
            return err;
        }
    }
    return WASMC_OK;
}

/* `tbl` has already been decoded by decode_instr(); this only performs the
 * type-checking side of spec 7.3's br_table case (error_if checks plus
 * the operand stack effect). */
static wasmc_err_t validate_br_table(wdec_validator_t *validator,
                                     const wdec_br_table_t *tbl)
{
    wasmc_err_t err;
    const wdec_ctrl_frame_t *default_frame =
        ctrl_at(validator, tbl->default_label);
    if (!default_frame) {
        return WASMC_ERR_LABEL_INDEX;
    }

    rd_t locals_rd = {tbl->labels, tbl->labels_end};
    for (uint32_t i = 0; i < tbl->count; i++) {
        uint32_t count;
        err = rd_u32(&locals_rd, &count);
        if (err) {
            return err;
        }
        const wdec_ctrl_frame_t *frame = ctrl_at(validator, count);
        if (!frame) {
            return WASMC_ERR_LABEL_INDEX;
        }
        if (frame->label_type != default_frame->label_type) {
            return WASMC_ERR_TYPE_MISMATCH;
        }
    }

    wdec_valtype_t type;
    err = pop_opd_expect(validator, WDEC_TYPE_I32, &type);
    if (err) {
        return err;
    }
    if (default_frame->label_type != WDEC_TYPE_EMPTY) {
        err = pop_opd_expect(validator, default_frame->label_type, &type);
        if (err) {
            return err;
        }
    }
    mark_unreachable(validator);
    return WASMC_OK;
}

/* the arguments of a call, last first, then its result */
static wasmc_err_t pop_call_operands(wdec_validator_t *validator,
                                     const wdec_functype_t *callee)
{
    for (uint32_t i = 0; i < callee->param_count; i++) {
        wasmc_err_t err =
            pop_expect(validator, callee->params[callee->param_count - 1 - i]);
        if (err) {
            return err;
        }
    }
    return callee->result_count ? push_opd(validator, callee->results[0])
                                : WASMC_OK;
}

static wasmc_err_t validate_call(const body_ctx *ctx, uint32_t fidx)
{
    if (fidx >= wdec_func_count(ctx->mod)) {
        return WASMC_ERR_FUNC_INDEX;
    }
    wdec_functype_t callee;
    wasmc_err_t err = wdec_func_type(ctx->mod, fidx, &callee);
    if (err) {
        return err;
    }
    if (callee.result_count > 1) {
        return WASMC_ERR_TOO_MANY_RESULTS;
    }
    return pop_call_operands(ctx->validator, &callee);
}

/* the reserved byte was already checked by decode_instr(); typeidx is
 * range-checked here, by wdec_get_type() */
static wasmc_err_t validate_call_indirect(const body_ctx *ctx, uint32_t typeidx)
{
    const wdec_module_t *mod = ctx->mod;
    if (mod->num_import_tables + mod->tables.count == 0) {
        return WASMC_ERR_TABLE_REQUIRED;
    }
    wdec_functype_t callee;
    wasmc_err_t err = wdec_get_type(mod, typeidx, &callee);
    if (!err) {
        err = pop_expect(ctx->validator, WDEC_TYPE_I32); /* element index */
    }
    if (err) {
        return err;
    }
    return pop_call_operands(ctx->validator, &callee);
}

static wasmc_err_t validate_select(wdec_validator_t *validator)
{
    wdec_valtype_t type1;
    wdec_valtype_t type2;
    wasmc_err_t err = pop_expect(validator, WDEC_TYPE_I32);
    if (!err) {
        err = pop_opd_any(validator, &type1);
    }
    if (!err) {
        err = pop_opd_expect(validator, type1, &type2);
    }
    if (err) {
        return err;
    }
    return push_opd(validator, type2);
}

/* local.get, local.set, local.tee */
static wasmc_err_t validate_local(const body_ctx *ctx, const wdec_instr_t *ins)
{
    uint32_t lidx = ins->imm.index;
    if (lidx >= ctx->ftype->param_count + ctx->func->local_count) {
        return WASMC_ERR_LOCAL_INDEX;
    }
    wdec_valtype_t valtype;
    wasmc_err_t err = local_type_of(ctx->ftype, ctx->func, lidx, &valtype);
    if (err) {
        return err;
    }
    if (ins->opcode == WDEC_OP_LOCAL_GET) {
        return push_opd(ctx->validator, valtype);
    }
    err = pop_expect(ctx->validator, valtype);
    if (!err && ins->opcode == WDEC_OP_LOCAL_TEE) {
        err = push_opd(ctx->validator, valtype);
    }
    return err;
}

/* global.get, global.set */
static wasmc_err_t validate_global(const body_ctx *ctx, const wdec_instr_t *ins)
{
    const wdec_module_t *mod = ctx->mod;
    uint32_t gidx = ins->imm.index;
    if (gidx >= mod->num_import_globals + mod->globals.count) {
        return WASMC_ERR_GLOBAL_INDEX;
    }
    wdec_globaltype_t glob;
    wasmc_err_t err = wdec_global_type(mod, gidx, &glob);
    if (err) {
        return err;
    }
    if (ins->opcode == WDEC_OP_GLOBAL_GET) {
        return push_opd(ctx->validator, glob.valtype);
    }
    if (!glob.mut) {
        return WASMC_ERR_GLOBAL_NOT_MUTABLE;
    }
    return pop_expect(ctx->validator, glob.valtype);
}

static bool has_memory(const wdec_module_t *mod)
{
    return mod->num_import_mems + mod->mems.count != 0;
}

/* the loads and stores */
static wasmc_err_t validate_memop(const body_ctx *ctx, const wdec_instr_t *ins)
{
    const struct memop *mem_info = &MEMOPS[ins->opcode - WDEC_OP_I32_LOAD];
    wdec_validator_t *validator = ctx->validator;
    if (!has_memory(ctx->mod)) {
        return WASMC_ERR_MEMORY_REQUIRED;
    }
    if (ins->imm.memarg.align > mem_info->max_align) {
        return WASMC_ERR_BAD_ALIGNMENT;
    }
    if (mem_info->is_store) {
        wasmc_err_t err = pop_expect(validator, mem_info->type);
        return err ? err : pop_expect(validator, WDEC_TYPE_I32);
    }
    wasmc_err_t err = pop_expect(validator, WDEC_TYPE_I32);
    return err ? err : push_opd(validator, mem_info->type);
}

/* memory.size, memory.grow */
static wasmc_err_t validate_memory_size(const body_ctx *ctx,
                                        wdec_opcode_t opcode)
{
    if (!has_memory(ctx->mod)) {
        return WASMC_ERR_MEMORY_REQUIRED;
    }
    if (opcode == WDEC_OP_MEMORY_GROW) {
        wasmc_err_t err = pop_expect(ctx->validator, WDEC_TYPE_I32);
        if (err) {
            return err;
        }
    }
    return push_opd(ctx->validator, WDEC_TYPE_I32);
}

/* The numeric instructions: the opcode range was already checked by
 * decode_instr(). */
static wasmc_err_t validate_numeric(wdec_validator_t *validator,
                                    wdec_opcode_t opcode)
{
    const struct numop *num_info = &NUMOPS[opcode - WDEC_OP_I32_EQZ];
    for (unsigned i = 0; i < num_info->pops; i++) {
        wasmc_err_t err = pop_expect(validator, num_info->in);
        if (err) {
            return err;
        }
    }
    return push_opd(validator, num_info->out);
}

static wasmc_err_t validate_instr(const body_ctx *ctx, const wdec_instr_t *ins)
{
    wdec_validator_t *validator = ctx->validator;
    const wdec_opcode_t opcode = ins->opcode;
    if (opcode >= WDEC_OP_I32_LOAD && opcode <= WDEC_OP_I64_STORE32) {
        return validate_memop(ctx, ins);
    }
    switch (opcode) {
    case WDEC_OP_UNREACHABLE:
        mark_unreachable(validator);
        return WASMC_OK;
    case WDEC_OP_NOP:
        return WASMC_OK;
    case WDEC_OP_BLOCK:
    case WDEC_OP_LOOP:
        return push_ctrl(validator, opcode, ins->imm.blocktype);
    case WDEC_OP_IF:
        return validate_if(validator, ins->imm.blocktype);
    case WDEC_OP_ELSE:
        return validate_else(validator);
    case WDEC_OP_END:
        return validate_end(validator);
    case WDEC_OP_BR:
        return validate_br(validator, ins->imm.index);
    case WDEC_OP_BR_IF:
        return validate_br_if(validator, ins->imm.index);
    case WDEC_OP_BR_TABLE:
        return validate_br_table(validator, &ins->imm.br_table);
    case WDEC_OP_RETURN:
        /* a branch to the outermost (function-level) block, which was
         * pushed with label_type == the function's result type */
        return validate_br(validator, validator->ctrl_top - 1);
    case WDEC_OP_CALL:
        return validate_call(ctx, ins->imm.index);
    case WDEC_OP_CALL_INDIRECT:
        return validate_call_indirect(ctx, ins->imm.index);
    case WDEC_OP_DROP: {
        wdec_valtype_t type;
        return pop_opd_any(validator, &type);
    }
    case WDEC_OP_SELECT:
        return validate_select(validator);
    case WDEC_OP_LOCAL_GET:
    case WDEC_OP_LOCAL_SET:
    case WDEC_OP_LOCAL_TEE:
        return validate_local(ctx, ins);
    case WDEC_OP_GLOBAL_GET:
    case WDEC_OP_GLOBAL_SET:
        return validate_global(ctx, ins);
    case WDEC_OP_MEMORY_SIZE:
    case WDEC_OP_MEMORY_GROW:
        return validate_memory_size(ctx, opcode);
    case WDEC_OP_I32_CONST:
        return push_opd(validator, WDEC_TYPE_I32);
    case WDEC_OP_I64_CONST:
        return push_opd(validator, WDEC_TYPE_I64);
    case WDEC_OP_F32_CONST:
        return push_opd(validator, WDEC_TYPE_F32);
    case WDEC_OP_F64_CONST:
        return push_opd(validator, WDEC_TYPE_F64);
    default:
        return validate_numeric(validator, opcode);
    }
}

static wasmc_err_t validate_func_body(const wdec_module_t *mod,
                                      uint32_t funcidx, const wdec_func_t *func,
                                      const wdec_functype_t *ftype,
                                      wdec_validator_t *validator)
{
    (void)funcidx;
    if (ftype->result_count > 1) {
        return WASMC_ERR_TOO_MANY_RESULTS;
    }
    wdec_valtype_t func_result =
        ftype->result_count ? ftype->results[0] : WDEC_TYPE_EMPTY;
    const body_ctx ctx = {mod, func, ftype, validator};

    validator->opd_top = 0;
    validator->ctrl_top = 0;
    wasmc_err_t err = push_ctrl(validator, WDEC_OP_BLOCK, func_result);
    if (err) {
        return err;
    }
    rd_t reader = {func->body, func->body_end};
    while (validator->ctrl_top > 0) {
        if (reader.cur >= reader.end) {
            return WASMC_ERR_TRUNCATED;
        }
        wdec_instr_t ins;
        err = decode_instr(&reader, &ins);
        if (!err) {
            err = validate_instr(&ctx, &ins);
        }
        if (err) {
            return err;
        }
    }
    if (reader.cur != func->body_end) {
        return WASMC_ERR_TRAILING_BYTES;
    }
    if (validator->opd_top != (func_result != WDEC_TYPE_EMPTY ? 1U : 0U)) {
        return WASMC_ERR_END_MISMATCH;
    }
    return WASMC_OK;
}

wasmc_err_t wdec_validate_func(const wdec_module_t *mod, uint32_t funcidx,
                               wdec_validator_t *validator)
{
    if (!mod || !validator) {
        return WASMC_ERR_INVALID_ARG;
    }
    wdec_func_t func;
    wasmc_err_t err = wdec_get_func(mod, funcidx, &func);
    if (err) {
        return err;
    }
    if (func.typeidx >= mod->types.count) {
        return WASMC_ERR_TYPE_INDEX;
    }
    wdec_functype_t ftype;
    err = wdec_get_type(mod, func.typeidx, &ftype);
    if (err) {
        return err;
    }
    return validate_func_body(mod, funcidx, &func, &ftype, validator);
}

/* ------------------------------------------------------------------ */
/* Constant expression semantics (spec 3.3.7)                         */
/* ------------------------------------------------------------------ */

static wasmc_err_t validate_const_expr(const wdec_module_t *mod,
                                       const uint8_t *start, const uint8_t *end,
                                       wdec_valtype_t expected)
{
    rd_t reader = {start, end};
    uint8_t op_byte;
    wasmc_err_t err = rd_u8(&reader, &op_byte);
    if (err) {
        return err;
    }
    wdec_opcode_t opcode = (wdec_opcode_t)op_byte;
    wdec_valtype_t actual;
    switch (opcode) {
    case WDEC_OP_I32_CONST: {
        int32_t val;
        err = rd_i32(&reader, &val);
        if (err) {
            return err;
        }
        actual = WDEC_TYPE_I32;
        break;
    }
    case WDEC_OP_I64_CONST: {
        int64_t val;
        err = rd_i64(&reader, &val);
        if (err) {
            return err;
        }
        actual = WDEC_TYPE_I64;
        break;
    }
    case WDEC_OP_F32_CONST: {
        const uint8_t *ptr;
        err = rd_bytes(&reader, 4, &ptr);
        if (err) {
            return err;
        }
        actual = WDEC_TYPE_F32;
        break;
    }
    case WDEC_OP_F64_CONST: {
        const uint8_t *ptr;
        err = rd_bytes(&reader, sizeof(uint64_t), &ptr);
        if (err) {
            return err;
        }
        actual = WDEC_TYPE_F64;
        break;
    }
    case WDEC_OP_GLOBAL_GET: {
        uint32_t gidx;
        err = rd_u32(&reader, &gidx);
        if (err) {
            return err;
        }
        /* global.get in a const expr may only reference an imported
         * global (spec 3.3.7); locally-defined globals are not yet
         * initialized at the point other const exprs might run. */
        if (gidx >= mod->num_import_globals) {
            return WASMC_ERR_CONST_GLOBAL_NOT_IMPORTED;
        }
        wdec_globaltype_t glob;
        err = wdec_global_type(mod, gidx, &glob);
        if (err) {
            return err;
        }
        if (glob.mut) {
            return WASMC_ERR_CONST_GLOBAL_MUTABLE;
        }
        actual = glob.valtype;
        break;
    }
    default:
        return WASMC_ERR_BAD_CONST_EXPR;
    }
    if (actual != expected) {
        return WASMC_ERR_CONST_EXPR_TYPE;
    }
    return WASMC_OK;
}

/* ------------------------------------------------------------------ */
/* Full module validation                                             */
/* ------------------------------------------------------------------ */

/* 1. types: every entry decodes, as many as declared */
static wasmc_err_t validate_types(const wdec_module_t *mod)
{
    wdec_type_iter_t iter;
    wdec_functype_t ftype;
    uint32_t cnt = 0;
    WDEC_FOR_EACH_TYPE(iter, ftype, mod) {
        cnt++;
    }
    wasmc_err_t err = wdec_type_iter_err(&iter);
    if (err) {
        return err;
    }
    return cnt == mod->types.count ? WASMC_OK : WASMC_ERR_SECTION_SIZE_MISMATCH;
}

static wasmc_err_t validate_import(const wdec_module_t *mod,
                                   const wdec_import_t *imp)
{
    wasmc_err_t err = check_utf8((const uint8_t *)imp->module, imp->module_len);
    if (!err) {
        err = check_utf8((const uint8_t *)imp->name, imp->name_len);
    }
    if (!err && imp->kind == WDEC_EXTKIND_FUNC &&
        imp->desc.typeidx >= mod->types.count) {
        err = WASMC_ERR_TYPE_INDEX;
    }
    return err;
}

/* 2. imports: UTF-8 names, function types in range */
static wasmc_err_t validate_imports(const wdec_module_t *mod)
{
    wdec_import_iter_t iter;
    wdec_import_t imp;
    uint32_t cnt = 0;
    WDEC_FOR_EACH_IMPORT(iter, imp, mod) {
        wasmc_err_t status = validate_import(mod, &imp);
        if (status) {
            return status;
        }
        cnt++;
    }
    wasmc_err_t err = wdec_import_iter_err(&iter);
    if (err) {
        return err;
    }
    return cnt == mod->imports.count ? WASMC_OK
                                     : WASMC_ERR_SECTION_SIZE_MISMATCH;
}

/* 3. function/code sections: one body per function, types in range */
static wasmc_err_t validate_func_section(const wdec_module_t *mod)
{
    if (mod->funcs.count != mod->codes.count) {
        return WASMC_ERR_FUNC_CODE_COUNT_MISMATCH;
    }
    rd_t reader = {mod->funcs.start, mod->funcs.end};
    for (uint32_t i = 0; i < mod->funcs.count; i++) {
        uint32_t typeidx;
        wasmc_err_t err = rd_u32(&reader, &typeidx);
        if (err) {
            return err;
        }
        if (typeidx >= mod->types.count) {
            return WASMC_ERR_TYPE_INDEX;
        }
    }
    return reader.cur == mod->funcs.end ? WASMC_OK
                                        : WASMC_ERR_SECTION_SIZE_MISMATCH;
}

/* 4. tables: at most one */
static wasmc_err_t validate_tables(const wdec_module_t *mod)
{
    if (mod->num_import_tables + mod->tables.count > 1) {
        return WASMC_ERR_TOO_MANY_TABLES;
    }
    wdec_table_iter_t iter;
    wdec_table_t table;
    uint32_t cnt = 0;
    WDEC_FOR_EACH_TABLE(iter, table, mod) {
        cnt++;
    }
    wasmc_err_t err = wdec_table_iter_err(&iter);
    if (err) {
        return err;
    }
    return cnt == mod->tables.count ? WASMC_OK
                                    : WASMC_ERR_SECTION_SIZE_MISMATCH;
}

/* 5. memories: at most one */
static wasmc_err_t validate_memories(const wdec_module_t *mod)
{
    if (mod->num_import_mems + mod->mems.count > 1) {
        return WASMC_ERR_TOO_MANY_MEMORIES;
    }
    wdec_mem_iter_t iter;
    wdec_limits_t lim;
    uint32_t cnt = 0;
    WDEC_FOR_EACH_MEM(iter, lim, mod) {
        cnt++;
    }
    wasmc_err_t err = wdec_mem_iter_err(&iter);
    if (err) {
        return err;
    }
    return cnt == mod->mems.count ? WASMC_OK : WASMC_ERR_SECTION_SIZE_MISMATCH;
}

/* 6. globals: valid initializers */
static wasmc_err_t validate_globals(const wdec_module_t *mod)
{
    wdec_global_iter_t iter;
    wdec_global_t glob;
    uint32_t cnt = 0;
    WDEC_FOR_EACH_GLOBAL(iter, glob, mod) {
        wasmc_err_t status = validate_const_expr(
            mod, glob.init_expr, glob.init_expr_end, glob.valtype);
        if (status) {
            return status;
        }
        cnt++;
    }
    wasmc_err_t err = wdec_global_iter_err(&iter);
    if (err) {
        return err;
    }
    return cnt == mod->globals.count ? WASMC_OK
                                     : WASMC_ERR_SECTION_SIZE_MISMATCH;
}

/* the index of an export is in range for its kind */
static wasmc_err_t validate_export_index(const wdec_module_t *mod,
                                         const wdec_export_t *exp)
{
    switch (exp->kind) {
    case WDEC_EXTKIND_FUNC:
        return exp->index < wdec_func_count(mod) ? WASMC_OK
                                                 : WASMC_ERR_FUNC_INDEX;
    case WDEC_EXTKIND_TABLE:
        return exp->index < mod->num_import_tables + mod->tables.count
                   ? WASMC_OK
                   : WASMC_ERR_TABLE_INDEX;
    case WDEC_EXTKIND_MEM:
        return exp->index < mod->num_import_mems + mod->mems.count
                   ? WASMC_OK
                   : WASMC_ERR_MEM_INDEX;
    case WDEC_EXTKIND_GLOBAL:
        return exp->index < mod->num_import_globals + mod->globals.count
                   ? WASMC_OK
                   : WASMC_ERR_GLOBAL_INDEX;
    default:
        return WASMC_ERR_BAD_EXPORT_KIND;
    }
}

/*
 * All export names must be pairwise distinct (spec 2.5.10): `exp`, export
 * number `count`, against the ones before it. With no allocation available
 * for a hash set, this is a deliberate O(n^2) name comparison over exports
 * (typically a small set on an embedded module) rather than any extra
 * memory use.
 */
static wasmc_err_t check_export_name_unique(const wdec_module_t *mod,
                                            const wdec_export_t *exp,
                                            uint32_t count)
{
    wdec_export_iter_t iter;
    wdec_export_iter_init(&iter, mod);
    wdec_export_t prev;
    for (uint32_t i = 0; i < count && wdec_export_iter_next(&iter, &prev);
         i++) {
        /* (an empty name may come with a NULL pointer: no memcmp) */
        if (prev.name_len == exp->name_len &&
            (!exp->name_len ||
             memcmp(prev.name, exp->name, exp->name_len) == 0)) {
            return WASMC_ERR_DUPLICATE_EXPORT_NAME;
        }
    }
    return WASMC_OK;
}

/* 7. exports: UTF-8 names, indices in range, names unique */
static wasmc_err_t validate_exports(const wdec_module_t *mod)
{
    wdec_export_iter_t iter;
    wdec_export_t exp;
    uint32_t idx = 0;
    WDEC_FOR_EACH_EXPORT(iter, exp, mod) {
        wasmc_err_t status =
            check_utf8((const uint8_t *)exp.name, exp.name_len);
        if (!status) {
            status = validate_export_index(mod, &exp);
        }
        if (!status) {
            status = check_export_name_unique(mod, &exp, idx);
        }
        if (status) {
            return status;
        }
        idx++;
    }
    wasmc_err_t err = wdec_export_iter_err(&iter);
    if (err) {
        return err;
    }
    return idx == mod->exports.count ? WASMC_OK
                                     : WASMC_ERR_SECTION_SIZE_MISMATCH;
}

/* 8. start function: in range, [] -> [] */
static wasmc_err_t validate_start(const wdec_module_t *mod)
{
    if (!mod->has_start) {
        return WASMC_OK;
    }
    if (mod->start_funcidx >= wdec_func_count(mod)) {
        return WASMC_ERR_FUNC_INDEX;
    }
    wdec_functype_t ftype;
    wasmc_err_t err = wdec_func_type(mod, mod->start_funcidx, &ftype);
    if (err) {
        return err;
    }
    if (ftype.param_count != 0 || ftype.result_count != 0) {
        return WASMC_ERR_START_FUNC_SIGNATURE;
    }
    return WASMC_OK;
}

static wasmc_err_t validate_elem(const wdec_module_t *mod,
                                 const wdec_elem_t *elem)
{
    if (mod->num_import_tables + mod->tables.count == 0) {
        return WASMC_ERR_ELEM_TABLE_REQUIRED;
    }
    if (elem->tableidx != 0) {
        return WASMC_ERR_TABLE_INDEX;
    }
    wasmc_err_t err = validate_const_expr(mod, elem->offset_expr,
                                          elem->offset_expr_end, WDEC_TYPE_I32);
    if (err) {
        return err;
    }
    wdec_elem_funcidx_iter_t iter;
    uint32_t fidx;
    WDEC_FOR_EACH_ELEM_FUNCIDX(iter, fidx, elem) {
        if (fidx >= wdec_func_count(mod)) {
            return WASMC_ERR_FUNC_INDEX;
        }
    }
    return wdec_elem_funcidx_iter_err(&iter);
}

/* 9. element segments: table 0, i32 offsets, functions in range */
static wasmc_err_t validate_elems(const wdec_module_t *mod)
{
    wdec_elem_iter_t iter;
    wdec_elem_t elem;
    uint32_t cnt = 0;
    WDEC_FOR_EACH_ELEM(iter, elem, mod) {
        wasmc_err_t status = validate_elem(mod, &elem);
        if (status) {
            return status;
        }
        cnt++;
    }
    wasmc_err_t err = wdec_elem_iter_err(&iter);
    if (err) {
        return err;
    }
    return cnt == mod->elems.count ? WASMC_OK : WASMC_ERR_SECTION_SIZE_MISMATCH;
}

static wasmc_err_t validate_data(const wdec_module_t *mod,
                                 const wdec_data_t *data)
{
    if (mod->num_import_mems + mod->mems.count == 0) {
        return WASMC_ERR_DATA_MEM_REQUIRED;
    }
    if (data->memidx != 0) {
        return WASMC_ERR_MEM_INDEX;
    }
    return validate_const_expr(mod, data->offset_expr, data->offset_expr_end,
                               WDEC_TYPE_I32);
}

/* 10. data segments: memory 0, i32 offsets */
static wasmc_err_t validate_datas(const wdec_module_t *mod)
{
    wdec_data_iter_t iter;
    wdec_data_t data;
    uint32_t cnt = 0;
    WDEC_FOR_EACH_DATA(iter, data, mod) {
        wasmc_err_t status = validate_data(mod, &data);
        if (status) {
            return status;
        }
        cnt++;
    }
    wasmc_err_t err = wdec_data_iter_err(&iter);
    if (err) {
        return err;
    }
    return cnt == mod->data.count ? WASMC_OK : WASMC_ERR_SECTION_SIZE_MISMATCH;
}

static wasmc_err_t validate_body(const wdec_module_t *mod, uint32_t funcidx,
                                 const wdec_func_t *func,
                                 wdec_validator_t *validator)
{
    if (func->typeidx >= mod->types.count) {
        return WASMC_ERR_TYPE_INDEX;
    }
    wdec_functype_t ftype;
    wasmc_err_t err = wdec_get_type(mod, func->typeidx, &ftype);
    if (err) {
        return err;
    }
    return validate_func_body(mod, funcidx, func, &ftype, validator);
}

/*
 * 11. function bodies: stack-machine validation, spec section 7.3. Uses
 * the sequential func+code iterator (not indexed lookups) so this whole
 * pass, like every other step, is O(module size) rather than
 * O(functions^2). The caller-supplied validator buffers are reused for
 * every function. On an error, *bad_funcidx names the function.
 */
static wasmc_err_t validate_bodies(const wdec_module_t *mod,
                                   wdec_validator_t *validator,
                                   uint32_t *bad_funcidx)
{
    wdec_func_iter_t iter;
    wdec_func_t func;
    uint32_t funcidx = mod->num_import_funcs;
    uint32_t cnt = 0;
    WDEC_FOR_EACH_FUNC(iter, func, mod) {
        wasmc_err_t status = validate_body(mod, funcidx, &func, validator);
        if (status) {
            *bad_funcidx = funcidx;
            return status;
        }
        funcidx++;
        cnt++;
    }
    wasmc_err_t err = wdec_func_iter_err(&iter);
    if (err) {
        *bad_funcidx = funcidx;
        return err;
    }
    return cnt == mod->funcs.count ? WASMC_OK : WASMC_ERR_SECTION_SIZE_MISMATCH;
}

wasmc_err_t wdec_validate(const wdec_module_t *mod, wdec_validator_t *validator,
                          uint32_t *out_bad_funcidx)
{
    static wasmc_err_t (*const steps[])(const wdec_module_t *) = {
        validate_types,   validate_imports,  validate_func_section,
        validate_tables,  validate_memories, validate_globals,
        validate_exports, validate_start,    validate_elems,
        validate_datas,
    };
    if (!mod || !validator) {
        return WASMC_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        wasmc_err_t err = steps[i](mod);
        if (err) {
            return err;
        }
    }
    uint32_t bad_funcidx = 0;
    wasmc_err_t err = validate_bodies(mod, validator, &bad_funcidx);
    if (err && out_bad_funcidx) {
        *out_bad_funcidx = bad_funcidx;
    }
    return err;
}
