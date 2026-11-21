/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/* aotw.c - see aotw.h and aotfmt/aotfmt.tex */
#include "aotw.h"
#include "leb128.h"
#include "rv32.h"
#include "wamr_abi.h"
#include "wasm_decode.h"
#include "wasmc/error.h"
#include "wasmc/wasmc.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Output primitives. Every multi-byte field is little endian and naturally */
/* aligned in the file (u64: 4 bytes) - aotfmt.tex "Data Representation".  */
/* ------------------------------------------------------------------------ */

/* Relocation record kept at the buffer tail, in the final format of a
 * 32-bit relocation: offset, addend, type, symbol index. */
#define RELOC_SIZE 16u

/* the first size of a growing buffer, and its smallest step */
#define OUT_MIN 1024u
/* each step adds 1/2^OUT_STEP_SHIFT of the size: the buffer ends at most
 * 12.5% above the file (doubling left keccak's 46 KB in a 64 KB buffer, on
 * top of the work memory at the compile's peak) */
#define OUT_STEP_SHIFT 3u

/* bytes of the relocation records staged at the tail */
static uint32_t staged_relocs(const aotw_t *writer)
{
    return (writer->cap & ~3U) - writer->tail;
}

/* Resizes the buffer (wasmc_allocator.out_grow) to `size` bytes; the staged
 * relocation records move to its new end. False if out_grow() fails (the
 * buffer is then as it was). */
static bool resize(aotw_t *writer, uint32_t size)
{
    const uint32_t relocs = staged_relocs(writer);
    const wasmc_allocator *mem = writer->mem;
    uint8_t *buf = mem->out_grow(mem->ctx, writer->buf, writer->cap, size);
    if (!buf) {
        return false;
    }
    memmove(buf + size - relocs, buf + writer->tail, relocs);
    writer->buf = buf;
    writer->cap = size;
    writer->tail = size - relocs;
    return true;
}

/* The size to grow the buffer to for `n` more bytes after pos: by an eighth
 * of its size (OUT_MIN at least), or more if `n` needs it. Sets `*want` to
 * the size `n` needs. 0 if that does not fit 32 bits. */
static uint32_t grown_size(const aotw_t *writer, uint32_t n, uint32_t *want)
{
    const uint64_t need = (((uint64_t)writer->pos + n + 3U) & ~(uint64_t)3U) +
                          staged_relocs(writer);
    if (need > (UINT32_MAX & ~3U)) {
        return 0;
    }
    *want = (uint32_t)need;
    uint64_t step = writer->cap >> OUT_STEP_SHIFT;
    uint64_t size =
        ((uint64_t)writer->cap + (step < OUT_MIN ? OUT_MIN : step)) & ~3ULL;
    if (size < need || size > (UINT32_MAX & ~3U)) {
        size = need;
    }
    return (uint32_t)size;
}

/* Grows the buffer so that `n` more bytes fit between pos and the staged
 * relocation records: by an eighth of its size (OUT_MIN at least), or just
 * enough if that fails. False, and the error set, if out of memory. */
static bool grow(aotw_t *writer, uint32_t n)
{
    uint32_t want = 0;
    uint32_t size = grown_size(writer, n, &want);
    if (!size ||
        (!resize(writer, size) && (size == want || !resize(writer, want)))) {
        writer->err = WASMC_ERR_OUT_OF_MEMORY;
        return false;
    }
    return true;
}

void aotw_reserve(aotw_t *writer, uint32_t n)
{
    uint32_t want = 0;
    if (writer->err || writer->dry) {
        return;
    }
    uint32_t size = grown_size(writer, n, &want);
    if (size && want > writer->cap) {
        (void)resize(writer, size); /* a hint: growth on demand still works */
    }
}

/* Whether the n bytes at pos can be stored, the buffer grown if needed;
 * false (out of memory, the error set) if they cannot. */
static bool room(aotw_t *writer, uint32_t n)
{
    if (n > writer->tail || writer->pos > writer->tail - n) {
        return grow(writer, n);
    }
    return true;
}

static void put8(aotw_t *writer, uint32_t val)
{
    if (writer->err) {
        return;
    }
    if (room(writer, 1)) {
        writer->buf[writer->pos] = (uint8_t)val;
    }
    writer->pos++;
}

static void align(aotw_t *writer, uint32_t alignment)
{
    while ((writer->pos & (alignment - 1)) && !writer->err) {
        put8(writer, 0);
    }
}

static void put16(aotw_t *writer, uint32_t val)
{
    align(writer, 2);
    if (writer->err) {
        return;
    }
    if (room(writer, 2)) {
        writer->buf[writer->pos] = (uint8_t)val;
        writer->buf[writer->pos + 1] = (uint8_t)(val >> CHAR_BIT);
    }
    writer->pos += 2;
}

static void put32_at(uint8_t *ptr, uint32_t val)
{
    for (unsigned i = 0; i < sizeof val; i++) {
        ptr[i] = (uint8_t)(val >> (CHAR_BIT * i));
    }
}

/* the little-endian unsigned integer in the `size` bytes at ptr, reading
 * no further than end */
static uint64_t get_le(const uint8_t *ptr, const uint8_t *end, unsigned size)
{
    uint64_t val = 0;
    for (unsigned i = 0; i < size && ptr + i < end; i++) {
        val |= (uint64_t)ptr[i] << (CHAR_BIT * i);
    }
    return val;
}

static void put32(aotw_t *writer, uint32_t val)
{
    align(writer, 4);
    if (writer->err) {
        return;
    }
    if (room(writer, 4)) {
        put32_at(writer->buf + writer->pos, val);
    }
    writer->pos += 4;
}

static void put64(aotw_t *writer, uint64_t val) /* only 4-byte aligned */
{
    put32(writer, (uint32_t)val);
    put32(writer, (uint32_t)(val >> (CHAR_BIT * sizeof(uint32_t))));
}

static void put_bytes(aotw_t *writer, const void *ptr, uint32_t n)
{
    if (writer->err) {
        return;
    }
    if (room(writer, n) && n) {
        memcpy(writer->buf + writer->pos, ptr, n);
    }
    writer->pos += n;
}

/* longest string written, NUL included */
#define STR_MAX_SIZE 0x7FFFU

