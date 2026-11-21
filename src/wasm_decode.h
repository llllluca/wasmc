/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * wdec - a small, allocation-free WebAssembly 1.0 binary decoder/validator
 *
 * Design goals (embedded / resource constrained targets):
 *
 *   - No dynamic memory allocation anywhere in this library. Every buffer
 *     the library needs (operand stack, control stack, ...) is supplied by
 *     the caller, who decides whether it comes from the stack, a static
 *     array, or a heap allocation. wdec never calls malloc/free.
 *
 *   - Zero-copy decoding. wdec_decode() does not copy the module: it only
 *     records pointers into the caller-owned input buffer, which must stay
 *     valid for as long as the wdec_module_t is used. This mirrors the
 *     "vector of raw bytes + count" approach and keeps RAM use at a small,
 *     fixed sizeof(wdec_module_t) regardless of module size.
 *
 *   - Two-phase use:
 *
 *       1. wdec_decode()   - cheap, single pass over the *top level*
 *                             section table (O(number of sections), plus
 *                             one skim over the import entries and the
 *                             custom section names). It checks the module
 *                             preamble, section ordering/uniqueness and
 *                             that every section's declared size stays
 *                             within the input buffer.
 *
 *       2. wdec_validate() - a full, single pass over the entire module
 *                             (O(module size)) that decodes every item in
 *                             every section, checks all static semantic
 *                             rules of the WebAssembly 1.0 spec (index
 *                             bounds, limits, constant expressions, ...)
 *                             and type-checks every function body using
 *                             the stack-based validation algorithm from
 *                             spec section 7.3 ("Validation Algorithm").
 *
 *     IMPORTANT SAFETY NOTE: every read performed anywhere in this library
 *     (by wdec_decode(), by wdec_validate(), and by every accessor/iterator
 *     below) is bounds-checked against the *whole* input buffer, not just
 *     against a section's declared boundaries. This means it is always
 *     memory-safe to call the accessor functions on a module that has only
 *     been wdec_decode()'d, even if the input is corrupt or adversarial -
 *     you will get WASMC_ERR_* codes rather than out-of-bounds reads. What
 *     wdec_decode() alone does *not* guarantee is that the module is a
 *     spec-conformant program (correct indices, well-typed code, etc.) -
 *     that guarantee only comes from a successful wdec_validate().
 *
 *   - The accessor API below lets a caller enumerate/inspect every part of
 *     a module (types, imports, functions, tables, memories, globals,
 *     exports, element segments, data segments) via either a forward-only
 *     O(1)-amortized iterator, or an O(n) indexed getter for convenience.
 *     A function body's own instructions are walked the same way, via
 *     wdec_instr_iter_t - a raw, allocation-free instruction decoder that
 *     needs no operand/control stack, separate from wdec_validate_func()
 *     which additionally type-checks what it decodes.
 */

#ifndef WDEC_H_INCLUDED
#define WDEC_H_INCLUDED

#include "wasmc/error.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Value types                                                        */
/* ------------------------------------------------------------------ */

/* wdec reuses the WebAssembly binary encoding of value types directly, so
 * no translation table is needed anywhere in the library. */
#define WDEC_TYPE_I32     0x7F
#define WDEC_TYPE_I64     0x7E
#define WDEC_TYPE_F32     0x7D
#define WDEC_TYPE_F64     0x7C
#define WDEC_TYPE_FUNCREF 0x70
#define WDEC_TYPE_EMPTY   0x40 /* blocktype: no result value */

/* Validator-internal marker for "unknown/polymorphic" operand type, used
 * only inside wdec_valtype_t values living on the caller-supplied operand
 * stack during function-body validation. 0x00 never collides with any of
 * the value type / blocktype encodings above. */
#define WDEC_TYPE_UNKNOWN 0x00

typedef uint8_t wdec_valtype_t;

/* ------------------------------------------------------------------ */
/* Instruction opcodes                                                */
/* ------------------------------------------------------------------ */

/**
 * enum wdec_opcode_t - Every WebAssembly 1.0 instruction opcode
 * (spec 5.4), one enumerator per defined byte value.
 *
 * Gaps in the byte range are reserved by the spec for future extensions
 * and simply have no enumerator here. Values are the actual
 * binary-format byte, so this enum can be used anywhere a raw opcode
 * byte is needed (table indexing, comparisons) without any translation,
 * the same way wdec_valtype_t reuses the value type byte directly.
 *
 * Individual enumerators are not documented one by one below: each
 * name directly encodes the WebAssembly instruction mnemonic it stands
 * for (e.g. %WDEC_OP_I32_ADD is i32.add), grouped by binary-format
 * subsection exactly as spec 5.4 lists them - control (5.4.1),
 * parametric (5.4.2), variable (5.4.3), memory (5.4.4), then numeric
 * (5.4.5): constants, testops/relops, unops/binops, and finally cvtops.
 */
typedef enum {
    /* control instructions (5.4.1) */
    WDEC_OP_UNREACHABLE = 0x00,
    WDEC_OP_NOP = 0x01,
    WDEC_OP_BLOCK = 0x02,
    WDEC_OP_LOOP = 0x03,
    WDEC_OP_IF = 0x04,
    WDEC_OP_ELSE = 0x05,
    WDEC_OP_END = 0x0B,
    WDEC_OP_BR = 0x0C,
    WDEC_OP_BR_IF = 0x0D,
    WDEC_OP_BR_TABLE = 0x0E,
    WDEC_OP_RETURN = 0x0F,
    WDEC_OP_CALL = 0x10,
    WDEC_OP_CALL_INDIRECT = 0x11,

    /* parametric instructions (5.4.2) */
    WDEC_OP_DROP = 0x1A,
    WDEC_OP_SELECT = 0x1B,

    /* variable instructions (5.4.3) */
    WDEC_OP_LOCAL_GET = 0x20,
    WDEC_OP_LOCAL_SET = 0x21,
    WDEC_OP_LOCAL_TEE = 0x22,
    WDEC_OP_GLOBAL_GET = 0x23,
    WDEC_OP_GLOBAL_SET = 0x24,

    /* memory instructions (5.4.4) */
    WDEC_OP_I32_LOAD = 0x28,
    WDEC_OP_I64_LOAD = 0x29,
    WDEC_OP_F32_LOAD = 0x2A,
    WDEC_OP_F64_LOAD = 0x2B,
    WDEC_OP_I32_LOAD8_S = 0x2C,
    WDEC_OP_I32_LOAD8_U = 0x2D,
    WDEC_OP_I32_LOAD16_S = 0x2E,
    WDEC_OP_I32_LOAD16_U = 0x2F,
    WDEC_OP_I64_LOAD8_S = 0x30,
    WDEC_OP_I64_LOAD8_U = 0x31,
    WDEC_OP_I64_LOAD16_S = 0x32,
    WDEC_OP_I64_LOAD16_U = 0x33,
    WDEC_OP_I64_LOAD32_S = 0x34,
    WDEC_OP_I64_LOAD32_U = 0x35,
    WDEC_OP_I32_STORE = 0x36,
    WDEC_OP_I64_STORE = 0x37,
    WDEC_OP_F32_STORE = 0x38,
    WDEC_OP_F64_STORE = 0x39,
    WDEC_OP_I32_STORE8 = 0x3A,
    WDEC_OP_I32_STORE16 = 0x3B,
    WDEC_OP_I64_STORE8 = 0x3C,
    WDEC_OP_I64_STORE16 = 0x3D,
    WDEC_OP_I64_STORE32 = 0x3E,
    WDEC_OP_MEMORY_SIZE = 0x3F,
    WDEC_OP_MEMORY_GROW = 0x40,

    /* numeric instructions (5.4.5): constants ... */
    WDEC_OP_I32_CONST = 0x41,
    WDEC_OP_I64_CONST = 0x42,
    WDEC_OP_F32_CONST = 0x43,
    WDEC_OP_F64_CONST = 0x44,

    /* ... testops, relops ... */
    WDEC_OP_I32_EQZ = 0x45,
    WDEC_OP_I32_EQ = 0x46,
    WDEC_OP_I32_NE = 0x47,
    WDEC_OP_I32_LT_S = 0x48,
    WDEC_OP_I32_LT_U = 0x49,
    WDEC_OP_I32_GT_S = 0x4A,
    WDEC_OP_I32_GT_U = 0x4B,
    WDEC_OP_I32_LE_S = 0x4C,
    WDEC_OP_I32_LE_U = 0x4D,
    WDEC_OP_I32_GE_S = 0x4E,
    WDEC_OP_I32_GE_U = 0x4F,
    WDEC_OP_I64_EQZ = 0x50,
    WDEC_OP_I64_EQ = 0x51,
    WDEC_OP_I64_NE = 0x52,
    WDEC_OP_I64_LT_S = 0x53,
    WDEC_OP_I64_LT_U = 0x54,
    WDEC_OP_I64_GT_S = 0x55,
    WDEC_OP_I64_GT_U = 0x56,
    WDEC_OP_I64_LE_S = 0x57,
    WDEC_OP_I64_LE_U = 0x58,
    WDEC_OP_I64_GE_S = 0x59,
    WDEC_OP_I64_GE_U = 0x5A,
    WDEC_OP_F32_EQ = 0x5B,
    WDEC_OP_F32_NE = 0x5C,
    WDEC_OP_F32_LT = 0x5D,
    WDEC_OP_F32_GT = 0x5E,
    WDEC_OP_F32_LE = 0x5F,
    WDEC_OP_F32_GE = 0x60,
    WDEC_OP_F64_EQ = 0x61,
    WDEC_OP_F64_NE = 0x62,
    WDEC_OP_F64_LT = 0x63,
    WDEC_OP_F64_GT = 0x64,
    WDEC_OP_F64_LE = 0x65,
    WDEC_OP_F64_GE = 0x66,

    /* ... unops, binops ... */
    WDEC_OP_I32_CLZ = 0x67,
    WDEC_OP_I32_CTZ = 0x68,
    WDEC_OP_I32_POPCNT = 0x69,
    WDEC_OP_I32_ADD = 0x6A,
    WDEC_OP_I32_SUB = 0x6B,
    WDEC_OP_I32_MUL = 0x6C,
    WDEC_OP_I32_DIV_S = 0x6D,
    WDEC_OP_I32_DIV_U = 0x6E,
    WDEC_OP_I32_REM_S = 0x6F,
    WDEC_OP_I32_REM_U = 0x70,
    WDEC_OP_I32_AND = 0x71,
    WDEC_OP_I32_OR = 0x72,
    WDEC_OP_I32_XOR = 0x73,
    WDEC_OP_I32_SHL = 0x74,
    WDEC_OP_I32_SHR_S = 0x75,
    WDEC_OP_I32_SHR_U = 0x76,
    WDEC_OP_I32_ROTL = 0x77,
    WDEC_OP_I32_ROTR = 0x78,
    WDEC_OP_I64_CLZ = 0x79,
    WDEC_OP_I64_CTZ = 0x7A,
    WDEC_OP_I64_POPCNT = 0x7B,
    WDEC_OP_I64_ADD = 0x7C,
    WDEC_OP_I64_SUB = 0x7D,
    WDEC_OP_I64_MUL = 0x7E,
    WDEC_OP_I64_DIV_S = 0x7F,
    WDEC_OP_I64_DIV_U = 0x80,
    WDEC_OP_I64_REM_S = 0x81,
    WDEC_OP_I64_REM_U = 0x82,
    WDEC_OP_I64_AND = 0x83,
    WDEC_OP_I64_OR = 0x84,
    WDEC_OP_I64_XOR = 0x85,
    WDEC_OP_I64_SHL = 0x86,
    WDEC_OP_I64_SHR_S = 0x87,
    WDEC_OP_I64_SHR_U = 0x88,
    WDEC_OP_I64_ROTL = 0x89,
    WDEC_OP_I64_ROTR = 0x8A,
    WDEC_OP_F32_ABS = 0x8B,
    WDEC_OP_F32_NEG = 0x8C,
    WDEC_OP_F32_CEIL = 0x8D,
    WDEC_OP_F32_FLOOR = 0x8E,
    WDEC_OP_F32_TRUNC = 0x8F,
    WDEC_OP_F32_NEAREST = 0x90,
    WDEC_OP_F32_SQRT = 0x91,
    WDEC_OP_F32_ADD = 0x92,
    WDEC_OP_F32_SUB = 0x93,
    WDEC_OP_F32_MUL = 0x94,
    WDEC_OP_F32_DIV = 0x95,
    WDEC_OP_F32_MIN = 0x96,
    WDEC_OP_F32_MAX = 0x97,
    WDEC_OP_F32_COPYSIGN = 0x98,
    WDEC_OP_F64_ABS = 0x99,
    WDEC_OP_F64_NEG = 0x9A,
    WDEC_OP_F64_CEIL = 0x9B,
    WDEC_OP_F64_FLOOR = 0x9C,
    WDEC_OP_F64_TRUNC = 0x9D,
    WDEC_OP_F64_NEAREST = 0x9E,
    WDEC_OP_F64_SQRT = 0x9F,
    WDEC_OP_F64_ADD = 0xA0,
    WDEC_OP_F64_SUB = 0xA1,
    WDEC_OP_F64_MUL = 0xA2,
    WDEC_OP_F64_DIV = 0xA3,
    WDEC_OP_F64_MIN = 0xA4,
    WDEC_OP_F64_MAX = 0xA5,
    WDEC_OP_F64_COPYSIGN = 0xA6,

    /* ... cvtops */
    WDEC_OP_I32_WRAP_I64 = 0xA7,
    WDEC_OP_I32_TRUNC_F32_S = 0xA8,
    WDEC_OP_I32_TRUNC_F32_U = 0xA9,
    WDEC_OP_I32_TRUNC_F64_S = 0xAA,
    WDEC_OP_I32_TRUNC_F64_U = 0xAB,
    WDEC_OP_I64_EXTEND_I32_S = 0xAC,
    WDEC_OP_I64_EXTEND_I32_U = 0xAD,
    WDEC_OP_I64_TRUNC_F32_S = 0xAE,
    WDEC_OP_I64_TRUNC_F32_U = 0xAF,
    WDEC_OP_I64_TRUNC_F64_S = 0xB0,
    WDEC_OP_I64_TRUNC_F64_U = 0xB1,
    WDEC_OP_F32_CONVERT_I32_S = 0xB2,
    WDEC_OP_F32_CONVERT_I32_U = 0xB3,
    WDEC_OP_F32_CONVERT_I64_S = 0xB4,
    WDEC_OP_F32_CONVERT_I64_U = 0xB5,
    WDEC_OP_F32_DEMOTE_F64 = 0xB6,
    WDEC_OP_F64_CONVERT_I32_S = 0xB7,
    WDEC_OP_F64_CONVERT_I32_U = 0xB8,
    WDEC_OP_F64_CONVERT_I64_S = 0xB9,
    WDEC_OP_F64_CONVERT_I64_U = 0xBA,
    WDEC_OP_F64_PROMOTE_F32 = 0xBB,
    WDEC_OP_I32_REINTERPRET_F32 = 0xBC,
    WDEC_OP_I64_REINTERPRET_F64 = 0xBD,
    WDEC_OP_F32_REINTERPRET_I32 = 0xBE,
    WDEC_OP_F64_REINTERPRET_I64 = 0xBF
} wdec_opcode_t;

/* Shared kind values for import/export descriptors (the binary format
 * assigns the same 0..3 encoding to both). */
#define WDEC_IMPKIND_FUNC   0x00
#define WDEC_IMPKIND_TABLE  0x01
#define WDEC_IMPKIND_MEM    0x02
#define WDEC_IMPKIND_GLOBAL 0x03

#define WDEC_EXTKIND_FUNC   0x00
#define WDEC_EXTKIND_TABLE  0x01
#define WDEC_EXTKIND_MEM    0x02
#define WDEC_EXTKIND_GLOBAL 0x03

/**
 * struct wdec_limits_t - A decoded limits pair (spec 5.3.4): the size
 * range of a table or memory.
 * @min: minimum size.
 * @max: maximum size; only meaningful if @has_max is non-zero.
 * @has_max: non-zero if @max was present in the encoding.
 */
typedef struct {
    uint32_t min;
    uint32_t max;
    int has_max;
} wdec_limits_t;

/**
 * struct wdec_vec_t - A section's raw byte range plus its declared item
 * count.
 * @count: number of items the section declares.
 * @start: first byte after the item count field.
 * @end: declared end of the section's contents.
 *
 * Internal building block of &wdec_module_t; not normally constructed
 * or read directly by callers, who instead use the per-section
 * iterator/accessor functions.
 */
typedef struct {
    uint32_t count;
    const uint8_t *start;
    const uint8_t *end;
} wdec_vec_t;

/**
 * struct wdec_module_t - A decoded WebAssembly module.
 * @buf: the input buffer passed to wdec_decode(). Every pointer
 *       elsewhere in this struct (and in anything decoded from it)
 *       points inside @buf; nothing is copied.
 * @len: length of @buf in bytes.
 * @types: the type section.
 * @imports: the import section.
 * @funcs: the function section (vec(typeidx) - one entry per defined
 *         function, pairing up with @codes).
 * @tables: the table section.
 * @mems: the memory section.
 * @globals: the global section.
 * @exports: the export section.
 * @elems: the element section.
 * @codes: the code section (paired with @funcs).
 * @data: the data section.
 * @num_import_funcs: number of func imports; also where defined
 *                     function indices start in the combined
 *                     (imports-first) index space.
 * @num_import_tables: number of table imports.
 * @num_import_mems: number of memory imports.
 * @num_import_globals: number of global imports; also where defined
 *                       global indices start in the combined index
 *                       space.
 * @has_start: non-zero if a start section is present.
 * @start_funcidx: the start function's index; only meaningful if
 *                 @has_start is non-zero.
 *
 * Initialized by wdec_decode(); every other field beyond @buf/@len is
 * computed from @buf's section table, import section and start section.
 * Read through
 * the accessor API in the rest of this header rather than these fields
 * directly, except @buf/@len/@has_start/@start_funcidx and the
 * @num_import_* counts, which are meant for direct use (e.g. to map a
 * combined-space index into "imported" vs. "locally defined").
 */
typedef struct {
    const uint8_t *buf;
    uint32_t len;

    wdec_vec_t types;
    wdec_vec_t imports;
    wdec_vec_t funcs;
    wdec_vec_t tables;
    wdec_vec_t mems;
    wdec_vec_t globals;
    wdec_vec_t exports;
    wdec_vec_t elems;
    wdec_vec_t codes;
    wdec_vec_t data;

    uint32_t num_import_funcs;
    uint32_t num_import_tables;
    uint32_t num_import_mems;
    uint32_t num_import_globals;

    int has_start;
    uint32_t start_funcidx;
} wdec_module_t;

/**
 * wdec_decode() - Decode the top-level structure of a WebAssembly
 * binary module.
 * @mod: module handle to initialize; overwritten unconditionally.
 * @buf: pointer to the start of the binary module. Must remain valid
 *       for as long as @mod (and anything decoded from it) is used,
 *       since @mod only stores pointers into @buf - nothing is copied.
 * @len: length of @buf in bytes.
 *
 * Parses the preamble and the section table in a single O(number of
 * sections) pass: checks the magic number and version, section
 * ordering and uniqueness, that every section's declared size stays
 * within @buf, skims the import section to count each import kind, and
 * checks every custom section's name for valid UTF-8 (the one part of
 * a custom section with any defined structure, per spec 5.2.4/5.5.3).
 *
 * This alone does not guarantee @mod is a spec-conformant module (correct
 * indices, well-typed code, etc.) - call wdec_validate() for that. It
 * does guarantee every subsequent read through the accessor API below
 * stays within @buf, even on malformed or adversarial input.
 *
 * Return: %WASMC_OK on success, or a %WASMC_ERR_* code.
 */
wasmc_err_t wdec_decode(wdec_module_t *mod, const uint8_t *buf, uint32_t len);

/* Types section */