/* String: u16 length including the final NUL, the bytes, NUL. */
static void put_str(aotw_t *writer, const char *str, uint32_t len)
{
    if (len + 1U > STR_MAX_SIZE) {
        if (!writer->err) {
            writer->err =
                WASMC_ERR_UNSUPPORTED; /* longer than the writer allows */
        }
        return;
    }
    put16(writer, len + 1U);
    put_bytes(writer, str, len);
    put8(writer, 0);
}

/* Section header; returns the offset of the size field. */
static uint32_t section_begin(aotw_t *writer, uint32_t ident)
{
    put32(writer, ident);
    uint32_t pos = writer->pos;
    put32(writer, 0);
    return pos;
}

static void section_end(aotw_t *writer, uint32_t size_at)
{
    if (!writer->err) {
        put32_at(writer->buf + size_at, writer->pos - (size_at + 4U));
    }
}

/* ------------------------------------------------------------------------ */
/* WASM constant expressions (raw bytes: opcode, immediate, 0x0B)           */
/* ------------------------------------------------------------------------ */

typedef struct {
    uint8_t op; /* WDEC_OP_I32_CONST, .._I64_CONST, .._F32_CONST, .._F64_CONST,
                   .._GLOBAL_GET; 0 if anything else */
    uint64_t bits; /* value bits, or the global index */
} cexpr_t;

static uint64_t leb(const uint8_t **pptr, const uint8_t *end, bool is_signed,
                    unsigned bits)
{
    const uint8_t *ptr = *pptr;
    uint64_t res = 0;
    unsigned shift = 0;
    uint8_t byte = 0;
    while (ptr < end) {
        byte = *ptr++;
        if (shift < LEB_BITS64) {
            res |= (uint64_t)(byte & LEB_PAYLOAD_MASK) << shift;
        }
        shift += LEB_PAYLOAD_BITS;
        if (!(byte & LEB_MORE)) {
            break;
        }
    }
    if (is_signed && shift < LEB_BITS64 && (byte & LEB_SIGN)) {
        res |= ~(uint64_t)0 << shift;
    }
    if (bits < LEB_BITS64) {
        res &= (((uint64_t)1 << bits) - 1U);
    }
    *pptr = ptr;
    return res;
}

static cexpr_t cexpr(const uint8_t *ptr, const uint8_t *end)
{
    cexpr_t expr = {0, 0};
    if (ptr >= end) {
        return expr;
    }
    expr.op = *ptr++;
    switch (expr.op) {
    case WDEC_OP_I32_CONST:
        expr.bits = leb(&ptr, end, true, LEB_BITS32);
        break;
    case WDEC_OP_I64_CONST:
        expr.bits = leb(&ptr, end, true, LEB_BITS64);
        break;
    case WDEC_OP_GLOBAL_GET:
        expr.bits = leb(&ptr, end, false, LEB_BITS32);
        break;
    case WDEC_OP_F32_CONST:
        expr.bits = get_le(ptr, end, sizeof(uint32_t));
        break;
    case WDEC_OP_F64_CONST:
        expr.bits = get_le(ptr, end, sizeof(uint64_t));
        break;
    default:
        expr.op = 0;
        break;
    }
    return expr;
}