/**
 * struct wdec_functype_t - A decoded function type (spec 5.3.3).
 * @param_count: number of parameters.
 * @params: array of @param_count value types, or NULL if @param_count
 *          is 0.
 * @result_count: number of results: 0 or 1 in WebAssembly 1.0.
 * @results: array of @result_count value types, or NULL if
 *           @result_count is 0.
 */
typedef struct {
    uint32_t param_count;
    const uint8_t *params;
    uint32_t result_count;
    const uint8_t *results;
} wdec_functype_t;

/**
 * struct wdec_type_iter_t - Iterator state for wdec_type_iter_init() /
 * wdec_type_iter_next().
 * @cur: current read position. Opaque; do not access directly.
 * @end: end of the type section. Opaque; do not access directly.
 * @err: why the iteration stopped, %WASMC_OK until an error; read it
 *       with wdec_type_iter_err().
 */
typedef struct {
    const uint8_t *cur;
    const uint8_t *end;
    wasmc_err_t err;
} wdec_type_iter_t;

/**
 * wdec_type_iter_init() - Start a forward iterator over the type
 * section.
 * @iter: the iterator to set up, positioned before the first type.
 * @mod: a decoded module.
 */
void wdec_type_iter_init(wdec_type_iter_t *iter, const wdec_module_t *mod);

/**
 * wdec_type_iter_next() - Decode the next function type.
 * @iter: iterator, advanced on success.
 * @out: filled with the decoded type on success.
 *
 * Return: 1 if @out was filled, 0 when iteration is done (check
 * wdec_type_iter_err()).
 */
int wdec_type_iter_next(wdec_type_iter_t *iter, wdec_functype_t *out);

/**
 * WDEC_FOR_EACH_TYPE() - Loop over the function types of a module.
 * @iter: a &wdec_type_iter_t, declared by the caller.
 * @ftype: a &wdec_functype_t, declared by the caller; the current one.
 * @mod: the module, as for wdec_type_iter_init().
 *
 * Expands to a for statement, so 'break' and 'continue' work as usual. When
 * the loop ends by itself, check wdec_type_iter_err(&@iter) to tell the end
 * from a decoding error.
 */
#define WDEC_FOR_EACH_TYPE(iter, ftype, mod)                                   \
    for (wdec_type_iter_init(&(iter), (mod));                                  \
         wdec_type_iter_next(&(iter), &(ftype));)

/**
 * wdec_type_iter_err() - Why an iteration over the type section stopped.
 * @iter: the iterator, once wdec_type_iter_next() returned 0.
 *
 * Return: %WASMC_OK at the end, or the decoding error that stopped it.
 */
static inline wasmc_err_t wdec_type_iter_err(const wdec_type_iter_t *iter)
{
    return iter->err;
}

/**
 * wdec_type_count() - Number of entries in the type section.
 * @mod: a decoded module.
 *
 * Return: The type count.
 */
uint32_t wdec_type_count(const wdec_module_t *mod);

/**
 * wdec_get_type() - Look up a function type by index.
 * @mod: a decoded module.
 * @typeidx: type index, checked against wdec_type_count().
 * @out: filled with the decoded type on success.
 *
 * O(@typeidx): walks the type section from the start. Prefer
 * wdec_type_iter_next() when visiting every type.
 *
 * Return: %WASMC_OK on success, %WASMC_ERR_TYPE_INDEX if @typeidx is out
 * of range, or another %WASMC_ERR_* if the type at that index is itself
 * malformed.
 */
wasmc_err_t wdec_get_type(const wdec_module_t *mod, uint32_t typeidx,
                          wdec_functype_t *out);

/* Import section */

/**
 * struct wdec_globaltype_t - The type of a global (spec 5.3.7).
 * @valtype: its value type.
 * @mut: 0 (const) or 1 (var).
 */
typedef struct {
    wdec_valtype_t valtype;
    uint8_t mut;
} wdec_globaltype_t;

/**
 * struct wdec_import_t - A decoded import (spec 5.5.5).
 * @module: the module name being imported from. Not NUL-terminated.
 * @module_len: length of @module in bytes.
 * @name: the field name being imported. Not NUL-terminated.
 * @name_len: length of @name in bytes.
 * @kind: which alternative of @desc is active: one of %WDEC_IMPKIND_*
 *        (numerically identical to %WDEC_EXTKIND_*).
 * @desc: the kind-specific import descriptor: @desc.typeidx when @kind
 *        is %WDEC_IMPKIND_FUNC, @desc.table when %WDEC_IMPKIND_TABLE,
 *        @desc.mem when %WDEC_IMPKIND_MEM, @desc.global when
 *        %WDEC_IMPKIND_GLOBAL.
 */
typedef struct {
    const char *module;
    uint32_t module_len;
    const char *name;
    uint32_t name_len;
    uint8_t kind;
    union {
        uint32_t typeidx;
        struct {
            uint8_t elemtype;
            wdec_limits_t limits;
        } table;
        wdec_limits_t mem;
        wdec_globaltype_t global;
    } desc;
} wdec_import_t;

/**
 * struct wdec_import_iter_t - Iterator state for
 * wdec_import_iter_init() / wdec_import_iter_next().
 * @cur: current read position. Opaque; do not access directly.
 * @end: end of the import section. Opaque; do not access directly.
 * @err: why the iteration stopped, %WASMC_OK until an error; read it
 *       with wdec_import_iter_err().
 */
typedef struct {
    const uint8_t *cur;
    const uint8_t *end;
    wasmc_err_t err;
} wdec_import_iter_t;

/**
 * wdec_import_iter_init() - Start a forward iterator over the import
 * section.
 * @iter: the iterator to set up, positioned before the first import.
 * @mod: a decoded module.
 */
void wdec_import_iter_init(wdec_import_iter_t *iter, const wdec_module_t *mod);

/**
 * wdec_import_iter_next() - Decode the next import.
 * @iter: iterator, advanced on success.
 * @out: filled with the decoded import on success.
 *
 * Return: 1 if @out was filled, 0 when iteration is done (check
 * wdec_import_iter_err()).
 */
int wdec_import_iter_next(wdec_import_iter_t *iter, wdec_import_t *out);

/**
 * WDEC_FOR_EACH_IMPORT() - Loop over the imports of a module.
 * @iter: a &wdec_import_iter_t, declared by the caller.
 * @imp: a &wdec_import_t, declared by the caller; the current one.
 * @mod: the module, as for wdec_import_iter_init().
 *
 * Expands to a for statement, so 'break' and 'continue' work as usual. When
 * the loop ends by itself, check wdec_import_iter_err(&@iter) to tell the end
 * from a decoding error.
 */
#define WDEC_FOR_EACH_IMPORT(iter, imp, mod)                                   \
    for (wdec_import_iter_init(&(iter), (mod));                                \
         wdec_import_iter_next(&(iter), &(imp));)

/**
 * wdec_import_iter_err() - Why an iteration over the import section stopped.
 * @iter: the iterator, once wdec_import_iter_next() returned 0.
 *
 * Return: %WASMC_OK at the end, or the decoding error that stopped it.
 */
static inline wasmc_err_t wdec_import_iter_err(const wdec_import_iter_t *iter)
{
    return iter->err;
}

/**
 * wdec_import_count() - Number of entries in the import section.
 * @mod: a decoded module.
 *
 * Return: The import count.
 */
uint32_t wdec_import_count(const wdec_module_t *mod);

/**
 * wdec_get_import() - Look up an import by index.
 * @mod: a decoded module.
 * @idx: import index, checked against wdec_import_count().
 * @out: filled with the decoded import on success.
 *
 * O(@idx): walks the import section from the start. Prefer
 * wdec_import_iter_next() when visiting every import.
 *
 * Return: %WASMC_OK on success, %WASMC_ERR_INVALID_ARG if @idx is out of
 * range, or another %WASMC_ERR_* if the import at that index is itself
 * malformed.
 */
wasmc_err_t wdec_get_import(const wdec_module_t *mod, uint32_t idx,
                            wdec_import_t *out);

/* Functions (function + code sections) */

/**
 * struct wdec_func_t - A decoded defined function (function + code
 * sections combined, spec 5.5.6/5.5.13).
 * @typeidx: index into the type section.
 * @locals_start: start of the raw vec(locals) - pairs of (u32 count,
 *                valtype); walk with wdec_locals_iter_init() /
 *                wdec_locals_iter_next() rather than reading directly.
 *                NULL if @local_decl_count is 0.
 * @locals_end: end of the raw vec(locals). NULL if @local_decl_count is
 *              0.
 * @local_decl_count: number of (count, valtype) run-length groups.
 * @local_count: total number of declared locals (expanded), not
 *               counting parameters.
 * @body: first opcode of the function body; walk with
 *        wdec_instr_iter_init() / wdec_instr_iter_next(), or type-check
 *        with wdec_validate_func().
 * @body_end: one past the closing 'end' of the function body.
 */
typedef struct {
    uint32_t typeidx;
    const uint8_t *locals_start;
    const uint8_t *locals_end;
    uint32_t local_decl_count;
    uint32_t local_count;
    const uint8_t *body;
    const uint8_t *body_end;
} wdec_func_t;

/**
 * struct wdec_locals_iter_t - Iterator state for
 * wdec_locals_iter_init() / wdec_locals_iter_next().
 * @cur: current read position. Opaque; do not access directly.
 * @end: end of the function's local declarations. Opaque; do not access
 *       directly.
 * @err: why the iteration stopped, %WASMC_OK until an error; read it
 *       with wdec_locals_iter_err().
 */
typedef struct {
    const uint8_t *cur;
    const uint8_t *end;
    wasmc_err_t err;
} wdec_locals_iter_t;

/**
 * wdec_locals_iter_init() - Start an iterator over a function's
 * declared-local run-length groups.
 * @iter: the iterator to set up, positioned before the first
 *        (count, valtype) group.
 * @func: a function obtained from wdec_get_func() or wdec_func_iter_next().
 */
void wdec_locals_iter_init(wdec_locals_iter_t *iter, const wdec_func_t *func);

/**
 * wdec_locals_iter_next() - Decode the next declared-local group.
 * @iter: iterator, advanced on success.
 * @count: filled with the number of consecutive locals of @valtype.
 * @valtype: filled with their shared value type.
 *
 * A function's locals are declared as a compressed run-length sequence
 * of (count, valtype) pairs (e.g. "3 x i32, 1 x i64") rather than one
 * entry per local; this walks that sequence one group at a time.
 *
 * Return: 1 if @count/@valtype were filled, 0 when iteration is done
 * (check
 * wdec_locals_iter_err()).
 */
int wdec_locals_iter_next(wdec_locals_iter_t *iter, uint32_t *count,
                          wdec_valtype_t *valtype);

/**
 * WDEC_FOR_EACH_LOCALS() - Loop over the declared-local groups of a function.
 * @iter: a &wdec_locals_iter_t, declared by the caller.
 * @count: a uint32_t, declared by the caller; the locals in the group.
 * @valtype: a wdec_valtype_t, declared by the caller; their type.
 * @func: the function, as for wdec_locals_iter_init().
 *
 * Expands to a for statement, so 'break' and 'continue' work as usual. When
 * the loop ends by itself, check wdec_locals_iter_err(&@iter) to tell the end
 * from a decoding error.
 */
#define WDEC_FOR_EACH_LOCALS(iter, count, valtype, func)                       \
    for (wdec_locals_iter_init(&(iter), (func));                               \
         wdec_locals_iter_next(&(iter), &(count), &(valtype));)

/**
 * wdec_locals_iter_err() - Why an iteration over the local declarations
 * stopped.
 * @iter: the iterator, once wdec_locals_iter_next() returned 0.
 *
 * Return: %WASMC_OK at the end, or the decoding error that stopped it.
 */
static inline wasmc_err_t wdec_locals_iter_err(const wdec_locals_iter_t *iter)
{
    return iter->err;
}

/**
 * struct wdec_func_iter_t - Iterator state for wdec_func_iter_init() /
 * wdec_func_iter_next().
 * @fcur: current read position in the function section. Opaque; do not
 *        access directly.
 * @fend: end of the function section. Opaque; do not access directly.
 * @ccur: current read position in the code section. Opaque; do not
 *        access directly.
 * @cend: end of the code section. Opaque; do not access directly.
 * @err: why the iteration stopped, %WASMC_OK until an error; read it
 *       with wdec_func_iter_err().
 */
typedef struct {
    const uint8_t *fcur;
    const uint8_t *fend;
    const uint8_t *ccur;
    const uint8_t *cend;
    wasmc_err_t err;
} wdec_func_iter_t;

/**
 * wdec_func_iter_init() - Start a forward iterator over defined
 * functions, walking the function and code sections together.
 * @iter: the iterator to set up, positioned before the first defined function.
 * @mod: a decoded module.
 *
 * Mirrors func+code index pairing in the binary format. Prefer this
 * over repeated wdec_get_func() calls when visiting every function:
 * wdec_get_func() re-walks from the start of the section each time and
 * is O(index) per call, while this iterator is O(1)-amortized per
 * function.
 */
void wdec_func_iter_init(wdec_func_iter_t *iter, const wdec_module_t *mod);

/**
 * wdec_func_iter_next() - Decode the next defined function.
 * @iter: iterator, advanced on success.
 * @out: filled with the decoded function on success.
 *
 * Return: 1 if @out was filled, 0 when iteration is done (check
 * wdec_func_iter_err()).
 */
int wdec_func_iter_next(wdec_func_iter_t *iter, wdec_func_t *out);

/**
 * WDEC_FOR_EACH_FUNC() - Loop over the defined functions of a module.
 * @iter: a &wdec_func_iter_t, declared by the caller.
 * @func: a &wdec_func_t, declared by the caller; the current one.
 * @mod: the module, as for wdec_func_iter_init().
 *
 * Expands to a for statement, so 'break' and 'continue' work as usual. When
 * the loop ends by itself, check wdec_func_iter_err(&@iter) to tell the end
 * from a decoding error.
 */
#define WDEC_FOR_EACH_FUNC(iter, func, mod)                                    \
    for (wdec_func_iter_init(&(iter), (mod));                                  \
         wdec_func_iter_next(&(iter), &(func));)

/**
 * wdec_func_iter_err() - Why an iteration over the defined functions stopped.
 * @iter: the iterator, once wdec_func_iter_next() returned 0.
 *
 * Return: %WASMC_OK at the end, or the decoding error that stopped it.
 */
static inline wasmc_err_t wdec_func_iter_err(const wdec_func_iter_t *iter)
{
    return iter->err;
}

/**
 * wdec_func_count() - Total number of functions: imported plus defined.
 * @mod: a decoded module.
 *
 * Return: The total function count.
 */
uint32_t wdec_func_count(const wdec_module_t *mod);

/**
 * wdec_imported_func_count() - Number of imported functions.
 * @mod: a decoded module.
 *
 * These occupy function indices [0, wdec_imported_func_count()) in the
 * combined index space; defined functions follow at
 * [wdec_imported_func_count(), wdec_func_count()).
 *
 * Return: The imported function count.
 */
uint32_t wdec_imported_func_count(const wdec_module_t *mod);

/**
 * wdec_defined_func_count() - Number of locally defined functions.
 * @mod: a decoded module.
 *
 * Return: The defined function count (the function section's own entry
 * count).
 */
uint32_t wdec_defined_func_count(const wdec_module_t *mod);

/**
 * wdec_func_typeidx() - Look up a function's type index.
 * @mod: a decoded module.
 * @funcidx: function index in the combined (imports first) index
 *           space; works for both imported and defined functions.
 * @out_typeidx: filled with the type index on success.
 *
 * O(@funcidx): walks the function section from the start for a defined
 * function, or the import section up to the match for an imported one.
 *
 * Return: %WASMC_OK on success, %WASMC_ERR_FUNC_INDEX if @funcidx is out
 * of range, or another %WASMC_ERR_* if the import or function section
 * entry it reads is malformed.
 */
wasmc_err_t wdec_func_typeidx(const wdec_module_t *mod, uint32_t funcidx,
                              uint32_t *out_typeidx);

/**
 * wdec_func_type() - Look up a function's full type.
 * @mod: a decoded module.
 * @funcidx: function index in the combined index space; works for both
 *           imported and defined functions.
 * @out: filled with the decoded type on success.
 *
 * Equivalent to wdec_func_typeidx() followed by wdec_get_type().
 *
 * O(@funcidx + typeidx): it walks the function (or import) section and
 * then the type section, both from the start.
 *
 * Return: %WASMC_OK on success, %WASMC_ERR_FUNC_INDEX if @funcidx is out
 * of range, %WASMC_ERR_TYPE_INDEX if its type index is, or another
 * %WASMC_ERR_* if an entry read along the way is malformed.
 */
wasmc_err_t wdec_func_type(const wdec_module_t *mod, uint32_t funcidx,
                           wdec_functype_t *out);

/**
 * wdec_get_func() - Look up a defined function's body by index.
 * @mod: a decoded module.
 * @funcidx: function index in the combined index space.
 * @out: filled with the decoded function on success.
 *
 * Only valid for a *defined* funcidx (funcidx >=
 * wdec_imported_func_count()); imported functions have no body.
 *
 * O(@funcidx): walks the function and code sections from the start,
 * decoding the local declarations of every function before it. Prefer
 * wdec_func_iter_next() when visiting every function.
 *
 * Return: %WASMC_OK on success, %WASMC_ERR_FUNC_IS_IMPORTED if @funcidx
 * names an imported function, or %WASMC_ERR_FUNC_INDEX if it is out of
 * range entirely.
 */
wasmc_err_t wdec_get_func(const wdec_module_t *mod, uint32_t funcidx,
                          wdec_func_t *out);

/**
 * wdec_local_type() - Look up the type of a local variable.
 * @mod: a decoded module.
 * @func: a defined function of @mod, from wdec_get_func() or
 *     wdec_func_iter_next().
 * @localidx: local index: 0..param_count-1 are @func's parameters,
 *            followed by its declared locals in declaration order.
 * @out: filled with the value type on success.
 *
 * Performs a linear scan over the (typically few) local-declaration
 * groups rather than materializing a per-local array, to keep memory
 * use at O(1); functions with both a very large number of local
 * accesses and a very large number of distinct local-declaration groups
 * will pay for this scan in CPU time rather than RAM.
 *
 * It also looks up @func's type with wdec_get_type(), which is
 * O(@func->typeidx).
 *
 * Return: %WASMC_OK on success, %WASMC_ERR_LOCAL_INDEX if @localidx is
 * out of range, %WASMC_ERR_TYPE_INDEX if @func's type index is, or
 * another %WASMC_ERR_* if its type or local declarations are malformed.
 */
wasmc_err_t wdec_local_type(const wdec_module_t *mod, const wdec_func_t *func,
                            uint32_t localidx, wdec_valtype_t *out);

/**
 * struct wdec_br_table_t - A decoded br_table instruction's immediate
 * (spec 5.4.1).
 * @count: number of label indices in @labels.
 * @labels: raw vec(labelidx) of @count entries, each a u32 LEB; walk
 *          with wdec_br_table_label_iter_init() /
 *          wdec_br_table_label_iter_next() rather than reading directly.
 * @labels_end: end of @labels.
 * @default_label: the mandatory default label index that follows
 *                  @labels in the encoding.
 */
typedef struct {
    uint32_t count;
    const uint8_t *labels;
    const uint8_t *labels_end;
    uint32_t default_label;
} wdec_br_table_t;

/**
 * struct wdec_br_table_label_iter_t - Iterator state for
 * wdec_br_table_label_iter_init() / wdec_br_table_label_iter_next().
 * @cur: current read position. Opaque; do not access directly.
 * @end: end of the label list. Opaque; do not access directly.
 * @err: why the iteration stopped, %WASMC_OK until an error; read it
 *       with wdec_br_table_label_iter_err().
 */
typedef struct {
    const uint8_t *cur, *end;
    wasmc_err_t err;
} wdec_br_table_label_iter_t;