/* InitExpr of the AOT format (aotfmt.tex "Initializer Expressions"). */
static void put_init_expr(aotw_t *writer, cexpr_t expr)
{
    switch (expr.op) {
    case WDEC_OP_I32_CONST:
        put32(writer, AOT_INIT_EXPR_I32_CONST);
        put32(writer, (uint32_t)expr.bits);
        break;
    case WDEC_OP_F32_CONST:
        put32(writer, AOT_INIT_EXPR_F32_CONST);
        put32(writer, (uint32_t)expr.bits);
        break;
    case WDEC_OP_I64_CONST:
        put32(writer, AOT_INIT_EXPR_I64_CONST);
        put64(writer, expr.bits);
        break;
    case WDEC_OP_F64_CONST:
        put32(writer, AOT_INIT_EXPR_F64_CONST);
        put64(writer, expr.bits);
        break;
    case WDEC_OP_GLOBAL_GET:
        put32(writer, AOT_INIT_EXPR_GET_GLOBAL);
        put32(writer, (uint32_t)expr.bits);
        break;
    default:
        if (!writer->err) {
            writer->err = WASMC_ERR_BAD_CONST_EXPR;
        }
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* Module data                                                              */
/* ------------------------------------------------------------------------ */

static uint32_t count_imports(const wdec_module_t *mod, uint8_t kind)
{
    uint32_t count = 0;
    wdec_import_t imp;
    wdec_import_iter_t imp_it;
    WDEC_FOR_EACH_IMPORT(imp_it, imp, mod) {
        if (imp.kind == kind) {
            count++;
        }
    }
    return count; /* (the module is validated: no decoding error) */
}

#define MOD_DATA_ALIGN 8U /* keeps the arena before it aligned */

uint32_t aotw_module_data_size(const wdec_module_t *mod)
{
    uint32_t nglob =
        count_imports(mod, WDEC_IMPKIND_GLOBAL) + wdec_global_count(mod);
    uint32_t count = (wdec_defined_func_count(mod) * 4U) + (nglob * 4U) +
                     (wdec_type_count(mod) * 2U);
    return (count + MOD_DATA_ALIGN - 1U) & ~(MOD_DATA_ALIGN - 1U);
}

static int same_type(const wdec_functype_t *lhs, const wdec_functype_t *rhs)
{
    return lhs->param_count == rhs->param_count &&
           lhs->result_count == rhs->result_count &&
           (!lhs->param_count ||
            !memcmp(lhs->params, rhs->params, lhs->param_count)) &&
           (!lhs->result_count ||
            !memcmp(lhs->results, rhs->results, lhs->result_count));
}

/* The next type of `iter`, the error if there is none (a type section
 * shorter than its count: WASMC_ERR_TYPE_INDEX, as wdec_get_type()). */
static wasmc_err_t next_type(wdec_type_iter_t *iter, wdec_functype_t *type)
{
    if (wdec_type_iter_next(iter, type)) {
        return WASMC_OK;
    }
    const wasmc_err_t err = wdec_type_iter_err(iter);
    return err ? err : WASMC_ERR_TYPE_INDEX;
}

/* canon_type[i] = smallest j with type j identical to type i (aotfmt.tex
 * "Indirect calls": call_indirect compares type indices). Each type is
 * compared with the ones before it, which an iterator restarted from the
 * first type walks in order: O(n^2) comparisons, no memory (wdec_get_type()
 * in both loops rescanned the section from the start for each lookup, n^3/6
 * type decodes: 4000 types took 200 s). */
static wasmc_err_t canonical_types(aotw_t *writer)
{
    const uint32_t count = wdec_type_count(writer->m);
    wdec_type_iter_t outer;
    wdec_type_iter_init(&outer, writer->m);
    for (uint32_t i = 0; i < count; i++) {
        wdec_functype_t type_i;
        wasmc_err_t err = next_type(&outer, &type_i);
        if (err) {
            return err;
        }
        writer->canon_type[i] = (uint16_t)i;
        wdec_type_iter_t inner;
        wdec_type_iter_init(&inner, writer->m);
        for (uint32_t j = 0; j < i; j++) {
            wdec_functype_t type_j;
            err = next_type(&inner, &type_j);
            if (err) {
                return err;
            }
            if (same_type(&type_i, &type_j)) {
                writer->canon_type[i] = writer->canon_type[j];
                break;
            }
        }
    }
    return WASMC_OK;
}

static uint32_t valtype_cells(wdec_valtype_t type)
{
    return (type == WDEC_TYPE_I64 || type == WDEC_TYPE_F64) ? 2U : 1U;
}

/* ------------------------------------------------------------------------ */
/* Init data section                                                        */
/* ------------------------------------------------------------------------ */

/* wamrc rounds __heap_base up to this for the memory shrink */
#define HEAP_BASE_ALIGN 8U

/*
 * Memory type. When no function contains memory.size or memory.grow, the
 * memory is rewritten as wamrc does (WAMR's loader; aotfmt.tex "Memory Shrink
 * Optimization"), in two steps:
 *   1. if the module's memory layout was found (@shrink.ok) and
 *      @shrink.size, __heap_base aligned up to 8, is within the initial
 *      memory: one page of @shrink.size bytes (just the data and the stack);
 *   2. if fewer than 65536 initial pages: one page of the whole initial size
 *      (none if the memory is empty), and that is also the maximum.
 * The module cannot see the page size without those instructions, and the
 * runtime then allocates only what the module can reach.
 */
typedef struct {
    bool ok;       /* the memory layout was found */
    uint64_t size; /* __heap_base aligned up to HEAP_BASE_ALIGN */
} mem_shrink;

static void put_memory(aotw_t *writer, const wdec_limits_t *lim,
                       mem_shrink shrink)
{
    uint64_t page = AOT_DEFAULT_PAGE_SIZE;
    uint32_t init = lim->min;
    uint32_t max = lim->has_max ? lim->max : AOT_DEFAULT_MAX_PAGES;
    if (max > AOT_DEFAULT_MAX_PAGES) {
        max = AOT_DEFAULT_MAX_PAGES;
    }
    if (!writer->has_mem_size && !writer->has_mem_grow) {
        if (shrink.ok && shrink.size <= UINT32_MAX &&
            shrink.size <= page * init) {
            page = shrink.size;
            init = 1;
        }
        if (init < AOT_DEFAULT_MAX_PAGES) {
            page *= init;
            init = max = init ? 1U : 0U;
        }
    }
    put32(writer, lim->has_max ? AOT_MEMORY_HAS_MAX : 0);
    put32(writer, (uint32_t)page);
    put32(writer, init);
    put32(writer, max);
    /* the smallest size it can have once not empty: the initial one, or one
     * page if it starts empty (and grows) */
    uint64_t size = init ? page * init : page;
    const uint64_t bound = size ? size - 1U : 0U;
    writer->mem_bound_min = bound > UINT32_MAX ? UINT32_MAX : (uint32_t)bound;
}

/* Table type; the maximum is adjusted like wamrc does (aotfmt.tex "Tables"). */
static void put_table(aotw_t *writer, uint8_t elemtype,
                      const wdec_limits_t *lim)
{
    uint32_t init = lim->min;
    uint32_t dist;
    uint32_t max;
    dist = init < UINT32_MAX / 2U ? init * 2U : UINT32_MAX;
    if (dist < AOT_TABLE_MIN_MAX_SIZE) {
        dist = AOT_TABLE_MIN_MAX_SIZE;
    }
    if (!lim->has_max) {
        max = dist;
    } else {
        max = (init < lim->max && lim->max > dist) ? dist : lim->max;
    }
    put8(writer, elemtype);
    put8(writer, lim->has_max ? AOT_TABLE_HAS_MAX : 0);
    put8(writer, 0); /* possible_grow: no table.grow in WASM 1.0 */
    put8(writer, 0); /* padding (nullable in GC mode) */
    put32(writer, init);
    put32(writer, max);
}

/* wamrc rounds __data_end up to this */
#define DATA_END_ALIGN 16U

static int export_is(const wdec_export_t *exp, const char *name)
{
    return exp->name_len == strlen(name) &&
           !memcmp(exp->name, name, exp->name_len);
}

/* how many imports there are of each kind */
typedef struct {
    uint32_t func;
    uint32_t table;
    uint32_t mem;
    uint32_t global;
} import_counts;

static wasmc_err_t count_import_kinds(const wdec_module_t *mod,
                                      import_counts *out)
{
    wdec_import_t imp;
    wdec_import_iter_t imp_it;
    memset(out, 0, sizeof *out);
    WDEC_FOR_EACH_IMPORT(imp_it, imp, mod) {
        switch (imp.kind) {
        case WDEC_IMPKIND_FUNC:
            out->func++;
            break;
        case WDEC_IMPKIND_TABLE:
            out->table++;
            break;
        case WDEC_IMPKIND_MEM:
            out->mem++;
            break;
        default:
            out->global++;
            break;
        }
    }
    return wdec_import_iter_err(&imp_it);
}

/* data segments: is_passive/memory index are placeholders (no bulk memory) */
static wasmc_err_t write_data_segments(aotw_t *writer)
{
    const wdec_module_t *mod = writer->m;
    wdec_data_t data;
    wdec_data_iter_t data_it;
    put32(writer, wdec_data_count(mod));
    WDEC_FOR_EACH_DATA(data_it, data, mod) {
        put32(writer, 0);
        put32(writer, 0);
        put_init_expr(writer, cexpr(data.offset_expr, data.offset_expr_end));
        put32(writer, data.data_len);
        put_bytes(writer, data.data, data.data_len);
    }
    return wdec_data_iter_err(&data_it);
}

/* the memories (an imported memory is folded into the list), then the data
 * segments */
/* @shrink: for put_memory() */
static wasmc_err_t write_memory_info(aotw_t *writer, uint32_t nimp_mem,
                                     mem_shrink shrink)
{
    const wdec_module_t *mod = writer->m;
    wasmc_err_t err;
    writer->mem_bound_min = 0;
    put32(writer, 0); /* import memory count: always 0 */
    uint32_t nmem = nimp_mem + wdec_mem_count(mod);
    writer->memory_count = nmem ? nmem : 1U;
    writer->init_pages_zero = 1;
    put32(writer, nmem ? nmem : 1U);
    if (!nmem) {
        put32(writer, 0);
        put32(writer, AOT_DEFAULT_PAGE_SIZE);
        put32(writer, 0);
        put32(writer, 0);
    }
    wdec_import_t imp;
    wdec_import_iter_t imp_it;
    WDEC_FOR_EACH_IMPORT(imp_it, imp, mod) {
        if (imp.kind == WDEC_IMPKIND_MEM) {
            put_memory(writer, &imp.desc.mem, shrink);
            writer->init_pages_zero = imp.desc.mem.min == 0;
        }
    }
    err = wdec_import_iter_err(&imp_it);
    if (err) {
        return err;
    }
    wdec_limits_t lim;
    wdec_mem_iter_t mem_it;
    WDEC_FOR_EACH_MEM(mem_it, lim, mod) {
        if (!nimp_mem) {
            writer->init_pages_zero = lim.min == 0;
        }
        put_memory(writer, &lim, shrink);
    }
    err = wdec_mem_iter_err(&mem_it);
    if (err) {
        return err;
    }
    return write_data_segments(writer);
}

static wasmc_err_t write_elem_segments(aotw_t *writer)
{
    const wdec_module_t *mod = writer->m;
    wdec_elem_t elem;
    wdec_elem_iter_t elem_it;
    put32(writer, wdec_elem_count(mod));
    WDEC_FOR_EACH_ELEM(elem_it, elem, mod) {
        put32(writer, 0);                   /* mode: active */
        put32(writer, AOT_VALTYPE_FUNCREF); /* element type */
        put32(writer, elem.tableidx);
        put_init_expr(writer, cexpr(elem.offset_expr, elem.offset_expr_end));
        /* reference type placeholder (GC only) */
        put16(writer, AOT_VALTYPE_FUNCREF);
        put16(writer, 0);
        put32(writer, 0);
        put32(writer, elem.func_count);
        uint32_t fidx;
        wdec_elem_funcidx_iter_t fit;
        WDEC_FOR_EACH_ELEM_FUNCIDX(fit, fidx, &elem) {
            put32(writer, AOT_INIT_EXPR_FUNCREF_CONST);
            put32(writer, fidx);
        }
        const wasmc_err_t err = wdec_elem_funcidx_iter_err(&fit);
        if (err) {
            return err;
        }
    }
    return wdec_elem_iter_err(&elem_it);
}

/* the imported tables, the tables, then the elem segments */
static wasmc_err_t write_table_info(aotw_t *writer, uint32_t nimp_tab)
{
    const wdec_module_t *mod = writer->m;
    put32(writer, nimp_tab);
    wdec_import_t imp;
    wdec_import_iter_t imp_it;
    WDEC_FOR_EACH_IMPORT(imp_it, imp, mod) {
        if (imp.kind == WDEC_IMPKIND_TABLE) {
            put_table(writer, imp.desc.table.elemtype, &imp.desc.table.limits);
        }
    }
    wasmc_err_t err = wdec_import_iter_err(&imp_it);
    if (err) {
        return err;
    }
    put32(writer, wdec_table_count(mod));
    wdec_table_t table;
    wdec_table_iter_t table_it;
    WDEC_FOR_EACH_TABLE(table_it, table, mod) {
        put_table(writer, table.elemtype, &table.limits);
    }
    err = wdec_table_iter_err(&table_it);
    if (err) {
        return err;
    }
    return write_elem_segments(writer);
}

static wasmc_err_t write_types(aotw_t *writer)
{
    const wdec_module_t *mod = writer->m;
    wdec_functype_t ftype;
    wdec_type_iter_t type_it;
    put32(writer, wdec_type_count(mod));
    WDEC_FOR_EACH_TYPE(type_it, ftype, mod) {
        align(writer, 4);
        put16(writer, 0); /* WASM_TYPE_FUNC */
        put16(writer, ftype.param_count);
        put16(writer, ftype.result_count);
        put_bytes(writer, ftype.params, ftype.param_count);
        put_bytes(writer, ftype.results, ftype.result_count);
    }
    return wdec_type_iter_err(&type_it);
}

static wasmc_err_t write_import_globals(aotw_t *writer, uint32_t nimp_glob)
{
    wdec_import_t imp;
    wdec_import_iter_t imp_it;
    put32(writer, nimp_glob);
    WDEC_FOR_EACH_IMPORT(imp_it, imp, writer->m) {
        if (imp.kind != WDEC_IMPKIND_GLOBAL) {
            continue;
        }
        align(writer, 2);
        put8(writer, imp.desc.global.valtype);
        put8(writer, imp.desc.global.mut);
        put_str(writer, imp.module, imp.module_len);
        put_str(writer, imp.name, imp.name_len);
    }
    return wdec_import_iter_err(&imp_it);
}

static wasmc_err_t write_globals(aotw_t *writer)
{
    const wdec_module_t *mod = writer->m;
    wdec_global_t glob;
    wdec_global_iter_t glob_it;
    put32(writer, wdec_global_count(mod));
    WDEC_FOR_EACH_GLOBAL(glob_it, glob, mod) {
        align(writer, 4);
        put8(writer, glob.valtype);
        put8(writer, glob.mut);
        put_init_expr(
            writer, cexpr(glob.init_expr, glob.init_expr_end)); /* 4-aligned */
    }
    return wdec_global_iter_err(&glob_it);
}

static wasmc_err_t write_import_funcs(aotw_t *writer, uint32_t nimp_func)
{
    wdec_import_t imp;
    wdec_import_iter_t imp_it;
    put32(writer, nimp_func);
    WDEC_FOR_EACH_IMPORT(imp_it, imp, writer->m) {
        if (imp.kind != WDEC_IMPKIND_FUNC) {
            continue;
        }
        put16(writer, writer->canon_type[imp.desc.typeidx]);
        put_str(writer, imp.module, imp.module_len);
        put_str(writer, imp.name, imp.name_len);
    }
    return wdec_import_iter_err(&imp_it);
}

/*
 * The linear memory layout of a module built by a C toolchain, detected as
 * wamrc does (aotfmt.tex "Auxiliary Subsection"): exported, defined,
 * immutable i32 globals __data_end and __heap_base initialized with
 * i32.const; the stack pointer is the first defined mutable i32 global with
 * an i32.const <= heap base.
 */
typedef struct {
    uint32_t data_end_global;
    uint32_t heap_base_global;
    uint32_t stack_global;
    uint32_t stack_size;
    uint64_t data_end;
    uint64_t heap_base;
    uint64_t stack_base;
    bool have_data_end;
    bool have_heap_base;
    bool found;
} aux_layout;

/* Notes an exported __heap_base or __data_end of the expected form. */
static wasmc_err_t note_layout_export(const wdec_module_t *mod,
                                      uint32_t nimp_glob,
                                      const wdec_export_t *exp,
                                      aux_layout *layout)
{
    int is_hb = export_is(exp, "__heap_base");
    int is_de = export_is(exp, "__data_end");
    if (!(is_hb || is_de) || exp->index < nimp_glob) {
        return WASMC_OK;
    }
    wdec_global_t glob;
    wasmc_err_t err = wdec_get_global(mod, exp->index - nimp_glob, &glob);
    if (err) {
        return err;
    }
    cexpr_t expr = cexpr(glob.init_expr, glob.init_expr_end);
    if (glob.valtype != WDEC_TYPE_I32 || glob.mut ||
        expr.op != WDEC_OP_I32_CONST) {
        return WASMC_OK;
    }
    if (is_hb) {
        layout->heap_base_global = exp->index;
        layout->heap_base = (uint32_t)expr.bits;
        layout->have_heap_base = true;
    } else {
        layout->data_end_global = exp->index;
        layout->data_end =
            ((uint64_t)(uint32_t)expr.bits + DATA_END_ALIGN - 1U) &
            ~(uint64_t)(DATA_END_ALIGN - 1U);
        layout->have_data_end = true;
    }
    return WASMC_OK;
}

/* The stack pointer global; the heap base global (an empty stack) if there
 * is none. */
static wasmc_err_t find_stack_pointer(const wdec_module_t *mod,
                                      uint32_t nimp_glob, aux_layout *layout)
{
    wdec_global_t glob;
    wdec_global_iter_t glob_it;
    uint32_t gidx = 0;
    WDEC_FOR_EACH_GLOBAL(glob_it, glob, mod) {
        cexpr_t expr = cexpr(glob.init_expr, glob.init_expr_end);
        if (glob.mut && glob.valtype == WDEC_TYPE_I32 &&
            expr.op == WDEC_OP_I32_CONST &&
            (uint32_t)expr.bits <= layout->heap_base) {
            layout->stack_global = nimp_glob + gidx;
            layout->stack_base = (uint32_t)expr.bits;
            layout->stack_size =
                layout->stack_base > layout->data_end
                    ? (uint32_t)(layout->stack_base - layout->data_end)
                    : (uint32_t)layout->stack_base;
            return WASMC_OK;
        }
        gidx++;
    }
    wasmc_err_t err = wdec_global_iter_err(&glob_it);
    if (!err) {
        layout->stack_global = layout->heap_base_global;
        layout->stack_base = layout->heap_base;
        layout->stack_size = 0;
    }
    return err;
}

static wasmc_err_t find_aux_layout(const wdec_module_t *mod, uint32_t nimp_glob,
                                   aux_layout *layout)
{
    wasmc_err_t err = WASMC_OK;
    wdec_export_t exp;
    wdec_export_iter_t exp_it;
    memset(layout, 0, sizeof *layout);
    layout->data_end_global = AOT_NO_INDEX;
    layout->heap_base_global = AOT_NO_INDEX;
    layout->stack_global = AOT_NO_INDEX;
    WDEC_FOR_EACH_EXPORT(exp_it, exp, mod) {
        if (exp.kind != WDEC_EXTKIND_GLOBAL) {
            continue;
        }
        err = note_layout_export(mod, nimp_glob, &exp, layout);
        if (err) {
            return err;
        }
        if (layout->have_data_end && layout->have_heap_base &&
            layout->data_end <= layout->heap_base) {
            layout->found = true;
            err = find_stack_pointer(mod, nimp_glob, layout);
            if (err) {
                return err;
            }
            break;
        }
    }
    err = wdec_export_iter_err(&exp_it);
    if (!err && !layout->found) {
        memset(layout, 0, sizeof *layout);
        layout->data_end_global = AOT_NO_INDEX;
        layout->heap_base_global = AOT_NO_INDEX;
        layout->stack_global = AOT_NO_INDEX;
    }
    return err;
}

static wasmc_err_t write_aux_data(aotw_t *writer, uint32_t nimp_glob)
{
    aux_layout layout;
    wasmc_err_t err = find_aux_layout(writer->m, nimp_glob, &layout);
    if (err) {
        return err;
    }
    /* the aux stack check applies to a real stack pointer global only */
    if (layout.found && layout.stack_size) {
        writer->aux_stack_global = layout.stack_global;
    }
    put32(writer, layout.data_end_global);
    put64(writer, layout.data_end);
    put32(writer, layout.heap_base_global);
    put64(writer, layout.heap_base);
    put32(writer, layout.stack_global);
    put64(writer, layout.stack_base);
    put32(writer, layout.stack_size);
    return WASMC_OK;
}

static wasmc_err_t write_init_data(aotw_t *writer,
                                   uint32_t *import_global_count)
{
    const wdec_module_t *mod = writer->m;
    uint32_t sec = section_begin(writer, AOT_SEC_INIT_DATA);
    import_counts imports;
    wasmc_err_t err = count_import_kinds(mod, &imports);
    if (err) {
        return err;
    }
    *import_global_count = imports.global;

    /* the memory shrink needs __heap_base */
    aux_layout layout;
    err = find_aux_layout(mod, imports.global, &layout);
    if (!err) {
        const mem_shrink shrink = {
            .ok = layout.found,
            .size = (layout.heap_base + HEAP_BASE_ALIGN - 1U) &
                    ~(uint64_t)(HEAP_BASE_ALIGN - 1U)};
        err = write_memory_info(writer, imports.mem, shrink);
    }
    if (!err) {
        err = write_table_info(writer, imports.table);
    }
    if (!err) {
        err = write_types(writer);
    }
    if (!err) {
        err = write_import_globals(writer, imports.global);
    }
    if (!err) {
        err = write_globals(writer);
    }
    if (!err) {
        err = write_import_funcs(writer, imports.func);
    }
    if (!err) {
        /* function count, start function, auxiliary data */
        put32(writer, writer->ndefined);
        put32(writer,
              wdec_has_start(mod) ? wdec_start_funcidx(mod) : AOT_NO_INDEX);
        err = write_aux_data(writer, imports.global);
    }
    if (err) {
        return err;
    }
    put32(writer, 0); /* object data sections: none */
    section_end(writer, sec);
    return writer->err;
}

/* Facts the code generator needs: global offsets in the module instance
 * (aotfmt.tex "Globals": packed after the memory instances, imported
 * first) and table 0's offset. */
static wasmc_err_t module_facts(aotw_t *writer)
{
    const wdec_module_t *mod = writer->m;
    uint32_t off = WAMR_INST_GLOBAL_TABLE_DATA +
                   (WAMR_MEMORY_INSTANCE_SIZE * writer->memory_count);
    uint32_t gidx = 0;
    wdec_import_t imp;
    wdec_import_iter_t imp_it;
    WDEC_FOR_EACH_IMPORT(imp_it, imp, mod) {
        if (imp.kind != WDEC_IMPKIND_GLOBAL) {
            continue;
        }
        writer->global_off[gidx++] = off;
        off += valtype_cells(imp.desc.global.valtype) * 4U;
    }
    wasmc_err_t err = wdec_import_iter_err(&imp_it);
    if (err) {
        return err;
    }
    wdec_global_t glob;
    wdec_global_iter_t git;
    WDEC_FOR_EACH_GLOBAL(git, glob, mod) {
        writer->global_off[gidx++] = off;
        off += valtype_cells(glob.valtype) * 4U;
    }
    err = wdec_global_iter_err(&git);
    if (err) {
        return err;
    }
    writer->table0_off = off; /* the table instances follow the global data */
    return WASMC_OK;
}

/* memory.grow and memory.size anywhere: they decide the memory written. */
static wasmc_err_t find_memory_ops(aotw_t *writer)
{
    const wdec_module_t *mod = writer->m;
    wdec_func_t func;
    wdec_func_iter_t func_it;
    WDEC_FOR_EACH_FUNC(func_it, func, mod) {
        wdec_instr_iter_t iter;
        wdec_instr_t ins;
        WDEC_FOR_EACH_INSTR(iter, ins, &func) {
            if (ins.opcode == WDEC_OP_MEMORY_GROW) {
                writer->has_mem_grow = 1;
            } else if (ins.opcode == WDEC_OP_MEMORY_SIZE) {
                writer->has_mem_size = 1;
            }
        }
        const wasmc_err_t err = wdec_instr_iter_err(&iter);
        if (err) {
            return err;
        }
        if (writer->has_mem_grow && writer->has_mem_size) {
            break;
        }
    }
    return wdec_func_iter_err(&func_it);
}

/* ------------------------------------------------------------------------ */
/* Public API                                                               */
/* ------------------------------------------------------------------------ */

wasmc_err_t aotw_begin(aotw_t *writer, const wdec_module_t *mod,
                       const wasmc_allocator *mem, void *module_data)
{
    wasmc_err_t err;
    uint32_t import_globals;

    memset(writer, 0, sizeof *writer); /* no buffer yet: room() grows it */
    writer->m = mod;
    writer->mem = mem;
    writer->ndefined = wdec_defined_func_count(mod);
    writer->nimported_funcs = wdec_imported_func_count(mod);
    writer->nglobals =
        count_imports(mod, WDEC_IMPKIND_GLOBAL) + wdec_global_count(mod);
    if (module_data) { /* NULL: no functions, globals or types */
        writer->func_off = (uint32_t *)module_data;
        writer->global_off = writer->func_off + writer->ndefined;
        writer->canon_type =
            (uint16_t *)(writer->global_off + writer->nglobals);
    }
    for (uint32_t i = 0; i < writer->ndefined; i++) {
        aotw_chain_init(&writer->func_off[i]);
    }
    writer->aux_stack_global = AOT_NO_INDEX;
    err = canonical_types(writer);
    if (err) {
        return writer->err = err;
    }

    /* file header */
    put8(writer, AOT_MAGIC_0);
    put8(writer, AOT_MAGIC_1);
    put8(writer, AOT_MAGIC_2);
    put8(writer, AOT_MAGIC_3);
    put32(writer, AOT_VERSION);

    /* target info */
    {
        static const char arch[16] = AOT_TARGET_ARCH;
        uint32_t sec = section_begin(writer, AOT_SEC_TARGET_INFO);
        put16(writer, AOT_BIN_TYPE_ELF32L);
        put16(writer, 0); /* abi_type */
        put16(writer, AOT_E_TYPE_REL);
        put16(writer, AOT_E_MACHINE_RISCV);
        put32(writer, AOT_E_VERSION);
        put32(writer, AOT_E_FLAGS_RV32_SOFT);
        put64(writer, 0); /* feature flags: WASM 1.0 only */
        put64(writer, 0); /* reserved */
        put_bytes(writer, arch, sizeof arch);
        section_end(writer, sec);
    }

    err = find_memory_ops(writer);
    if (!err) {
        err = write_init_data(writer, &import_globals);
    }
    if (err) {
        return writer->err = err;
    }
    err = module_facts(writer);
    if (err) {
        return writer->err = err;
    }

    /* text section: literal size 0, then the code */
    writer->text_size_pos = section_begin(writer, AOT_SEC_TEXT);
    put32(writer, 0);
    writer->code_start = writer->pos;
    return writer->err;
}

void aotw_func_begin(aotw_t *writer)
{
    align(writer, 4);
    if (writer->err) {
        return;
    }
    if (writer->next_func >= writer->ndefined) {
        writer->err = WASMC_ERR_INVALID_ARG;
        return;
    }
    aotw_bind(writer, &writer->func_off[writer->next_func++]);
}

uint32_t aotw_read32(const aotw_t *writer, uint32_t code_off)
{
    if (writer->dry) {
        return 0;
    }
    const uint8_t *ptr = writer->buf + writer->code_start + code_off;
    return (uint32_t)get_le(ptr, ptr + sizeof(uint32_t), sizeof(uint32_t));
}

void aotw_patch32(const aotw_t *writer, uint32_t code_off, uint32_t insn)
{
    if (!writer->err && !writer->dry) {
        put32_at(writer->buf + writer->code_start + code_off, insn);
    }
}

void aotw_dry_begin(aotw_t *writer)
{
    writer->dry = 1;
    writer->dry_start = writer->pos;
}

uint32_t aotw_dry_end(aotw_t *writer)
{
    uint32_t count = writer->pos - writer->dry_start;
    writer->pos = writer->dry_start;
    writer->dry = 0;
    return count;
}

/* sizing pass: a jump or branch only marks its chain pending */
static void dry_site(aotw_t *writer, uint32_t *chain)
{
    if (*chain & AOTW_CHAIN_PENDING) {
        *chain = AOTW_CHAIN_PENDING | aotw_code_pos(writer);
    }
    writer->pos += 4;
}

void aotw_jump(aotw_t *writer, uint32_t *chain, unsigned dst)
{
    uint32_t site = aotw_code_pos(writer);
    if (writer->err) {
        return;
    }
    if (writer->dry) {
        dry_site(writer, chain);
        return;
    }
    if (!(*chain & AOTW_CHAIN_PENDING)) { /* bound: direct jump */
        int64_t delta = (int64_t)*chain - site;
        if (!rv_fits_jal((int32_t)delta) || delta != (int32_t)delta) {
            writer->err = WASMC_ERR_CODE_TOO_LARGE;
            return;
        }
        aotw_emit32(writer, rv_jal(dst, (int32_t)delta));
        return;
    }
    int32_t link = 0;
    if (*chain != AOTW_CHAIN_EMPTY) {
        int64_t delta = (int64_t)(*chain & ~AOTW_CHAIN_PENDING) - site;
        if (!rv_fits_jal((int32_t)delta) || delta != (int32_t)delta) {
            writer->err = WASMC_ERR_CODE_TOO_LARGE;
            return;
        }
        link = (int32_t)delta;
    }
    aotw_emit32(writer, rv_jal(dst, link));
    *chain = AOTW_CHAIN_PENDING | site;
}

void aotw_branch(aotw_t *writer, uint32_t *chain, uint32_t insn)
{
    uint32_t site = aotw_code_pos(writer);
    if (writer->err) {
        return;
    }
    if (writer->dry) {
        dry_site(writer, chain);
        return;
    }
    int64_t delta = 0;
    if (!(*chain & AOTW_CHAIN_PENDING)) {
        delta = (int64_t)*chain - site; /* bound */
    } else if (*chain != AOTW_CHAIN_EMPTY) {
        delta = (int64_t)(*chain & ~AOTW_CHAIN_PENDING) - site;
    }
    if (!rv_fits_branch((int32_t)delta) || delta != (int32_t)delta) {
        writer->err = WASMC_ERR_CODE_TOO_LARGE;
        return;
    }
    aotw_emit32(writer, rv_b_set_off(insn, (int32_t)delta));
    if (*chain & AOTW_CHAIN_PENDING) {
        *chain = AOTW_CHAIN_PENDING | site;
    }
}

void aotw_bind(aotw_t *writer, uint32_t *chain)
{
    uint32_t target = aotw_code_pos(writer);
    if (writer->err) {
        return;
    }
    if (!writer->dry && *chain != AOTW_CHAIN_EMPTY &&
        (*chain & AOTW_CHAIN_PENDING)) {
        uint32_t site = *chain & ~AOTW_CHAIN_PENDING;
        for (;;) {
            uint32_t insn = aotw_read32(writer, site);
            int branch = rv_is_branch(insn);
            int32_t link = branch ? rv_b_off(insn) : rv_jal_off(insn);
            int64_t delta = (int64_t)target - site;
            if (delta != (int32_t)delta ||
                !(branch ? rv_fits_branch((int32_t)delta)
                         : rv_fits_jal((int32_t)delta))) {
                writer->err = WASMC_ERR_CODE_TOO_LARGE;
                return;
            }
            aotw_patch32(writer, site,
                         branch ? rv_b_set_off(insn, (int32_t)delta)
                                : rv_jal(rv_jal_rd(insn), (int32_t)delta));
            if (!link) {
                break;
            }
            site = (uint32_t)((int32_t)site + link);
        }
    }
    *chain = target;
}

void aotw_call(aotw_t *writer, uint32_t def_idx)
{
    if (def_idx >= writer->ndefined) {
        if (!writer->err) {
            writer->err = WASMC_ERR_INVALID_ARG;
        }
        return;
    }
    if (writer->dry) {
        writer->pos += 4;
        return;
    } /* the module's chains are not touched */
    aotw_jump(writer, &writer->func_off[def_idx], RV_RA);
}

void aotw_emit32(aotw_t *writer, uint32_t insn)
{
    if (writer->dry) {
        writer->pos += 4;
        return;
    }
    if (writer->err) {
        return;
    }
    if (room(writer, 4)) {
        put32_at(writer->buf + writer->pos, insn);
    }
    writer->pos += 4;
}

void aotw_reloc(aotw_t *writer, uint32_t code_off, uint32_t type,
                int32_t addend, unsigned sym)
{
    if (writer->err || writer->dry) {
        return;
    }
    if (sym >= WAMR_SYM__COUNT) {
        writer->err = WASMC_ERR_INVALID_ARG;
        return;
    }
    if (!writer->sym_index[sym]) {
        writer->sym_order[writer->nsyms++] = (uint8_t)sym;
        writer->sym_index[sym] = (uint8_t)writer->nsyms; /* 0 is ".rela.text" */
    }
    if (writer->tail - writer->pos < RELOC_SIZE && !grow(writer, RELOC_SIZE)) {
        return;
    }
    writer->nrelocs++;
    writer->tail -= RELOC_SIZE;
    const uint32_t fields[] = {code_off, (uint32_t)addend, type,
                               writer->sym_index[sym]};
    for (unsigned i = 0; i < sizeof fields / sizeof fields[0]; i++) {
        put32_at(writer->buf + writer->tail + (i * sizeof(uint32_t)),
                 fields[i]);
    }
}

static const char *const sym_names[WAMR_SYM__COUNT] = {
#define WAMR_SYM_NAME(id, name) name,
    WAMR_SYMBOLS(WAMR_SYM_NAME)
#undef WAMR_SYM_NAME
};

static uint32_t str_size(uint32_t len)
{
    return (2U + len + 1U + 1U) & ~1U;
} /* 2-aligned */

/* max_local_cell_num of a defined function: parameter cells + local cells */
static wasmc_err_t func_local_cells(const wdec_module_t *mod,
                                    const wdec_func_t *func, uint32_t *cells)
{
    wdec_functype_t ftype;
    uint32_t cnt;
    wdec_valtype_t valtype;
    wasmc_err_t err = wdec_get_type(mod, func->typeidx, &ftype);
    if (err) {
        return err;
    }
    *cells = 0;
    for (uint32_t k = 0; k < ftype.param_count; k++) {
        *cells += valtype_cells(ftype.params[k]);
    }
    wdec_locals_iter_t locals_it;
    WDEC_FOR_EACH_LOCALS(locals_it, cnt, valtype, func) {
        *cells += cnt * valtype_cells(valtype);
    }
    return wdec_locals_iter_err(&locals_it);
}

static wasmc_err_t write_function_section(aotw_t *writer)
{
    const wdec_module_t *mod = writer->m;
    uint32_t sec = section_begin(writer, AOT_SEC_FUNCTION);
    wdec_func_t func;
    for (uint32_t i = 0; i < writer->ndefined; i++) {
        put32(writer, writer->func_off[i]);
    }
    /* each array in one pass over the code section: wdec_get_func() and
     * wdec_func_typeidx() would walk it from the start for every function */
    wdec_func_iter_t func_it;
    WDEC_FOR_EACH_FUNC(func_it, func, mod) {
        put32(writer, writer->canon_type[func.typeidx]);
    }
    wasmc_err_t err = wdec_func_iter_err(&func_it);
    if (err) {
        return err;
    }
    WDEC_FOR_EACH_FUNC(func_it, func, mod) {
        uint32_t cells;
        err = func_local_cells(mod, &func, &cells);
        if (err) {
            return err;
        }
        put32(writer, cells);
    }
    err = wdec_func_iter_err(&func_it);
    if (err) {
        return err;
    }
    /* max_stack_cell_num: only used by runtimes with AOT stack frames */
    for (uint32_t i = 0; i < writer->ndefined; i++) {
        put32(writer, 0);
    }
    section_end(writer, sec);
    return WASMC_OK;
}

static wasmc_err_t write_export_section(aotw_t *writer)
{
    const wdec_module_t *mod = writer->m;
    uint32_t sec = section_begin(writer, AOT_SEC_EXPORT);
    wdec_export_t exp;
    wdec_export_iter_t exp_it;
    put32(writer, wdec_export_count(mod));
    WDEC_FOR_EACH_EXPORT(exp_it, exp, mod) {
        put32(writer, exp.index);
        put8(writer, exp.kind);
        put_str(writer, exp.name, exp.name_len); /* 2-aligned: one padding
                                                    byte */
    }
    wasmc_err_t err = wdec_export_iter_err(&exp_it);
    if (!err) {
        section_end(writer, sec);
    }
    return err;
}

/* name of symbol `idx`: ".rela.text", then the runtime symbols in first-use
 * order */
static const char *symbol_name_at(const aotw_t *writer, uint32_t idx)
{
    return idx ? sym_names[writer->sym_order[idx - 1]] : ".rela.text";
}

/* Copies the relocation records, pushed downwards from the top of the
 * buffer (the oldest is at the top), into the section in their order. The
 * destination stays below the tail region (room() keeps pos <= tail), so
 * the copy never overlaps a record still to read. */
static void put_reloc_records(aotw_t *writer)
{
    uint32_t need = writer->nrelocs * RELOC_SIZE;
    if (writer->err) {
        return;
    }
    if (!room(writer, need)) {
        return;
    }
    /* (room() may have grown the buffer: the records are at its new top) */
    const uint32_t src_top = writer->cap & ~3U;
    for (uint32_t k = 0; k < writer->nrelocs; k++) {
        uint32_t src = src_top - ((k + 1U) * RELOC_SIZE);
        memmove(writer->buf + writer->pos, writer->buf + src, RELOC_SIZE);
        writer->pos += RELOC_SIZE;
    }
}

static void write_relocation_section(aotw_t *writer)
{
    uint32_t sec = section_begin(writer, AOT_SEC_RELOCATION);
    uint32_t nsym = writer->nrelocs ? writer->nsyms + 1U : 0U;
    put32(writer, nsym);
    uint32_t off = 0;
    for (uint32_t i = 0; i < nsym; i++) {
        put32(writer, off);
        off += str_size((uint32_t)strlen(symbol_name_at(writer, i)));
    }
    put32(writer, off);
    for (uint32_t i = 0; i < nsym; i++) {
        const char *str = symbol_name_at(writer, i);
        put_str(writer, str, (uint32_t)strlen(str));
        align(writer, 2);
    }
    if (!writer->nrelocs) {
        put32(writer, 0); /* no groups */
    } else {
        put32(writer, 1); /* one group */
        put32(writer, 0); /* ".rela.text" */
        put32(writer, writer->nrelocs);
        put_reloc_records(writer);
    }
    section_end(writer, sec);
}

wasmc_err_t aotw_end(aotw_t *writer, uint32_t *out_size)
{
    if (!writer->err && writer->next_func != writer->ndefined) {
        writer->err = WASMC_ERR_INVALID_ARG;
    }
    align(writer, 4);
    section_end(writer, writer->text_size_pos);

    wasmc_err_t err = write_function_section(writer);
    if (!err) {
        err = write_export_section(writer);
    }
    if (err) {
        return writer->err = err;
    }
    write_relocation_section(writer);

    *out_size = writer->pos;
    if (!writer->err && writer->cap > writer->pos) {
        /* the staged records are copied: give back the rest */
        const wasmc_allocator *mem = writer->mem;
        uint8_t *buf =
            mem->out_grow(mem->ctx, writer->buf, writer->cap, writer->pos);
        if (buf) {
            writer->buf = buf;
            writer->cap = writer->pos;
        }
    }
    return writer->err;
}

void aotw_abort(aotw_t *writer)
{
    if (writer->buf) {
        const wasmc_allocator *mem = writer->mem;
        mem->out_grow(mem->ctx, writer->buf, writer->cap, 0);
        writer->buf = NULL;
        writer->cap = 0;
    }
}