/**
 * wdec_br_table_label_iter_init() - Start an iterator over a decoded
 * br_table instruction's label list.
 * @iter: the iterator to set up, positioned before the first label index.
 * @tbl: the br_table immediate from a decoded &wdec_instr_t.
 */
void wdec_br_table_label_iter_init(wdec_br_table_label_iter_t *iter,
                                   const wdec_br_table_t *tbl);

/**
 * wdec_br_table_label_iter_next() - Decode the next label index.
 * @iter: iterator, advanced on success.
 * @out: filled with the label index on success.
 *
 * Return: 1 if @out was filled, 0 when iteration is done (check
 * wdec_br_table_label_iter_err()).
 */
int wdec_br_table_label_iter_next(wdec_br_table_label_iter_t *iter,
                                  uint32_t *out);

/**
 * WDEC_FOR_EACH_BR_TABLE_LABEL() - Loop over the label indices of a br_table.
 * @iter: a &wdec_br_table_label_iter_t, declared by the caller.
 * @label: a uint32_t, declared by the caller; the current one.
 * @tbl: the br_table, as for wdec_br_table_label_iter_init().
 *
 * Expands to a for statement, so 'break' and 'continue' work as usual. When
 * the loop ends by itself, check wdec_br_table_label_iter_err(&@iter) to tell
 * the end from a decoding error.
 */
#define WDEC_FOR_EACH_BR_TABLE_LABEL(iter, label, tbl)                         \
    for (wdec_br_table_label_iter_init(&(iter), (tbl));                        \
         wdec_br_table_label_iter_next(&(iter), &(label));)

/**
 * wdec_br_table_label_iter_err() - Why an iteration over the label list
 * stopped.
 * @iter: the iterator, once wdec_br_table_label_iter_next() returned 0.
 *
 * Return: %WASMC_OK at the end, or the decoding error that stopped it.
 */
static inline wasmc_err_t
wdec_br_table_label_iter_err(const wdec_br_table_label_iter_t *iter)
{
    return iter->err;
}

/**
 * struct wdec_instr_t - One decoded instruction: an opcode plus
 * whatever immediate operand(s) WebAssembly 1.0's binary format
 * defines for it (spec 5.4).
 * @at: pointer to the opcode byte itself - useful for reporting a byte
 *      offset (via `at - func.body`) or for resuming iteration from a
 *      specific instruction.
 * @opcode: which instruction this is; also determines which member of
 *          @imm (if any) is meaningful:
 *
 *          WDEC_OP_BLOCK/LOOP/IF              -> imm.blocktype
 *          WDEC_OP_BR/BR_IF                   -> imm.index (a label index)
 *          WDEC_OP_BR_TABLE                   -> imm.br_table
 *          WDEC_OP_CALL                       -> imm.index (a function index)
 *          WDEC_OP_CALL_INDIRECT              -> imm.index (a type index)
 *          WDEC_OP_LOCAL_GET/SET/TEE          -> imm.index (a local index)
 *          WDEC_OP_GLOBAL_GET/SET             -> imm.index (a global index)
 *          loads and stores (WDEC_OP_I32_LOAD, WDEC_OP_I64_STORE16, ...)
 *                                              -> imm.memarg
 *          WDEC_OP_I32_CONST                  -> imm.i32
 *          WDEC_OP_I64_CONST                  -> imm.i64
 *          WDEC_OP_F32_CONST                  -> imm.f32_bytes (4
 *                                                 little-endian bytes)
 *          WDEC_OP_F64_CONST                  -> imm.f64_bytes (8
 *                                                 little-endian bytes)
 *          everything else                    -> no immediate, @imm unused
 * @imm: the decoded immediate operand, interpreted per @opcode as above.
 */
typedef struct {
    const uint8_t *at;
    wdec_opcode_t opcode;
    union {
        wdec_valtype_t blocktype;
        uint32_t index;
        struct {
            uint32_t align;
            uint32_t offset;
        } memarg;
        int32_t i32;
        int64_t i64;
        const uint8_t *f32_bytes;
        const uint8_t *f64_bytes;
        wdec_br_table_t br_table;
    } imm;
} wdec_instr_t;

/**
 * struct wdec_instr_iter_t - Iterator state for wdec_instr_iter_init()
 * / wdec_instr_iter_next().
 * @cur: current read position. Opaque; do not access directly.
 * @end: end of the function body. Opaque; do not access directly.
 * @err: why the iteration stopped, %WASMC_OK until an error; read it
 *       with wdec_instr_iter_err().
 *
 * Sequential, allocation-free iterator over the raw instructions of one
 * function body. This is the recommended way to walk a function's code:
 * unlike wdec_validate_func(), a linear syntactic walk needs no
 * operand/control stack - only these two cursor pointers - since it
 * does not type-check or track control-flow nesting on the caller's
 * behalf (nothing to underflow or overflow). It decodes every
 * instruction in [body, body_end), including the function's closing
 * 'end'.
 *
 * It shares its per-opcode decoding with the validator (both go through
 * the same internal decoder), so the two never disagree about how an
 * instruction is shaped; what this iterator does *not* do is check that
 * the decoded instructions are well-typed or well-nested - it will
 * happily hand you the instructions of, say, a function whose stack
 * underflows, deferring that judgment to wdec_validate_func(). If you
 * only need to inspect code (a disassembler, an offline analysis, a
 * search for a specific opcode) and don't need type information, this
 * iterator alone is enough and is cheaper to set up.
 *
 * To track nesting depth for indentation or to find the matching 'end'
 * of a block, maintain your own counter in the consuming loop: increment
 * it on WDEC_OP_BLOCK/LOOP/IF, decrement it on WDEC_OP_END.
 */
typedef struct {
    const uint8_t *cur, *end;
    wasmc_err_t err;
} wdec_instr_iter_t;

/**
 * wdec_instr_iter_init() - Start a forward iterator over one function
 * body's raw instructions.
 * @iter: the iterator to set up, positioned before the first instruction.
 * @func: a function obtained from wdec_get_func() or wdec_func_iter_next().
 */
void wdec_instr_iter_init(wdec_instr_iter_t *iter, const wdec_func_t *func);

/**
 * wdec_instr_iter_next() - Decode the next instruction.
 * @iter: iterator, advanced on success.
 * @out: filled with the decoded instruction on success.
 *
 * Purely syntactic: does not type-check or track control-flow nesting,
 * so it needs no operand/control stack (unlike wdec_validate_func()).
 * See &wdec_instr_t for how to interpret the decoded instruction.
 *
 * Return: 1 if @out was filled, 0 when iteration is done (check
 * wdec_instr_iter_err()).
 */
int wdec_instr_iter_next(wdec_instr_iter_t *iter, wdec_instr_t *out);

/**
 * WDEC_FOR_EACH_INSTR() - Loop over the instructions of a function body.
 * @iter: a &wdec_instr_iter_t, declared by the caller; started at the first
 *        instruction.
 * @ins: a &wdec_instr_t, declared by the caller; the current instruction.
 * @func: the function, as for wdec_instr_iter_init().
 *
 * Expands to a for statement, so 'break' and 'continue' work as usual. When
 * the loop ends by itself, check wdec_instr_iter_err(&@iter) to tell the end of
 * the body from a decoding error.
 */
#define WDEC_FOR_EACH_INSTR(iter, ins, func)                                   \
    for (wdec_instr_iter_init(&(iter), (func));                                \
         wdec_instr_iter_next(&(iter), &(ins));)

/**
 * wdec_instr_iter_err() - Why an iteration over the function body stopped.
 * @iter: the iterator, once wdec_instr_iter_next() returned 0.
 *
 * Return: %WASMC_OK at the end, or the decoding error that stopped it.
 */
static inline wasmc_err_t wdec_instr_iter_err(const wdec_instr_iter_t *iter)
{
    return iter->err;
}

/* Tables section */

/**
 * struct wdec_table_t - A decoded table (spec 5.5.7).
 * @elemtype: the table's element type; always %WDEC_TYPE_FUNCREF in
 *            WebAssembly 1.0.
 * @limits: the table's size limits, in elements.
 */
typedef struct {
    uint8_t elemtype;
    wdec_limits_t limits;
} wdec_table_t;

/**
 * struct wdec_table_iter_t - Iterator state for wdec_table_iter_init()
 * / wdec_table_iter_next().
 * @cur: current read position. Opaque; do not access directly.
 * @end: end of the table section. Opaque; do not access directly.
 * @err: why the iteration stopped, %WASMC_OK until an error; read it
 *       with wdec_table_iter_err().
 */
typedef struct {
    const uint8_t *cur;
    const uint8_t *end;
    wasmc_err_t err;
} wdec_table_iter_t;

/**
 * wdec_table_iter_init() - Start a forward iterator over the table
 * section.
 * @iter: the iterator to set up, positioned before the first locally
 *        defined table.
 * @mod: a decoded module.
 */
void wdec_table_iter_init(wdec_table_iter_t *iter, const wdec_module_t *mod);

/**
 * wdec_table_iter_next() - Decode the next table.
 * @iter: iterator, advanced on success.
 * @out: filled with the decoded table on success.
 *
 * Return: 1 if @out was filled, 0 when iteration is done (check
 * wdec_table_iter_err()).
 */
int wdec_table_iter_next(wdec_table_iter_t *iter, wdec_table_t *out);

/**
 * WDEC_FOR_EACH_TABLE() - Loop over the tables of a module.
 * @iter: a &wdec_table_iter_t, declared by the caller.
 * @table: a &wdec_table_t, declared by the caller; the current one.
 * @mod: the module, as for wdec_table_iter_init().
 *
 * Expands to a for statement, so 'break' and 'continue' work as usual. When
 * the loop ends by itself, check wdec_table_iter_err(&@iter) to tell the end
 * from a decoding error.
 */
#define WDEC_FOR_EACH_TABLE(iter, table, mod)                                  \
    for (wdec_table_iter_init(&(iter), (mod));                                 \
         wdec_table_iter_next(&(iter), &(table));)

/**
 * wdec_table_iter_err() - Why an iteration over the table section stopped.
 * @iter: the iterator, once wdec_table_iter_next() returned 0.
 *
 * Return: %WASMC_OK at the end, or the decoding error that stopped it.
 */
static inline wasmc_err_t wdec_table_iter_err(const wdec_table_iter_t *iter)
{
    return iter->err;
}

/**
 * wdec_table_count() - Number of locally defined tables.
 * @mod: a decoded module.
 *
 * Does not include imported tables. WebAssembly 1.0 allows at most one
 * table in total (imported plus defined), checked by wdec_validate().
 *
 * Return: The locally defined table count.
 */
uint32_t wdec_table_count(const wdec_module_t *mod);

/**
 * wdec_get_table() - Look up a locally defined table by index.
 * @mod: a decoded module.
 * @idx: table index among locally defined tables (not the combined
 *       index space), checked against wdec_table_count().
 * @out: filled with the decoded table on success.
 *
 * O(@idx): walks the table section from the start. Prefer
 * wdec_table_iter_next() when visiting every table.
 *
 * Return: %WASMC_OK on success, or %WASMC_ERR_TABLE_INDEX if @idx is out
 * of range.
 */
wasmc_err_t wdec_get_table(const wdec_module_t *mod, uint32_t idx,
                           wdec_table_t *out);

/* Memories section */

/**
 * struct wdec_mem_iter_t - Iterator state for wdec_mem_iter_init() /
 * wdec_mem_iter_next().
 * @cur: current read position. Opaque; do not access directly.
 * @end: end of the memory section. Opaque; do not access directly.
 * @err: why the iteration stopped, %WASMC_OK until an error; read it
 *       with wdec_mem_iter_err().
 */
typedef struct {
    const uint8_t *cur;
    const uint8_t *end;
    wasmc_err_t err;
} wdec_mem_iter_t;

/**
 * wdec_mem_iter_init() - Start a forward iterator over the memory
 * section.
 * @iter: the iterator to set up, positioned before the first locally
 *        defined memory.
 * @mod: a decoded module.
 */
void wdec_mem_iter_init(wdec_mem_iter_t *iter, const wdec_module_t *mod);

/**
 * wdec_mem_iter_next() - Decode the next memory.
 * @iter: iterator, advanced on success.
 * @out: filled with the decoded memory's limits on success.
 *
 * Return: 1 if @out was filled, 0 when iteration is done (check
 * wdec_mem_iter_err()).
 */
int wdec_mem_iter_next(wdec_mem_iter_t *iter, wdec_limits_t *out);

/**
 * WDEC_FOR_EACH_MEM() - Loop over the memories of a module.
 * @iter: a &wdec_mem_iter_t, declared by the caller.
 * @lim: a &wdec_limits_t, declared by the caller; the current one.
 * @mod: the module, as for wdec_mem_iter_init().
 *
 * Expands to a for statement, so 'break' and 'continue' work as usual. When
 * the loop ends by itself, check wdec_mem_iter_err(&@iter) to tell the end from
 * a decoding error.
 */
#define WDEC_FOR_EACH_MEM(iter, lim, mod)                                      \
    for (wdec_mem_iter_init(&(iter), (mod));                                   \
         wdec_mem_iter_next(&(iter), &(lim));)

/**
 * wdec_mem_iter_err() - Why an iteration over the memory section stopped.
 * @iter: the iterator, once wdec_mem_iter_next() returned 0.
 *
 * Return: %WASMC_OK at the end, or the decoding error that stopped it.
 */
static inline wasmc_err_t wdec_mem_iter_err(const wdec_mem_iter_t *iter)
{
    return iter->err;
}

/**
 * wdec_mem_count() - Number of locally defined memories.
 * @mod: a decoded module.
 *
 * Does not include imported memories. WebAssembly 1.0 allows at most
 * one memory in total (imported plus defined), checked by
 * wdec_validate().
 *
 * Return: The locally defined memory count.
 */
uint32_t wdec_mem_count(const wdec_module_t *mod);

/**
 * wdec_get_mem() - Look up a locally defined memory by index.
 * @mod: a decoded module.
 * @idx: memory index among locally defined memories, checked against
 *       wdec_mem_count().
 * @out: filled with the decoded limits on success.
 *
 * O(@idx): walks the memory section from the start. Prefer
 * wdec_mem_iter_next() when visiting every memory.
 *
 * Return: %WASMC_OK on success, or %WASMC_ERR_MEM_INDEX if @idx is out of
 * range.
 */
wasmc_err_t wdec_get_mem(const wdec_module_t *mod, uint32_t idx,
                         wdec_limits_t *out);

/* Globals section */

/**
 * struct wdec_global_t - A decoded global (spec 5.5.9).
 * @valtype: the global's value type.
 * @mut: mutability: 0 (const) or 1 (var).
 * @init_expr: first byte of the constant initializer expression.
 * @init_expr_end: one past the initializer's closing 'end'.
 */
typedef struct {
    wdec_valtype_t valtype;
    uint8_t mut;
    const uint8_t *init_expr;
    const uint8_t *init_expr_end;
} wdec_global_t;

/**
 * struct wdec_global_iter_t - Iterator state for
 * wdec_global_iter_init() / wdec_global_iter_next().
 * @cur: current read position. Opaque; do not access directly.
 * @end: end of the global section. Opaque; do not access directly.
 * @err: why the iteration stopped, %WASMC_OK until an error; read it
 *       with wdec_global_iter_err().
 */
typedef struct {
    const uint8_t *cur, *end;
    wasmc_err_t err;
} wdec_global_iter_t;

/**
 * wdec_global_iter_init() - Start a forward iterator over the global
 * section.
 * @iter: the iterator to set up, positioned before the first locally
 *        defined global.
 * @mod: a decoded module.
 */
void wdec_global_iter_init(wdec_global_iter_t *iter, const wdec_module_t *mod);

/**
 * wdec_global_iter_next() - Decode the next global.
 * @iter: iterator, advanced on success.
 * @out: filled with the decoded global on success.
 *
 * Return: 1 if @out was filled, 0 when iteration is done (check
 * wdec_global_iter_err()).
 */
int wdec_global_iter_next(wdec_global_iter_t *iter, wdec_global_t *out);

/**
 * WDEC_FOR_EACH_GLOBAL() - Loop over the globals of a module.
 * @iter: a &wdec_global_iter_t, declared by the caller.
 * @glob: a &wdec_global_t, declared by the caller; the current one.
 * @mod: the module, as for wdec_global_iter_init().
 *
 * Expands to a for statement, so 'break' and 'continue' work as usual. When
 * the loop ends by itself, check wdec_global_iter_err(&@iter) to tell the end
 * from a decoding error.
 */
#define WDEC_FOR_EACH_GLOBAL(iter, glob, mod)                                  \
    for (wdec_global_iter_init(&(iter), (mod));                                \
         wdec_global_iter_next(&(iter), &(glob));)

/**
 * wdec_global_iter_err() - Why an iteration over the global section stopped.
 * @iter: the iterator, once wdec_global_iter_next() returned 0.
 *
 * Return: %WASMC_OK at the end, or the decoding error that stopped it.
 */
static inline wasmc_err_t wdec_global_iter_err(const wdec_global_iter_t *iter)
{
    return iter->err;
}

/**
 * wdec_global_count() - Number of locally defined globals.
 * @mod: a decoded module.
 *
 * Does not include imported globals.
 *
 * Return: The locally defined global count.
 */
uint32_t wdec_global_count(const wdec_module_t *mod);

/**
 * wdec_get_global() - Look up a locally defined global by index.
 * @mod: a decoded module.
 * @idx: global index among locally defined globals (not the combined
 *       index space), checked against wdec_global_count().
 * @out: filled with the decoded global on success.
 *
 * O(@idx): walks the global section from the start. Prefer
 * wdec_global_iter_next() when visiting every global.
 *
 * Return: %WASMC_OK on success, or %WASMC_ERR_GLOBAL_INDEX if @idx is out
 * of range.
 */
wasmc_err_t wdec_get_global(const wdec_module_t *mod, uint32_t idx,
                            wdec_global_t *out);

/**
 * wdec_global_type() - Look up a global's value type and mutability.
 * @mod: a decoded module.
 * @globalidx: global index in the combined (imports first) index
 *             space; works for both imported and locally defined
 *             globals.
 * @out: filled with the global's value type and mutability on success.
 *
 * O(@globalidx): walks the global section from the start for a locally
 * defined global, or the import section up to the match for an imported
 * one.
 *
 * Return: %WASMC_OK on success, or %WASMC_ERR_GLOBAL_INDEX if @globalidx
 * is out of range.
 */
wasmc_err_t wdec_global_type(const wdec_module_t *mod, uint32_t globalidx,
                             wdec_globaltype_t *out);

/* Exports section */

/**
 * struct wdec_export_t - A decoded export (spec 5.5.10).
 * @name: the exported name. Not NUL-terminated.
 * @name_len: length of @name in bytes.
 * @kind: which index space @index refers into: one of %WDEC_EXTKIND_*.
 * @index: index, in the combined (imports first) index space, of the
 *         exported item.
 */
typedef struct {
    const char *name;
    uint32_t name_len;
    uint8_t kind;
    uint32_t index;
} wdec_export_t;

/**
 * struct wdec_export_iter_t - Iterator state for
 * wdec_export_iter_init() / wdec_export_iter_next().
 * @cur: current read position. Opaque; do not access directly.
 * @end: end of the export section. Opaque; do not access directly.
 * @err: why the iteration stopped, %WASMC_OK until an error; read it
 *       with wdec_export_iter_err().
 */
typedef struct {
    const uint8_t *cur;
    const uint8_t *end;
    wasmc_err_t err;
} wdec_export_iter_t;

/**
 * wdec_export_iter_init() - Start a forward iterator over the export
 * section.
 * @iter: the iterator to set up, positioned before the first export.
 * @mod: a decoded module.
 */
void wdec_export_iter_init(wdec_export_iter_t *iter, const wdec_module_t *mod);

/**
 * wdec_export_iter_next() - Decode the next export.
 * @iter: iterator, advanced on success.
 * @out: filled with the decoded export on success.
 *
 * Return: 1 if @out was filled, 0 when iteration is done (check
 * wdec_export_iter_err()).
 */
int wdec_export_iter_next(wdec_export_iter_t *iter, wdec_export_t *out);

/**
 * WDEC_FOR_EACH_EXPORT() - Loop over the exports of a module.
 * @iter: a &wdec_export_iter_t, declared by the caller.
 * @exp: a &wdec_export_t, declared by the caller; the current one.
 * @mod: the module, as for wdec_export_iter_init().
 *
 * Expands to a for statement, so 'break' and 'continue' work as usual. When
 * the loop ends by itself, check wdec_export_iter_err(&@iter) to tell the end
 * from a decoding error.
 */
#define WDEC_FOR_EACH_EXPORT(iter, exp, mod)                                   \
    for (wdec_export_iter_init(&(iter), (mod));                                \
         wdec_export_iter_next(&(iter), &(exp));)

/**
 * wdec_export_iter_err() - Why an iteration over the export section stopped.
 * @iter: the iterator, once wdec_export_iter_next() returned 0.
 *
 * Return: %WASMC_OK at the end, or the decoding error that stopped it.
 */
static inline wasmc_err_t wdec_export_iter_err(const wdec_export_iter_t *iter)
{
    return iter->err;
}

/**
 * wdec_export_count() - Number of entries in the export section.
 * @mod: a decoded module.
 *
 * Return: The export count.
 */
uint32_t wdec_export_count(const wdec_module_t *mod);

/**
 * wdec_get_export() - Look up an export by index.
 * @mod: a decoded module.
 * @idx: export index, checked against wdec_export_count().
 * @out: filled with the decoded export on success.
 *
 * O(@idx): walks the export section from the start. Prefer
 * wdec_export_iter_next() when visiting every export.
 *
 * Return: %WASMC_OK on success, or %WASMC_ERR_INVALID_ARG if @idx is out
 * of range.
 */
wasmc_err_t wdec_get_export(const wdec_module_t *mod, uint32_t idx,
                            wdec_export_t *out);

/**
 * wdec_find_export() - Look up an export by name and kind.
 * @mod: a decoded module.
 * @name: the name to search for. Need not be NUL-terminated.
 * @name_len: length of @name in bytes.
 * @kind: the export kind to match, one of %WDEC_EXTKIND_*.
 * @out: filled with the matching export on success.
 *
 * O(wdec_export_count()): walks the export section from the start up to
 * the match, comparing names, with no allocation. Prefer
 * wdec_export_iter_next() when matching several names.
 *
 * Return: %WASMC_OK if a matching export was found, or
 * %WASMC_ERR_NOT_FOUND otherwise.
 */
wasmc_err_t wdec_find_export(const wdec_module_t *mod, const char *name,
                             uint32_t name_len, uint8_t kind,
                             wdec_export_t *out);

/* Element section */

/**
 * struct wdec_elem_t - A decoded element segment (spec 5.5.12).
 * @tableidx: index of the table this segment initializes.
 * @offset_expr: first byte of the constant offset expression.
 * @offset_expr_end: one past the offset expression's closing 'end'.
 * @func_count: number of function indices in @funcidxs.
 * @funcidxs: raw vec(funcidx) of @func_count entries, each a u32 LEB;
 *            walk with wdec_elem_funcidx_iter_init() /
 *            wdec_elem_funcidx_iter_next() rather than reading directly.
 * @funcidxs_end: end of @funcidxs.
 */
typedef struct {
    uint32_t tableidx;
    const uint8_t *offset_expr;
    const uint8_t *offset_expr_end;
    uint32_t func_count;
    const uint8_t *funcidxs;
    const uint8_t *funcidxs_end;
} wdec_elem_t;

/**
 * struct wdec_elem_iter_t - Iterator state for wdec_elem_iter_init() /
 * wdec_elem_iter_next().
 * @cur: current read position. Opaque; do not access directly.
 * @end: end of the element section. Opaque; do not access directly.
 * @err: why the iteration stopped, %WASMC_OK until an error; read it
 *       with wdec_elem_iter_err().
 */
typedef struct {
    const uint8_t *cur;
    const uint8_t *end;
    wasmc_err_t err;
} wdec_elem_iter_t;

/**
 * wdec_elem_iter_init() - Start a forward iterator over the element
 * section.
 * @iter: the iterator to set up, positioned before the first element segment.
 * @mod: a decoded module.
 */
void wdec_elem_iter_init(wdec_elem_iter_t *iter, const wdec_module_t *mod);

/**
 * wdec_elem_iter_next() - Decode the next element segment.
 * @iter: iterator, advanced on success.
 * @out: filled with the decoded element segment on success.
 *
 * Return: 1 if @out was filled, 0 when iteration is done (check
 * wdec_elem_iter_err()).
 */
int wdec_elem_iter_next(wdec_elem_iter_t *iter, wdec_elem_t *out);

/**
 * WDEC_FOR_EACH_ELEM() - Loop over the element segments of a module.
 * @iter: a &wdec_elem_iter_t, declared by the caller.
 * @elem: a &wdec_elem_t, declared by the caller; the current one.
 * @mod: the module, as for wdec_elem_iter_init().
 *
 * Expands to a for statement, so 'break' and 'continue' work as usual. When
 * the loop ends by itself, check wdec_elem_iter_err(&@iter) to tell the end
 * from a decoding error.
 */
#define WDEC_FOR_EACH_ELEM(iter, elem, mod)                                    \
    for (wdec_elem_iter_init(&(iter), (mod));                                  \
         wdec_elem_iter_next(&(iter), &(elem));)

/**
 * wdec_elem_iter_err() - Why an iteration over the element section stopped.
 * @iter: the iterator, once wdec_elem_iter_next() returned 0.
 *
 * Return: %WASMC_OK at the end, or the decoding error that stopped it.
 */
static inline wasmc_err_t wdec_elem_iter_err(const wdec_elem_iter_t *iter)
{
    return iter->err;
}

/**
 * wdec_elem_count() - Number of entries in the element section.
 * @mod: a decoded module.
 *
 * Return: The element segment count.
 */
uint32_t wdec_elem_count(const wdec_module_t *mod);

/**
 * wdec_get_elem() - Look up an element segment by index.
 * @mod: a decoded module.
 * @idx: element segment index, checked against wdec_elem_count().
 * @out: filled with the decoded element segment on success.
 *
 * O(@idx): walks the element section from the start. Prefer
 * wdec_elem_iter_next() when visiting every segment.
 *
 * Return: %WASMC_OK on success, or %WASMC_ERR_INVALID_ARG if @idx is out
 * of range.
 */
wasmc_err_t wdec_get_elem(const wdec_module_t *mod, uint32_t idx,
                          wdec_elem_t *out);

/**
 * struct wdec_elem_funcidx_iter_t - Iterator state for
 * wdec_elem_funcidx_iter_init() / wdec_elem_funcidx_iter_next().
 * @cur: current read position. Opaque; do not access directly.
 * @end: end of the segment's function index list. Opaque; do not
 *       access directly.
 * @err: why the iteration stopped, %WASMC_OK until an error; read it
 *       with wdec_elem_funcidx_iter_err().
 */
typedef struct {
    const uint8_t *cur, *end;
    wasmc_err_t err;
} wdec_elem_funcidx_iter_t;

/**
 * wdec_elem_funcidx_iter_init() - Start an iterator over one element
 * segment's function indices.
 * @iter: the iterator to set up, positioned before the first function index.
 * @elem: an element segment from wdec_get_elem() or wdec_elem_iter_next().
 */
void wdec_elem_funcidx_iter_init(wdec_elem_funcidx_iter_t *iter,
                                 const wdec_elem_t *elem);

/**
 * wdec_elem_funcidx_iter_next() - Decode the next function index.
 * @iter: iterator, advanced on success.
 * @out: filled with the function index on success.
 *
 * Return: 1 if @out was filled, 0 when iteration is done (check
 * wdec_elem_funcidx_iter_err()).
 */
int wdec_elem_funcidx_iter_next(wdec_elem_funcidx_iter_t *iter, uint32_t *out);

/**
 * WDEC_FOR_EACH_ELEM_FUNCIDX() - Loop over the function indices of an element
 * segment.
 * @iter: a &wdec_elem_funcidx_iter_t, declared by the caller.
 * @fidx: an uint32_t, declared by the caller; the current one.
 * @elem: the element segment, as for wdec_elem_funcidx_iter_init().
 *
 * Expands to a for statement, so 'break' and 'continue' work as usual. When
 * the loop ends by itself, check wdec_elem_funcidx_iter_err(&@iter) to tell the
 * end from a decoding error.
 */
#define WDEC_FOR_EACH_ELEM_FUNCIDX(iter, fidx, elem)                           \
    for (wdec_elem_funcidx_iter_init(&(iter), (elem));                         \
         wdec_elem_funcidx_iter_next(&(iter), &(fidx));)

/**
 * wdec_elem_funcidx_iter_err() - Why an iteration over the segment's function
 * indices stopped.
 * @iter: the iterator, once wdec_elem_funcidx_iter_next() returned 0.
 *
 * Return: %WASMC_OK at the end, or the decoding error that stopped it.
 */
static inline wasmc_err_t
wdec_elem_funcidx_iter_err(const wdec_elem_funcidx_iter_t *iter)
{
    return iter->err;
}

/* Data section */

/**
 * struct wdec_data_t - A decoded data segment (spec 5.5.14).
 * @memidx: index of the memory this segment initializes.
 * @offset_expr: first byte of the constant offset expression.
 * @offset_expr_end: one past the offset expression's closing 'end'.
 * @data_len: length of @data in bytes.
 * @data: the segment's raw initializer bytes.
 */
typedef struct {
    uint32_t memidx;
    const uint8_t *offset_expr;
    const uint8_t *offset_expr_end;
    uint32_t data_len;
    const uint8_t *data;
} wdec_data_t;

/**
 * struct wdec_data_iter_t - Iterator state for wdec_data_iter_init() /
 * wdec_data_iter_next().
 * @cur: current read position. Opaque; do not access directly.
 * @end: end of the data section. Opaque; do not access directly.
 * @err: why the iteration stopped, %WASMC_OK until an error; read it
 *       with wdec_data_iter_err().
 */
typedef struct {
    const uint8_t *cur;
    const uint8_t *end;
    wasmc_err_t err;
} wdec_data_iter_t;

/**
 * wdec_data_iter_init() - Start a forward iterator over the data
 * section.
 * @iter: the iterator to set up, positioned before the first data segment.
 * @mod: a decoded module.
 */
void wdec_data_iter_init(wdec_data_iter_t *iter, const wdec_module_t *mod);

/**
 * wdec_data_iter_next() - Decode the next data segment.
 * @iter: iterator, advanced on success.
 * @out: filled with the decoded data segment on success.
 *
 * Return: 1 if @out was filled, 0 when iteration is done (check
 * wdec_data_iter_err()).
 */
int wdec_data_iter_next(wdec_data_iter_t *iter, wdec_data_t *out);

/**
 * WDEC_FOR_EACH_DATA() - Loop over the data segments of a module.
 * @iter: a &wdec_data_iter_t, declared by the caller.
 * @data: a &wdec_data_t, declared by the caller; the current one.
 * @mod: the module, as for wdec_data_iter_init().
 *
 * Expands to a for statement, so 'break' and 'continue' work as usual. When
 * the loop ends by itself, check wdec_data_iter_err(&@iter) to tell the end
 * from a decoding error.
 */
#define WDEC_FOR_EACH_DATA(iter, data, mod)                                    \
    for (wdec_data_iter_init(&(iter), (mod));                                  \
         wdec_data_iter_next(&(iter), &(data));)

/**
 * wdec_data_iter_err() - Why an iteration over the data section stopped.
 * @iter: the iterator, once wdec_data_iter_next() returned 0.
 *
 * Return: %WASMC_OK at the end, or the decoding error that stopped it.
 */
static inline wasmc_err_t wdec_data_iter_err(const wdec_data_iter_t *iter)
{
    return iter->err;
}

/**
 * wdec_data_count() - Number of entries in the data section.
 * @mod: a decoded module.
 *
 * Return: The data segment count.
 */
uint32_t wdec_data_count(const wdec_module_t *mod);

/**
 * wdec_get_data() - Look up a data segment by index.
 * @mod: a decoded module.
 * @idx: data segment index, checked against wdec_data_count().
 * @out: filled with the decoded data segment on success.
 *
 * O(@idx): walks the data section from the start. Prefer
 * wdec_data_iter_next() when visiting every segment.
 *
 * Return: %WASMC_OK on success, or %WASMC_ERR_INVALID_ARG if @idx is out
 * of range.
 */
wasmc_err_t wdec_get_data(const wdec_module_t *mod, uint32_t idx,
                          wdec_data_t *out);

/* Start section */

/**
 * wdec_has_start() - Whether the module declares a start function.
 * @mod: a decoded module.
 *
 * Return: Non-zero if a start section is present.
 */
int wdec_has_start(const wdec_module_t *mod);

/**
 * wdec_start_funcidx() - The module's start function index.
 * @mod: a decoded module.
 *
 * Only meaningful when wdec_has_start() is non-zero.
 *
 * Return: The start function's index in the combined index space.
 */
uint32_t wdec_start_funcidx(const wdec_module_t *mod);

/* Function-body validation */

/**
 * struct wdec_ctrl_frame_t - One entry of the control-frame stack from
 * spec 7.3.1.
 * @opcode: which construct opened this frame: %WDEC_OP_BLOCK,
 *          %WDEC_OP_LOOP, or %WDEC_OP_IF (the function body itself also
 *          uses %WDEC_OP_BLOCK as its implicit outer frame).
 * @label_type: type expected/produced by branching to this frame's
 *              label, or %WDEC_TYPE_EMPTY. WebAssembly 1.0 blocks carry
 *              at most one result value, so a single value type
 *              suffices where the spec uses a list.
 * @end_type: type expected/produced at this frame's 'end', or
 *            %WDEC_TYPE_EMPTY.
 * @else_seen: only meaningful when @opcode is %WDEC_OP_IF: non-zero
 *             once a matching 'else' has been seen.
 * @unreachable: non-zero once code in this frame has become
 *               unreachable (spec 7.3.1's stack-polymorphism flag).
 * @height: operand stack height at this frame's entry.
 *
 * Caller-supplied storage for wdec_validate_func()'s control stack; see
 * wdec_validator_init(). Not meant to be constructed or read directly.
 */
typedef struct {
    wdec_opcode_t opcode;
    wdec_valtype_t label_type;
    wdec_valtype_t end_type;
    uint8_t else_seen;
    uint8_t unreachable;
    uint32_t height;
} wdec_ctrl_frame_t;

/**
 * struct wdec_validator_t - Function-body validation context.
 * @buf: caller-owned work buffer shared by both stacks.
 * @buf_len: size of @buf, in bytes.
 * @ctrl_end: byte offset into @buf one past the control stack's bottom
 *            frame (the address @buf + @buf_len rounded down so every
 *            frame is aligned).
 * @opd_top: current operand stack height, in entries.
 * @ctrl_top: current control stack depth, in frames.
 * @opd_max: highest @opd_top reached since wdec_validator_init().
 * @ctrl_max: highest @ctrl_top reached since wdec_validator_init().
 *
 * All fields are opaque; do not access them directly.
 *
 * Bound to a caller-supplied buffer by wdec_validator_init() and driven
 * by wdec_validate_func() / wdec_validate(); see wdec_validator_init()
 * for the buffer layout. Apart from this struct, its buffer and
 * wdec_validate()'s optional @out_bad_funcidx, wdec_validate_func() and
 * wdec_validate() write no memory beyond their own stack frame.
 */
typedef struct {
    uint8_t *buf;
    uint32_t buf_len;
    uint32_t ctrl_end;

    uint32_t opd_top;
    uint32_t ctrl_top;

    uint32_t opd_max;
    uint32_t ctrl_max;
} wdec_validator_t;

/**
 * wdec_validator_init() - Bind a caller-supplied work buffer to a
 * validator context.
 * @validator: validator context to initialize.
 * @buf: caller-owned work buffer of @buf_len bytes. Any alignment.
 * @buf_len: size of @buf, in bytes.
 *
 * Both validation stacks of spec 7.3.1 share @buf and grow towards each
 * other:
 *
 *   buf[0]                                                buf[buf_len]
 *   | opd opd opd -->        free         <-- ctrl ctrl ctrl |
 *
 *   - the operand-type stack grows left to right from buf[0],
 *     1 byte (one &wdec_valtype_t) per entry;
 *   - the control-frame stack grows right to left from the end of @buf,
 *     sizeof(wdec_ctrl_frame_t) bytes (typically 12) per frame.
 *     Up to _Alignof(wdec_ctrl_frame_t) - 1 bytes at the very end of @buf
 *     may go unused, to keep every frame aligned.
 *
 * The validator fails only when the two stacks would actually collide,
 * so a function with a deep operand stack but shallow nesting (or vice
 * versa) can use the whole buffer.
 *
 * No memory is allocated by wdec; @buf may live on the stack, in a static
 * array, or on the heap - the caller's choice. The same @validator may (and, to
 * minimize memory use, should) be reused across multiple
 * wdec_validate_func() calls: each call resets both stacks.
 *
 * If a function needs more room than @buf provides, wdec_validate_func()
 * returns %WASMC_ERR_OPD_STACK_OVERFLOW or %WASMC_ERR_CTRL_STACK_OVERFLOW
 * (whichever push hit the collision) rather than overrunning @buf.
 */
void wdec_validator_init(wdec_validator_t *validator, uint8_t *buf,
                         uint32_t buf_len);

/**
 * struct wdec_stack_depth - Depths of the two validation stacks.
 * @opd: Operand-stack height.
 * @ctrl: Control-stack depth.
 */
typedef struct {
    uint32_t opd;
    uint32_t ctrl;
} wdec_stack_depth;

/**
 * wdec_validator_max_depth() - Report the deepest stacks seen so far.
 * @validator: a validator context.
 *
 * High-water marks across every wdec_validate_func() call since
 * wdec_validator_init() (they are not reset per function). After a
 * successful wdec_validate() these bound the stack depths of every
 * function in the module - see wcompile_func().
 *
 * Return: the maximum operand-stack height and control-stack depth.
 */
wdec_stack_depth wdec_validator_max_depth(const wdec_validator_t *validator);

/**
 * wdec_validate_func() - Type-check one defined function's body.
 * @mod: a decoded module.
 * @funcidx: index of a *defined* function, in the combined (imports
 *     first) index space.
 * @validator: a validator context initialized by wdec_validator_init(); reset
 *     at the start of this call.
 *
 * Runs the stack-based validation algorithm of WebAssembly 1.0 spec
 * section 7.3 over the function body.
 *
 * Finding the function is O(@funcidx), as with wdec_get_func(): validate
 * a whole module with wdec_validate(), not one call per function.
 *
 * Return: %WASMC_OK if the function is well-typed,
 * %WASMC_ERR_FUNC_IS_IMPORTED if @funcidx names an imported function, or
 * another %WASMC_ERR_* code (from decoding its body, or from the
 * stack-machine checks) otherwise.
 */
wasmc_err_t wdec_validate_func(const wdec_module_t *mod, uint32_t funcidx,
                               wdec_validator_t *validator);

/**
 * wdec_validate() - Fully validate a module against the WebAssembly 1.0
 * specification.
 * @mod: a decoded module.
 * @validator: a validator context initialized by wdec_validator_init(), reused
 *     across every function body validated.
 * @out_bad_funcidx: if non-NULL and a function body fails validation,
 *                    set to that function's index.
 *
 * Structural and semantic checks of every section (index bounds,
 * limits, uniqueness of export names, constant-expression
 * well-formedness, ...) plus a wdec_validate_func() call for every
 * defined function.
 *
 * Return: %WASMC_OK if the module is fully spec-conformant, or the first
 * %WASMC_ERR_* encountered otherwise.
 */
wasmc_err_t wdec_validate(const wdec_module_t *mod, wdec_validator_t *validator,
                          uint32_t *out_bad_funcidx);

#endif /* WDEC_H_INCLUDED */
