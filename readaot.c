/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * readaot - display and validate the contents of a WAMR AOT file.
 *
 * The decoder follows the WAMR-2.4.4 loader (core/iwasm/aot/aot_loader.c)
 * and writer (core/iwasm/compilation/aot_emit_aot_file.c) field by field;
 * see aotfmt/aotfmt.tex for the format description. It is self-contained
 * (no WAMR headers) and never trusts the input: every read is bounds checked
 * and every count is checked against the bytes left before it is used.
 *
 * Alignment rule (same as the WAMR loader): every u16 is read at a 2-byte
 * aligned file offset, every u32 and u64 at a 4-byte aligned file offset;
 * the reader skips the padding bytes in front of a field implicitly.
 *
 * The file is decoded twice: the first pass is silent, validates everything
 * and collects the cross-section information (function count, types, text
 * offsets, symbols, ...); the second pass prints the selected sections with
 * all of that information available.
 */
#include <ctype.h>
#include <elf.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* ------------------------------------------------------------------------ */
/* Format constants (WAMR-2.4.4)                                            */
/* ------------------------------------------------------------------------ */

#define AOT_MAGIC           "\0aot"
#define AOT_MAGIC_SIZE      (sizeof AOT_MAGIC - 1)
#define AOT_HEADER_SIZE     (AOT_MAGIC_SIZE + sizeof(uint32_t)) /* + version */
#define AOT_CURRENT_VERSION 5

enum {
    SEC_TARGET_INFO = 0,
    SEC_INIT_DATA = 1,
    SEC_TEXT = 2,
    SEC_FUNCTION = 3,
    SEC_EXPORT = 4,
    SEC_RELOCATION = 5,
    SEC_SIGNATURE = 6, /* reserved, rejected by the loader */
    SEC_CUSTOM = 100,
};

/* sections 0 (target info) to 5 (relocation) must all be present */
#define REQUIRED_SECTIONS SEC_SIGNATURE

enum {
    CUSTOM_RAW = 0,
    CUSTOM_NATIVE_SYMBOL = 1,
    CUSTOM_ACCESS_CONTROL = 2,
    CUSTOM_NAME = 3,
    CUSTOM_STRING_LITERAL = 4,
};

/* target info */
#define BIN_TYPE_ELF32L 0
#define BIN_TYPE_ELF32B 1
#define BIN_TYPE_ELF64L 2
#define BIN_TYPE_ELF64B 3
#define BIN_TYPE_COFF32 4
#define BIN_TYPE_COFF64 6
#define BIN_TYPE_64BIT  0x2 /* set in the 64-bit bin types */

/* target info section: field offsets and the arch string */
#define TI_E_TYPE_OFF    4
#define TI_E_MACHINE_OFF 6
#define TI_E_VERSION_OFF 8
#define TI_RESERVED_OFF  24
#define TI_ARCH_OFF      32
#define TI_ARCH_LEN      16

#define E_TYPE_REL 1
#define E_TYPE_XIP 4

/* e_machine values not in <elf.h> */
#define EM_ARC_COMPACT2 195
#define EM_WIN_I386     0x14c
#define EM_WIN_X86_64   0x8664

/* feature flags */
#define FEATURE_SIMD_128BIT        (1u << 0)
#define FEATURE_BULK_MEMORY        (1u << 1)
#define FEATURE_MULTI_THREAD       (1u << 2)
#define FEATURE_REF_TYPES          (1u << 3)
#define FEATURE_GC                 (1u << 4)
#define FEATURE_EXCEPTION_HANDLING (1u << 5)
#define FEATURE_TINY_STACK_FRAME   (1u << 6)
#define FEATURE_MULTI_MEMORY       (1u << 7)
#define FEATURE_DYNAMIC_LINKING    (1u << 8)
#define FEATURE_COMPONENT_MODEL    (1u << 9)
#define FEATURE_RELAXED_SIMD       (1u << 10)
#define FEATURE_FLEXIBLE_VECTORS   (1u << 11)
#define FEATURE_FRAME_PER_FUNCTION (1u << 12)
#define FEATURE_FRAME_NO_FUNC_IDX  (1u << 13)

/* init expression types */
#define INIT_EXPR_NONE               0x00
#define INIT_EXPR_I32_CONST          0x41
#define INIT_EXPR_I64_CONST          0x42
#define INIT_EXPR_F32_CONST          0x43
#define INIT_EXPR_F64_CONST          0x44
#define INIT_EXPR_V128_CONST         0xFD
#define INIT_EXPR_GET_GLOBAL         0x23
#define INIT_EXPR_I32_ADD            0x6A
#define INIT_EXPR_I32_SUB            0x6B
#define INIT_EXPR_I32_MUL            0x6C
#define INIT_EXPR_I64_ADD            0x7C
#define INIT_EXPR_I64_SUB            0x7D
#define INIT_EXPR_I64_MUL            0x7E
#define INIT_EXPR_REFNULL_CONST      0xD0
#define INIT_EXPR_FUNCREF_CONST      0xD2
#define INIT_EXPR_STRUCT_NEW         0xD3
#define INIT_EXPR_STRUCT_NEW_DEFAULT 0xD4
#define INIT_EXPR_ARRAY_NEW          0xD5
#define INIT_EXPR_ARRAY_NEW_DEFAULT  0xD6
#define INIT_EXPR_ARRAY_NEW_FIXED    0xD7
#define INIT_EXPR_I31_NEW            0xD8

/* GC type kinds (type_flag) */
#define WASM_TYPE_FUNC   0
#define WASM_TYPE_STRUCT 1
#define WASM_TYPE_ARRAY  2

/* value types needing a heap type (GC "multi byte" types) */
#define REF_TYPE_HT_NON_NULLABLE 0x64
#define REF_TYPE_HT_NULLABLE     0x63

/* value types (WASM binary codes) */
#define VALTYPE_I32       0x7F
#define VALTYPE_I64       0x7E
#define VALTYPE_F32       0x7D
#define VALTYPE_F64       0x7C
#define VALTYPE_V128      0x7B
#define VALTYPE_FUNCREF   0x70
#define VALTYPE_EXTERNREF 0x6F
#define V128_SIZE         16

/* memory and table flags */
#define MEM_FLAG_HAS_MAX   0x1
#define MEM_FLAG_SHARED    0x2
#define MEM_FLAG_MEMORY64  0x4
#define TABLE_FLAG_HAS_MAX 0x1
#define TABLE_FLAG_TABLE64 0x4

/* elem segment modes */
#define ELEM_MODE_PASSIVE     0x1
#define ELEM_MODE_DECLARATIVE 0x3

/* smallest encoded size of each kind of entry, in bytes: rd_count() rejects
 * counts that cannot fit in the rest of the section */
#define MIN_SIZE_TYPE          6
#define MIN_SIZE_IMPORT_GLOBAL 6
#define MIN_SIZE_GLOBAL        8
#define MIN_SIZE_IMPORT_FUNC   6
#define MIN_SIZE_MEMORY        16
#define MIN_SIZE_DATA_SEGMENT  16
#define MIN_SIZE_TABLE         12
#define MIN_SIZE_ELEM_SEGMENT  28
#define MIN_SIZE_OBJECT_DATA   6
#define MIN_SIZE_EXPORT        8
#define MIN_SIZE_RELOC_GROUP   8
#define MIN_SIZE_RELOC32       16
#define MIN_SIZE_RELOC64       24
#define MIN_SIZE_FUNC_NAME     6

/* deepest nesting of extended constant expressions */
#define MAX_INIT_EXPR_DEPTH 64

/* longest data and string literal prefix printed */
#define QUOTE_DATA_MAX   48
#define QUOTE_STRING_MAX 64

/* name of the symbol of AOT function N: "aot_func#N" */
#define AOT_FUNC_PREFIX "aot_func#"

/* room for a relocation type shown as "<name>(<number>)" */
#define RELOC_LABEL_LEN 48

#define MAX_LINEAR_MEMORY_SIZE (4ull << 30)

/* ------------------------------------------------------------------------ */
/* Diagnostics                                                              */
/* ------------------------------------------------------------------------ */

static const char *g_path;
static unsigned g_errors, g_warnings;
static bool g_report = true; /* false during the printing pass */

static void diag(const char *kind, size_t off, const char *fmt, va_list args)
{
    fprintf(stderr, "%s: %s: offset 0x%zx: ", g_path, kind, off);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
}

static void error_at(size_t off, const char *fmt, ...)
{
    va_list args;
    if (!g_report) {
        return;
    }
    g_errors++;
    va_start(args, fmt);
    diag("error", off, fmt, args);
    va_end(args);
}

static void warn_at(size_t off, const char *fmt, ...)
{
    va_list args;
    if (!g_report) {
        return;
    }
    g_warnings++;
    va_start(args, fmt);
    diag("warning", off, fmt, args);
    va_end(args);
}

static void *xcalloc(size_t n, size_t size)
{
    void *ptr = calloc(n ? n : 1, size ? size : 1);
    if (!ptr) {
        fprintf(stderr, "readaot: out of memory\n");
        exit(2);
    }
    return ptr;
}

/* ------------------------------------------------------------------------ */
/* Bounds-checked reader. Offsets are absolute file offsets, so alignment   */
/* is relative to the start of the file, as in WAMR (which loads the file   */
/* at a 4-byte aligned address).                                            */
/* ------------------------------------------------------------------------ */

typedef struct {
    const uint8_t *base; /* whole file */
    size_t off;          /* current position */
    size_t end;          /* end of the current region */
    bool failed;         /* sticky: a read ran out of bounds */
} rd_t;

static rd_t rd_sub(const uint8_t *base, size_t start, size_t end)
{
    rd_t reader = {base, start, end, false};
    return reader;
}

static size_t rd_left(const rd_t *reader)
{
    return reader->off <= reader->end ? reader->end - reader->off : 0;
}

static bool rd_need(rd_t *reader, size_t n, const char *what)
{
    if (reader->failed) {
        return false;
    }
    if (reader->off > reader->end || reader->end - reader->off < n) {
        error_at(reader->off,
                 "unexpected end of section reading %s (%zu byte(s) needed, "
                 "%zu left)",
                 what, n, rd_left(reader));
        reader->failed = true;
        return false;
    }
    return true;
}

static void rd_align(rd_t *reader, size_t idx)
{
    size_t aligned = (reader->off + idx - 1) & ~(idx - 1);
    if (reader->failed) {
        return;
    }
    if (aligned > reader->end) {
        error_at(reader->off, "unexpected end of section while aligning to %zu",
                 idx);
        reader->failed = true;
        return;
    }
    reader->off = aligned;
}

static uint8_t rd_u8(rd_t *reader, const char *what)
{
    if (!rd_need(reader, 1, what)) {
        return 0;
    }
    return reader->base[reader->off++];
}

/* the little-endian unsigned integer in the `size` bytes at ptr */
static uint64_t load_le(const uint8_t *ptr, size_t size)
{
    uint64_t val = 0;
    while (size--) {
        val = val << CHAR_BIT | ptr[size];
    }
    return val;
}

static uint32_t load_le32(const uint8_t *ptr)
{
    return (uint32_t)load_le(ptr, sizeof(uint32_t));
}

/* an unsigned integer of `size` bytes, aligned to its size */
static uint64_t rd_uint(rd_t *reader, size_t size, const char *what)
{
    const uint8_t *ptr;
    rd_align(reader, size);
    if (!rd_need(reader, size, what)) {
        return 0;
    }
    ptr = reader->base + reader->off;
    reader->off += size;
    return load_le(ptr, size);
}

static uint16_t rd_u16(rd_t *reader, const char *what)
{
    return (uint16_t)rd_uint(reader, sizeof(uint16_t), what);
}

static uint32_t rd_u32(rd_t *reader, const char *what)
{
    return (uint32_t)rd_uint(reader, sizeof(uint32_t), what);
}

/* u64 values are only 4-byte aligned in the AOT format */
static uint64_t rd_u64(rd_t *reader, const char *what)
{
    uint64_t low = rd_u32(reader, what);
    uint64_t high = rd_u32(reader, what);
    return low | high << (CHAR_BIT * sizeof(uint32_t));
}

static bool starts_with(const char *str, const char *prefix)
{
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

/* function index N of an "aot_func#N" symbol */
static uint32_t aot_func_index(const char *sym)
{
    enum { DECIMAL = 10 };
    return (uint32_t)strtoul(sym + strlen(AOT_FUNC_PREFIX), NULL, DECIMAL);
}

/* offset of the field `n` u32 slots before the current position */
static size_t back_u32(const rd_t *reader, size_t n)
{
    return reader->off - (n * sizeof(uint32_t));
}

static const uint8_t *rd_bytes(rd_t *reader, size_t n, const char *what)
{
    const uint8_t *ptr;
    if (!rd_need(reader, n, what)) {
        return NULL;
    }
    ptr = reader->base + reader->off;
    reader->off += n;
    return ptr;
}

/* Count read from the file, checked against a minimum element size so that
 * a corrupt count can neither loop for ages nor overflow an allocation. */
static uint32_t rd_count(rd_t *reader, size_t min_elem_size, const char *what)
{
    uint32_t count = rd_u32(reader, what);
    if (!reader->failed && min_elem_size &&
        (uint64_t)count * min_elem_size > rd_left(reader)) {
        error_at(back_u32(reader, 1),
                 "%s %" PRIu32 " cannot fit in the %zu byte(s) left", what,
                 count, rd_left(reader));
        reader->failed = true;
        return 0;
    }
    return count;
}

/* AOT string: u16 length (2-byte aligned) including the final '\0', then the
 * bytes. Returns a pointer into the file buffer (always NUL terminated when
 * valid), or "" on error. */
static const char *rd_str(rd_t *reader, const char *what)
{
    size_t start;
    uint16_t len;
    const uint8_t *ptr;

    len = rd_u16(reader, what);
    start = reader->off;
    ptr = rd_bytes(reader, len, what);
    if (!ptr) {
        return "";
    }
    if (len == 0) {
        return ""; /* accepted by the loader */
    }
    if (ptr[len - 1] != '\0') {
        error_at(start, "%s is not NUL terminated", what);
        reader->failed = true;
        return "";
    }
    if (memchr(ptr, '\0', len) != ptr + len - 1) {
        warn_at(start, "%s contains an embedded NUL byte", what);
    }
    return (const char *)ptr;
}

/* ------------------------------------------------------------------------ */
/* Module information gathered by the first pass                            */
/* ------------------------------------------------------------------------ */

typedef struct {
    uint32_t id;
    size_t hdr; /* offset of the section id */
    size_t start, end;
} section_t;

typedef struct {
    uint16_t kind; /* WASM_TYPE_* */
    uint16_t param_count, result_count;
    const uint8_t *types;  /* param types then result types (into the file) */
    uint16_t field_count;  /* struct */
    const uint8_t *fields; /* struct: (flags, type) pairs */
    uint8_t elem_type;     /* array */
    int32_t same_as;       /* GC equivalence type, -1 if none */
} type_t;

typedef struct {
    uint16_t bin_type, abi_type, e_type, e_machine;
    uint32_t e_version, e_flags;
    uint64_t feature_flags, reserved;
    char arch[TI_ARCH_LEN + 1];
} target_t;

typedef struct {
    const uint8_t *buf;
    size_t size;
    uint32_t version;

    section_t *secs;
    uint32_t nsecs;
    int idx[REQUIRED_SECTIONS]; /* index in secs[] of each required section,
                                   -1 if missing */

    bool have_target;
    target_t ti;
    bool is64, gc, xip;
    unsigned ptr_size;

    uint32_t type_count;
    type_t *types;
    uint32_t import_global_count, global_count;
    uint32_t import_func_count, func_count;
    uint32_t import_table_count, table_count;
    uint32_t memory_count;
    uint32_t start_func_index;
    uint32_t data_section_count;

    /* text */
    bool have_text;
    uint32_t literal_size;
    size_t code_off, code_size; /* code_off: file offset of code[0] */

    /* function section */
    uint64_t *func_offsets; /* [func_count] */
    uint32_t *func_types;
    bool have_funcs;

    /* exports, names: function index -> name for nicer output */
    const char **func_names; /* [import_func_count + func_count] */

    /* relocation symbol table */
    uint32_t symbol_count;
    const char **symbols;
} aot_t;

static aot_t aot;

/* which sections to print */
static struct {
    bool sections, target, init, text, func, exports, reloc, custom;
    bool hexdump, disasm;
    const char *elf_out;
} opt;

static bool g_print; /* true during the printing pass */

/* printf() for a section being shown, during the printing pass */
static void print_if(bool show, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void print_if(bool show, const char *fmt, ...)
{
    va_list args;
    if (!g_print || !show) {
        return;
    }
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

/* ------------------------------------------------------------------------ */
/* Names                                                                    */
/* ------------------------------------------------------------------------ */

static const char *section_name(uint32_t ident)
{
    switch (ident) {
    case SEC_TARGET_INFO:
        return "target info";
    case SEC_INIT_DATA:
        return "init data";
    case SEC_TEXT:
        return "text";
    case SEC_FUNCTION:
        return "function";
    case SEC_EXPORT:
        return "export";
    case SEC_RELOCATION:
        return "relocation";
    case SEC_SIGNATURE:
        return "signature (reserved)";
    case SEC_CUSTOM:
        return "custom";
    default:
        return "unknown";
    }
}

static const char *bin_type_name(uint16_t type)
{
    switch (type) {
    case BIN_TYPE_ELF32L:
        return "ELF32L (ELF, 32-bit, little endian)";
    case BIN_TYPE_ELF32B:
        return "ELF32B (ELF, 32-bit, big endian)";
    case BIN_TYPE_ELF64L:
        return "ELF64L (ELF, 64-bit, little endian)";
    case BIN_TYPE_ELF64B:
        return "ELF64B (ELF, 64-bit, big endian)";
    case BIN_TYPE_COFF32:
        return "COFF32 (COFF, 32-bit, little endian)";
    case BIN_TYPE_COFF64:
        return "COFF64 (COFF, 64-bit, little endian)";
    default:
        return "invalid";
    }
}

static const char *e_type_name(uint16_t type)
{
    switch (type) {
    case 0:
        return "NONE";
    case E_TYPE_REL:
        return "REL (relocatable)";
    case 2:
        return "EXEC";
    case 3:
        return "DYN";
    case E_TYPE_XIP:
        return "XIP (execute in place)";
    default:
        return "invalid";
    }
}

static const char *e_machine_name(uint16_t mask)
{
    switch (mask) {
    case EM_386:
        return "386";
    case EM_MIPS:
        return "MIPS";
    case EM_ARM:
        return "ARM";
    case EM_X86_64:
        return "X86_64";
    case EM_ARC_COMPACT:
        return "ARC_COMPACT";
    case EM_XTENSA:
        return "XTENSA";
    case EM_AARCH64:
        return "AARCH64";
    case EM_ARC_COMPACT2:
        return "ARC_COMPACT2";
    case EM_RISCV:
        return "RISCV";
    case EM_WIN_I386:
        return "WIN_I386 (COFF i386)";
    case EM_WIN_X86_64:
        return "WIN_X86_64 (COFF x86-64)";
    default:
        return "unknown";
    }
}

/* The arch prefix the WAMR loader requires for each e_machine (NULL: the arch
 * string is compared with the runtime build target directly). */
static const char *e_machine_arch(uint16_t mask)
{
    switch (mask) {
    case EM_X86_64:
    case EM_WIN_X86_64:
        return "x86_64";
    case EM_386:
    case EM_WIN_I386:
        return "i386";
    case EM_MIPS:
        return "mips";
    case EM_XTENSA:
        return "xtensa";
    case EM_RISCV:
        return "riscv";
    case EM_ARC_COMPACT:
    case EM_ARC_COMPACT2:
        return "arc";
    default:
        return NULL;
    }
}

/* name and size of each value type; size 0: a reference (pointer size) */
static const struct {
    const char *name;
    uint8_t code;
    uint8_t size;
} valtypes[] = {
    {"i32", VALTYPE_I32, 4},
    {"i64", VALTYPE_I64, 8},
    {"f32", VALTYPE_F32, 4},
    {"f64", VALTYPE_F64, 8},
    {"v128", VALTYPE_V128, V128_SIZE},
    {"i8", 0x78, 1}, /* packed types of GC struct fields and arrays */
    {"i16", 0x77, 2},
    {"nullfuncref", 0x73, 0},
    {"nullexternref", 0x72, 0},
    {"nullref", 0x71, 0},
    {"funcref", VALTYPE_FUNCREF, 0},
    {"externref", VALTYPE_EXTERNREF, 0},
    {"anyref", 0x6E, 0},
    {"eqref", 0x6D, 0},
    {"i31ref", 0x6C, 0},
    {"structref", 0x6B, 0},
    {"arrayref", 0x6A, 0},
    {"stringref", 0x67, 0},
    {"stringview_wtf8", 0x66, 0},
    {"(ref ht)", REF_TYPE_HT_NON_NULLABLE, 0},
    {"(ref null ht)", REF_TYPE_HT_NULLABLE, 0},
    {"stringview_wtf16", 0x62, 0},
    {"stringview_iter", 0x61, 0},
    {"void", 0x40, 0},
};

#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

/* index of the type in valtypes[], or -1 */
static int valtype_index(uint8_t type)
{
    for (size_t i = 0; i < ARRAY_LEN(valtypes); i++) {
        if (valtypes[i].code == type) {
            return (int)i;
        }
    }
    return -1;
}

static const char *valtype_name(uint8_t type)
{
    int idx = valtype_index(type);
    return idx < 0 ? NULL : valtypes[idx].name;
}

static const char *vt(uint8_t type)
{
    /* a few buffers, so that one printf() can show several unknown types */
    enum { RING = 4, LEN = 16 };
    static char buf[RING][LEN];
    static int idx;
    const char *name = valtype_name(type);
    if (name) {
        return name;
    }
    idx = (idx + 1) % RING;
    snprintf(buf[idx], sizeof buf[idx], "0x%02x?", type);
    return buf[idx];
}

static bool is_valid_valtype(uint8_t type)
{
    /* is_valid_value_type() in the WAMR loader, without GC */
    return type == VALTYPE_I32 || type == VALTYPE_I64 || type == VALTYPE_F32 ||
           type == VALTYPE_F64 || type == VALTYPE_V128 ||
           type == VALTYPE_FUNCREF || type == VALTYPE_EXTERNREF;
}

static bool is_multi_byte_type(uint8_t type)
{
    return type == REF_TYPE_HT_NULLABLE || type == REF_TYPE_HT_NON_NULLABLE;
}

static const char *export_kind_name(uint8_t kind)
{
    switch (kind) {
    case 0:
        return "func";
    case 1:
        return "table";
    case 2:
        return "memory";
    case 3:
        return "global";
    case 4:
        return "tag";
    default:
        return "invalid";
    }
}

/* size of a struct field / array element in a GC init expression */
static unsigned gc_value_size(uint8_t type)
{
    int idx = valtype_index(type);
    if (idx < 0 || valtypes[idx].size == 0) {
        return aot.ptr_size; /* reference types */
    }
    return valtypes[idx].size;
}

/* ------------------------------------------------------------------------ */
/* Relocation type names (the ELF/COFF numbers of the target)               */
/* ------------------------------------------------------------------------ */

typedef struct {
    uint32_t type;
    const char *name;
} reloc_name_t;

static const reloc_name_t reloc_x86_64[] = {
    {0, "R_X86_64_NONE"},
    {1, "R_X86_64_64"},
    {2, "R_X86_64_PC32"},
    {3, "R_X86_64_GOT32"},
    {4, "R_X86_64_PLT32"},
    {9, "R_X86_64_GOTPCREL"},
    {10, "R_X86_64_32"},
    {11, "R_X86_64_32S"},
    {24, "R_X86_64_PC64"},
    {41, "R_X86_64_GOTPCRELX"},
    {42, "R_X86_64_REX_GOTPCRELX"},
    {0, NULL},
};
static const reloc_name_t reloc_coff_amd64[] = {
    {1, "IMAGE_REL_AMD64_ADDR64"},
    {2, "IMAGE_REL_AMD64_ADDR32"},
    {3, "IMAGE_REL_AMD64_ADDR32NB"},
    {4, "IMAGE_REL_AMD64_REL32"},
    {0, NULL},
};
static const reloc_name_t reloc_386[] = {
    {1, "R_386_32"},    {2, "R_386_PC32"},       {3, "R_386_GOT32"},
    {4, "R_386_PLT32"}, {24, "R_386_TLS_GD_32"}, {0, NULL},
};
static const reloc_name_t reloc_coff_i386[] = {
    {6, "IMAGE_REL_I386_DIR32"},
    {7, "IMAGE_REL_I386_DIR32NB"},
    {20, "IMAGE_REL_I386_REL32"},
    {0, NULL},
};
static const reloc_name_t reloc_arm[] = {
    {2, "R_ARM_ABS32"},          {10, "R_ARM_THM_CALL"},
    {28, "R_ARM_CALL"},          {29, "R_ARM_JUMP24"},
    {30, "R_ARM_THM_JUMP24"},    {43, "R_ARM_MOVW_ABS_NC"},
    {44, "R_ARM_MOVT_ABS"},      {47, "R_ARM_THM_MOVW_ABS_NC"},
    {48, "R_ARM_THM_MOVT_ABS"},  {49, "R_ARM_THM_MOVW_PREL_NC"},
    {50, "R_ARM_THM_MOVT_PREL"}, {0, NULL},
};
static const reloc_name_t reloc_aarch64[] = {
    {257, "R_AARCH64_ABS64"},
    {258, "R_AARCH64_ABS32"},
    {263, "R_AARCH64_MOVW_UABS_G0"},
    {264, "R_AARCH64_MOVW_UABS_G0_NC"},
    {265, "R_AARCH64_MOVW_UABS_G1"},
    {266, "R_AARCH64_MOVW_UABS_G1_NC"},
    {267, "R_AARCH64_MOVW_UABS_G2"},
    {268, "R_AARCH64_MOVW_UABS_G2_NC"},
    {269, "R_AARCH64_MOVW_UABS_G3"},
    {270, "R_AARCH64_MOVW_SABS_G0"},
    {271, "R_AARCH64_MOVW_SABS_G1"},
    {272, "R_AARCH64_MOVW_SABS_G2"},
    {273, "R_AARCH64_LD_PREL_LO19"},
    {274, "R_AARCH64_ADR_PREL_LO21"},
    {275, "R_AARCH64_ADR_PREL_PG_HI21"},
    {276, "R_AARCH64_ADR_PREL_PG_HI21_NC"},
    {277, "R_AARCH64_ADD_ABS_LO12_NC"},
    {278, "R_AARCH64_LDST8_ABS_LO12_NC"},
    {282, "R_AARCH64_JUMP26"},
    {283, "R_AARCH64_CALL26"},
    {284, "R_AARCH64_LDST16_ABS_LO12_NC"},
    {285, "R_AARCH64_LDST32_ABS_LO12_NC"},
    {286, "R_AARCH64_LDST64_ABS_LO12_NC"},
    {299, "R_AARCH64_LDST128_ABS_LO12_NC"},
    {0, NULL},
};
static const reloc_name_t reloc_mips[] = {
    {2, "R_MIPS_32"},   {4, "R_MIPS_26"}, {5, "R_MIPS_HI16"},
    {6, "R_MIPS_LO16"}, {0, NULL},
};
static const reloc_name_t reloc_xtensa[] = {
    {1, "R_XTENSA_32"},
    {20, "R_XTENSA_SLOT0_OP"},
    {0, NULL},
};
static const reloc_name_t reloc_riscv[] = {
    {1, "R_RISCV_32"},
    {2, "R_RISCV_64"},
    {16, "R_RISCV_BRANCH"},
    {17, "R_RISCV_JAL"},
    {18, "R_RISCV_CALL"},
    {19, "R_RISCV_CALL_PLT"},
    {20, "R_RISCV_GOT_HI20"},
    {23, "R_RISCV_PCREL_HI20"},
    {24, "R_RISCV_PCREL_LO12_I"},
    {25, "R_RISCV_PCREL_LO12_S"},
    {26, "R_RISCV_HI20"},
    {27, "R_RISCV_LO12_I"},
    {28, "R_RISCV_LO12_S"},
    {43, "R_RISCV_ALIGN"},
    {44, "R_RISCV_RVC_BRANCH"},
    {45, "R_RISCV_RVC_JUMP"},
    {51, "R_RISCV_RELAX"},
    {0, NULL},
};
static const reloc_name_t reloc_arc[] = {
    {4, "R_ARC_32"},
    {14, "R_ARC_S21H_PCREL"},
    {15, "R_ARC_S21W_PCREL"},
    {16, "R_ARC_S25H_PCREL"},
    {17, "R_ARC_S25W_PCREL"},
    {27, "R_ARC_32_ME"},
    {0, NULL},
};

/* relocation types the WAMR-2.4.4 runtime can apply, per architecture */
static const uint32_t supp_x86_64[] = {1, 2, 4, 9, 10, 11, 24, UINT32_MAX};
static const uint32_t supp_coff_amd64[] = {1, 2, 4, UINT32_MAX};
static const uint32_t supp_386[] = {1, 2, 4, 24, UINT32_MAX};
static const uint32_t supp_coff_i386[] = {6, 20, UINT32_MAX};
static const uint32_t supp_arm[] = {2, 28, 29, 43, 44, UINT32_MAX};
static const uint32_t supp_thumb[] = {2, 10, 30, 47, 48, 49, 50, UINT32_MAX};
static const uint32_t supp_aarch64[] = {263, 264, 265, 266, 267, 268,
                                        269, 275, 276, 277, 278, 282,
                                        283, 284, 285, 286, 299, UINT32_MAX};
static const uint32_t supp_mips[] = {2, 4, UINT32_MAX};
static const uint32_t supp_xtensa[] = {1, 20, UINT32_MAX};
static const uint32_t supp_riscv32[] = {1,  18, 19, 23, 24,
                                        25, 26, 27, 28, UINT32_MAX};
static const uint32_t supp_riscv64[] = {1,  2,  18, 19, 23,        24,
                                        25, 26, 27, 28, UINT32_MAX};
static const uint32_t supp_arc[] = {4, 16, 17, 27, UINT32_MAX};

static bool arch_is_thumb(void)
{
    return starts_with(aot.ti.arch, "thumb");
}

static void reloc_tables(const reloc_name_t **names, const uint32_t **supp)
{
    *names = NULL;
    *supp = NULL;
    switch (aot.ti.e_machine) {
    case EM_X86_64:
        *names = reloc_x86_64;
        *supp = supp_x86_64;
        break;
    case EM_WIN_X86_64:
        *names = reloc_coff_amd64;
        *supp = supp_coff_amd64;
        break;
    case EM_386:
        *names = reloc_386;
        *supp = supp_386;
        break;
    case EM_WIN_I386:
        *names = reloc_coff_i386;
        *supp = supp_coff_i386;
        break;
    case EM_ARM:
        *names = reloc_arm;
        *supp = arch_is_thumb() ? supp_thumb : supp_arm;
        break;
    case EM_AARCH64:
        *names = reloc_aarch64;
        *supp = supp_aarch64;
        break;
    case EM_MIPS:
        *names = reloc_mips;
        *supp = supp_mips;
        break;
    case EM_XTENSA:
        *names = reloc_xtensa;
        *supp = supp_xtensa;
        break;
    case EM_RISCV:
        *names = reloc_riscv;
        *supp = aot.is64 ? supp_riscv64 : supp_riscv32;
        break;
    case EM_ARC_COMPACT:
    case EM_ARC_COMPACT2:
        *names = reloc_arc;
        *supp = supp_arc;
        break;
    default:
        break;
    }
}

static const char *reloc_type_name(uint32_t type, bool *supported)
{
    const reloc_name_t *names;
    const uint32_t *supp;
    const char *name = "?";

    reloc_tables(&names, &supp);
    for (; names && names->name; names++) {
        if (names->type == type) {
            name = names->name;
            break;
        }
    }
    *supported = supp == NULL; /* unknown machine: don't complain */
    for (; supp && *supp != UINT32_MAX; supp++) {
        if (*supp == type) {
            *supported = true;
        }
    }
    return name;
}

/* ------------------------------------------------------------------------ */
/* Hex dump                                                                 */
/* ------------------------------------------------------------------------ */

enum { HEXDUMP_ROW = 16 }; /* bytes per line */

static bool is_printable(uint8_t byte)
{
    return isprint(byte) != 0;
}

static void hexdump(size_t start, size_t end)
{
    size_t row;
    for (row = start & ~(size_t)(HEXDUMP_ROW - 1); row < end;
         row += HEXDUMP_ROW) {
        printf("  %08zx ", row);
        for (size_t i = row; i < row + HEXDUMP_ROW; i++) {
            if (i == row + (HEXDUMP_ROW / 2)) {
                putchar(' ');
            }
            if (i < start || i >= end) {
                printf("   ");
            } else {
                printf(" %02x", aot.buf[i]);
            }
        }
        printf("  |");
        for (size_t i = row; i < row + HEXDUMP_ROW; i++) {
            uint8_t byte = (i < start || i >= end) ? ' ' : aot.buf[i];
            putchar(is_printable(byte) ? byte : '.');
        }
        printf("|\n");
    }
}

static void print_quoted(const uint8_t *ptr, size_t n, size_t max)
{
    putchar('"');
    for (size_t i = 0; i < n && i < max; i++) {
        uint8_t byte = ptr[i];
        if (byte == '"' || byte == '\\') {
            printf("\\%c", byte);
        } else if (is_printable(byte)) {
            putchar(byte);
        } else {
            printf("\\x%02x", byte);
        }
    }
    putchar('"');
    if (n > max) {
        printf("... (%zu bytes)", n);
    }
}

/* ------------------------------------------------------------------------ */
/* Target info section                                                      */
/* ------------------------------------------------------------------------ */

static bool is_valid_bin_type(uint16_t bin_type)
{
    return bin_type == BIN_TYPE_ELF32L || bin_type == BIN_TYPE_ELF32B ||
           bin_type == BIN_TYPE_ELF64L || bin_type == BIN_TYPE_ELF64B ||
           bin_type == BIN_TYPE_COFF32 || bin_type == BIN_TYPE_COFF64;
}

/* the checks of the loader on the target info (at `start` in the file) */
static void check_target_info(const target_t *target, size_t start)
{
    if (target->arch[TI_ARCH_LEN - 1] != '\0') {
        error_at(start + TI_ARCH_OFF,
                 "arch string is not NUL terminated within 16 bytes");
    }
    if (!is_valid_bin_type(target->bin_type)) {
        error_at(start, "invalid bin_type %u", target->bin_type);
    }
    /* note: the container is little endian even when bin_type says big endian
     */
    if (target->e_type != E_TYPE_REL && target->e_type != E_TYPE_XIP) {
        error_at(start + TI_E_TYPE_OFF,
                 "e_type %u is neither REL (1) nor XIP (4)", target->e_type);
    }
    if (target->e_version != 1) {
        error_at(start + TI_E_VERSION_OFF, "e_version %" PRIu32 " is not 1",
                 target->e_version);
    }
    const char *want = e_machine_arch(target->e_machine);
    if (want && strncmp(target->arch, want, strlen(want)) != 0) {
        error_at(
            start + TI_ARCH_OFF,
            "arch \"%s\" does not start with \"%s\" as e_machine %u requires",
            target->arch, want, target->e_machine);
    }
    if (!want && target->e_machine != EM_ARM &&
        target->e_machine != EM_AARCH64) {
        error_at(start + TI_E_MACHINE_OFF, "unknown e_machine %u",
                 target->e_machine);
    }
    if (target->reserved) {
        warn_at(start + TI_RESERVED_OFF, "reserved field is not zero");
    }
}

static void print_riscv_flags(uint32_t e_flags)
{
    static const char *fabi[] = {"soft", "single", "double", "quad"};
    unsigned float_abi = (e_flags & EF_RISCV_FLOAT_ABI) >> 1;
    printf("  (%sfloat-abi=%s%s%s)", e_flags & EF_RISCV_RVC ? "RVC, " : "",
           fabi[float_abi], e_flags & EF_RISCV_RVE ? ", RVE" : "",
           e_flags & EF_RISCV_TSO ? ", TSO" : "");
}

static void print_feature_flags(uint64_t flags)
{
    static const char *names[] = {
        "SIMD",
        "BULK_MEMORY",
        "MULTI_THREAD",
        "REF_TYPES",
        "GC",
        "EXCEPTION",
        "TINY_STACK_FRAME",
        "MULTI_MEMORY",
        "DYNAMIC_LINK",
        "COMPONENT_MODEL",
        "RELAXED_SIMD",
        "FLEXIBLE_VECTORS",
        "FRAME_PER_FUNCTION",
        "FRAME_NO_FUNC_IDX",
    };
    const char *sep = " (";
    for (unsigned i = 0; i < sizeof flags * CHAR_BIT; i++) {
        if (flags & (1ULL << i)) {
            printf("%s%s", sep, i < ARRAY_LEN(names) ? names[i] : "?");
            sep = " | ";
        }
    }
    printf(")");
}

static void print_target_info(bool show, const target_t *target)
{
    print_if(show, "  bin_type:      %u  %s\n", target->bin_type,
             bin_type_name(target->bin_type));
    print_if(show, "  abi_type:      %u  (unused by WAMR)\n", target->abi_type);
    print_if(show, "  e_type:        %u  %s\n", target->e_type,
             e_type_name(target->e_type));
    print_if(show, "  e_machine:     %u  %s\n", target->e_machine,
             e_machine_name(target->e_machine));
    print_if(show, "  e_version:     %" PRIu32 "\n", target->e_version);
    print_if(show, "  e_flags:       0x%08" PRIx32, target->e_flags);
    if (g_print && show && target->e_machine == EM_RISCV) {
        print_riscv_flags(target->e_flags);
    }
    print_if(show, "\n");
    print_if(show, "  feature_flags: 0x%016" PRIx64, target->feature_flags);
    if (g_print && show && target->feature_flags) {
        print_feature_flags(target->feature_flags);
    }
    print_if(show, "\n");
    print_if(show, "  reserved:      0x%016" PRIx64 "\n", target->reserved);
    print_if(show, "  arch:          \"%s\"\n", target->arch);
}

static void parse_target_info(const section_t *sec, bool show)
{
    rd_t reader = rd_sub(aot.buf, sec->start, sec->end);
    target_t *target = &aot.ti;

    target->bin_type = rd_u16(&reader, "bin_type");
    target->abi_type = rd_u16(&reader, "abi_type");
    target->e_type = rd_u16(&reader, "e_type");
    target->e_machine = rd_u16(&reader, "e_machine");
    target->e_version = rd_u32(&reader, "e_version");
    target->e_flags = rd_u32(&reader, "e_flags");
    target->feature_flags = rd_u64(&reader, "feature_flags");
    target->reserved = rd_u64(&reader, "reserved");
    const uint8_t *arch = rd_bytes(&reader, TI_ARCH_LEN, "arch");
    if (reader.failed) {
        return;
    }
    memcpy(target->arch, arch, TI_ARCH_LEN);
    target->arch[TI_ARCH_LEN] = '\0';

    aot.have_target = true;
    aot.is64 = (target->bin_type & BIN_TYPE_64BIT) != 0;
    aot.ptr_size = aot.is64 ? sizeof(uint64_t) : sizeof(uint32_t);
    aot.gc = (target->feature_flags & FEATURE_GC) != 0;
    aot.xip = target->e_type == E_TYPE_XIP;

    if (reader.off != sec->end) {
        error_at(reader.off, "target info section size is %zu, expected 48",
                 sec->end - sec->start);
    }
    check_target_info(target, sec->start);
    print_target_info(show, target);
}

/* ------------------------------------------------------------------------ */
/* Init data section                                                        */
/* ------------------------------------------------------------------------ */

static void parse_init_expr(rd_t *reader, bool show, int depth);

/* one struct field or array element of `size` bytes */
static void print_gc_value(rd_t *reader, bool show, unsigned size,
                           const char *what)
{
    if (size <= sizeof(uint32_t)) {
        uint32_t value = rd_u32(reader, what);
        print_if(show, " 0x%" PRIx32, value);
    } else if (size == sizeof(uint64_t)) {
        uint64_t value = rd_u64(reader, what);
        print_if(show, " 0x%" PRIx64, value);
    } else {
        rd_bytes(reader, V128_SIZE, what);
        print_if(show, " <v128>");
    }
}

/* a constant of an init expression: false if `type` is not one */
static bool print_const_expr(rd_t *reader, bool show, uint32_t type)
{
    switch (type) {
    case INIT_EXPR_NONE:
        print_if(show, "none");
        return true;
    case INIT_EXPR_I32_CONST: {
        uint32_t val = rd_u32(reader, "i32.const");
        print_if(show, "i32.const %" PRId32 " (0x%" PRIx32 ")", (int32_t)val,
                 val);
        return true;
    }
    case INIT_EXPR_F32_CONST: {
        uint32_t val = rd_u32(reader, "f32.const");
        float fval;
        memcpy(&fval, &val, sizeof fval);
        print_if(show, "f32.const %g (0x%08" PRIx32 ")", (double)fval, val);
        return true;
    }
    case INIT_EXPR_I64_CONST: {
        uint64_t val = rd_u64(reader, "i64.const");
        print_if(show, "i64.const %" PRId64 " (0x%" PRIx64 ")", (int64_t)val,
                 val);
        return true;
    }
    case INIT_EXPR_F64_CONST: {
        uint64_t val = rd_u64(reader, "f64.const");
        double dval;
        memcpy(&dval, &val, sizeof dval);
        print_if(show, "f64.const %g (0x%016" PRIx64 ")", dval, val);
        return true;
    }
    case INIT_EXPR_V128_CONST: {
        const uint8_t *ptr = rd_bytes(reader, V128_SIZE, "v128.const");
        print_if(show, "v128.const 0x");
        if (ptr && g_print && show) {
            for (int i = V128_SIZE - 1; i >= 0; i--) {
                printf("%02x", ptr[i]);
            }
        }
        return true;
    }
    default:
        return false;
    }
}

/* a reference or global init expression: false if `type` is not one */
static bool print_ref_expr(rd_t *reader, bool show, uint32_t type)
{
    switch (type) {
    case INIT_EXPR_GET_GLOBAL:
        print_if(show, "global.get %" PRIu32, rd_u32(reader, "global index"));
        return true;
    case INIT_EXPR_FUNCREF_CONST: {
        uint32_t val = rd_u32(reader, "ref.func index");
        if (val == UINT32_MAX) {
            print_if(show, "ref.func <null>");
        } else {
            print_if(show, "ref.func %" PRIu32, val);
        }
        return true;
    }
    case INIT_EXPR_REFNULL_CONST:
        print_if(show, "ref.null 0x%" PRIx32, rd_u32(reader, "ref.null type"));
        return true;
    case INIT_EXPR_I31_NEW:
        print_if(show, "ref.i31 %" PRIu32, rd_u32(reader, "i31 value"));
        return true;
    case INIT_EXPR_STRUCT_NEW_DEFAULT:
        print_if(show, "struct.new_default type %" PRIu32,
                 rd_u32(reader, "type index"));
        return true;
    default:
        return false;
    }
}

static void parse_struct_new(rd_t *reader, bool show)
{
    uint32_t typeidx = rd_u32(reader, "type index");
    uint32_t count = rd_count(reader, 4, "field count");
    print_if(show, "struct.new type %" PRIu32 " (%" PRIu32 " fields:", typeidx,
             count);
    if (typeidx >= aot.type_count ||
        aot.types[typeidx].kind != WASM_TYPE_STRUCT ||
        aot.types[typeidx].field_count != count) {
        error_at(reader->off,
                 "struct.new refers to an unknown struct type or field count");
        reader->failed = true;
        return;
    }
    for (uint32_t i = 0; i < count && !reader->failed; i++) {
        unsigned size = gc_value_size(aot.types[typeidx].fields[(2 * i) + 1]);
        print_gc_value(reader, show, size, "field");
    }
    print_if(show, ")");
}

static const char *array_new_name(uint32_t type)
{
    switch (type) {
    case INIT_EXPR_ARRAY_NEW:
        return "array.new";
    case INIT_EXPR_ARRAY_NEW_DEFAULT:
        return "array.new_default";
    default:
        return "array.new_fixed";
    }
}

/* array.new, array.new_default or array.new_fixed */
static void parse_array_new(rd_t *reader, bool show, uint32_t type)
{
    uint32_t elem_type = rd_u32(reader, "array element type");
    uint32_t typeidx = rd_u32(reader, "type index");
    uint32_t len = rd_u32(reader, "array length");
    print_if(show, "%s type %" PRIu32 " elem %s len %" PRIu32,
             array_new_name(type), typeidx, vt((uint8_t)elem_type), len);
    if (type == INIT_EXPR_ARRAY_NEW_DEFAULT) {
        return;
    }
    uint32_t count = type == INIT_EXPR_ARRAY_NEW_FIXED ? len : 1;
    unsigned size = gc_value_size((uint8_t)elem_type);
    unsigned min_size = size < sizeof(uint32_t) ? sizeof(uint32_t) : size;
    if ((uint64_t)count * min_size > rd_left(reader)) {
        error_at(reader->off, "array init values do not fit in the section");
        reader->failed = true;
        return;
    }
    print_if(show, " values:");
    for (uint32_t i = 0; i < count && !reader->failed; i++) {
        print_gc_value(reader, show, size, "element");
    }
}

/* name of an extended-const add/sub/mul, NULL if `type` is not one */
static const char *extended_const_name(uint32_t type)
{
    switch (type) {
    case INIT_EXPR_I32_ADD:
        return "i32.add";
    case INIT_EXPR_I32_SUB:
        return "i32.sub";
    case INIT_EXPR_I32_MUL:
        return "i32.mul";
    case INIT_EXPR_I64_ADD:
        return "i64.add";
    case INIT_EXPR_I64_SUB:
        return "i64.sub";
    case INIT_EXPR_I64_MUL:
        return "i64.mul";
    default:
        return NULL;
    }
}

/* an extended-const add/sub/mul of two init expressions */
/* NOLINTNEXTLINE(misc-no-recursion): recursive by design, depth-bounded */
static void parse_extended_const(rd_t *reader, bool show, const char *name,
                                 int depth)
{
    if (depth > MAX_INIT_EXPR_DEPTH) {
        error_at(reader->off, "extended constant expression nested too deeply");
        reader->failed = true;
        return;
    }
    print_if(show, "%s(", name);
    parse_init_expr(reader, show, depth + 1);
    print_if(show, ", ");
    parse_init_expr(reader, show, depth + 1);
    print_if(show, ")");
}

/* InitExpr: (4-aligned) u32 type, then a type-dependent payload */
/* NOLINTNEXTLINE(misc-no-recursion): recursive by design, depth-bounded */
static void parse_init_expr(rd_t *reader, bool show, int depth)
{
    rd_align(reader, 4);
    uint32_t type = rd_u32(reader, "init expression type");
    if (reader->failed || print_const_expr(reader, show, type) ||
        print_ref_expr(reader, show, type)) {
        return;
    }
    const char *ext_name = extended_const_name(type);
    if (ext_name) {
        parse_extended_const(reader, show, ext_name, depth);
    } else if (type == INIT_EXPR_STRUCT_NEW) {
        parse_struct_new(reader, show);
    } else if (type == INIT_EXPR_ARRAY_NEW ||
               type == INIT_EXPR_ARRAY_NEW_DEFAULT ||
               type == INIT_EXPR_ARRAY_NEW_FIXED) {
        parse_array_new(reader, show, type);
    } else {
        error_at(back_u32(reader, 1), "invalid init expression type 0x%" PRIx32,
                 type);
        reader->failed = true;
    }
}

static void parse_memory_info(rd_t *reader, bool show)
{
    uint32_t import_count;
    uint32_t count;
    uint32_t nseg;

    print_if(show, "  [memory]\n");
    import_count = rd_u32(reader, "import memory count");
    print_if(show, "    import memory count: %" PRIu32 "\n", import_count);
    if (import_count) {
        warn_at(back_u32(reader, 1),
                "import memory count is %" PRIu32
                "; wamrc always writes 0 and the "
                "loader does not read import memory entries",
                import_count);
    }

    count = rd_count(reader, MIN_SIZE_MEMORY, "memory count");
    aot.memory_count = count;
    print_if(show, "    memory count: %" PRIu32 "\n", count);
    for (uint32_t i = 0; i < count && !reader->failed; i++) {
        size_t pos = reader->off;
        uint32_t flags = rd_u32(reader, "memory flags");
        uint32_t page = rd_u32(reader, "num_bytes_per_page");
        uint32_t init = rd_u32(reader, "init_page_count");
        uint32_t max = rd_u32(reader, "max_page_count");
        print_if(show,
                 "    memory[%" PRIu32 "]: flags 0x%" PRIx32 "%s%s%s, %" PRIu32
                 " bytes/page, init %" PRIu32 " page(s), max %" PRIu32
                 " page(s)  (initial size %" PRIu64 " bytes)\n",
                 i, flags, flags & MEM_FLAG_HAS_MAX ? " has-max" : "",
                 flags & MEM_FLAG_SHARED ? " shared" : "",
                 flags & MEM_FLAG_MEMORY64 ? " memory64" : "", page, init, max,
                 (uint64_t)page * init);
        if (flags & ~(uint32_t)(MEM_FLAG_HAS_MAX | MEM_FLAG_SHARED |
                                MEM_FLAG_MEMORY64)) {
            error_at(pos, "invalid memory flags 0x%" PRIx32, flags);
        } else if ((flags & MEM_FLAG_SHARED) && !(flags & MEM_FLAG_HAS_MAX)) {
            error_at(pos, "shared memory must have a maximum");
        }
    }

    nseg = rd_count(reader, MIN_SIZE_DATA_SEGMENT, "data segment count");
    print_if(show, "    data segment count: %" PRIu32 "\n", nseg);
    for (uint32_t i = 0; i < nseg && !reader->failed; i++) {
        uint32_t passive;
        uint32_t memidx;
        uint32_t num;
        const uint8_t *bytes;
        size_t pos;

        passive = rd_u32(reader, "is_passive");
        memidx = rd_u32(reader, "memory index");
        print_if(show, "    data[%" PRIu32 "]: %s, memory %" PRIu32 ", offset ",
                 i, passive ? "passive" : "active", memidx);
        parse_init_expr(reader, show, 0);
        num = rd_u32(reader, "data byte count");
        pos = reader->off;
        bytes = rd_bytes(reader, num, "data bytes");
        print_if(show, ", %" PRIu32 " byte(s) at 0x%zx: ", num, pos);
        if (bytes && g_print && show) {
            print_quoted(bytes, num, QUOTE_DATA_MAX);
        }
        print_if(show, "\n");
        if (!reader->failed && memidx >= count) {
            error_at(pos, "data segment memory index %" PRIu32 " out of range",
                     memidx);
        }
    }
}

static const char *elem_mode_name(uint32_t mode)
{
    if ((mode & ELEM_MODE_DECLARATIVE) == ELEM_MODE_DECLARATIVE) {
        return "declarative";
    }
    return mode & ELEM_MODE_PASSIVE ? "passive" : "active";
}

static void parse_table_type(rd_t *reader, bool show, bool is_import,
                             uint32_t idx)
{
    uint8_t elem = rd_u8(reader, "table elem type");
    uint8_t flags = rd_u8(reader, "table flags");
    if (!is_import && flags > (TABLE_FLAG_HAS_MAX | TABLE_FLAG_TABLE64)) {
        /* the entry starts at the elem byte, 2 bytes back */
        error_at(reader->off - 2, "invalid table flags 0x%x", flags);
    }
    uint8_t grow = rd_u8(reader, "table possible_grow");
    uint8_t nullable = rd_u8(reader, "table nullable/padding");
    uint32_t init = rd_u32(reader, "table init size");
    uint32_t max = rd_u32(reader, "table max size");

    print_if(show,
             "    %s[%" PRIu32
             "]: elem %s, flags 0x%x%s%s, possible_grow %u, init %" PRIu32
             ", max %" PRIu32,
             is_import ? "import table" : "table", idx, vt(elem), flags,
             flags & 1 ? " has-max" : "", flags & 4 ? " table64" : "", grow,
             init, max);
    if (aot.gc && is_multi_byte_type(elem)) {
        uint32_t heap_type = rd_u32(reader, "table heap type");
        print_if(show, ", nullable %u, heap type %" PRId32, nullable,
                 (int32_t)heap_type);
    }
    if (!is_import && aot.gc) {
        print_if(show, ", init ");
        parse_init_expr(reader, show, 0);
    }
    print_if(show, "\n");
}

static void parse_table_info(rd_t *reader, bool show)
{
    uint32_t num;

    print_if(show, "  [table]\n");
    num = rd_count(reader, MIN_SIZE_TABLE, "import table count");
    aot.import_table_count = num;
    print_if(show, "    import table count: %" PRIu32 "\n", num);
    for (uint32_t i = 0; i < num && !reader->failed; i++) {
        parse_table_type(reader, show, true, i);
    }

    num = rd_count(reader, MIN_SIZE_TABLE, "table count");
    aot.table_count = num;
    print_if(show, "    table count: %" PRIu32 "\n", num);
    for (uint32_t i = 0; i < num && !reader->failed; i++) {
        parse_table_type(reader, show, false, i);
    }

    num = rd_count(reader, MIN_SIZE_ELEM_SEGMENT,
                   "table init data (elem segment) count");
    print_if(show, "    elem segment count: %" PRIu32 "\n", num);
    for (uint32_t i = 0; i < num && !reader->failed; i++) {
        uint32_t mode = rd_u32(reader, "elem mode");
        uint32_t elem = rd_u32(reader, "elem type");
        uint32_t table = rd_u32(reader, "elem table index");
        uint32_t count;

        print_if(show,
                 "    elem[%" PRIu32 "]: mode %" PRIu32
                 " (%s), elem %s, table %" PRIu32 ", offset ",
                 i, mode, elem_mode_name(mode), vt((uint8_t)elem), table);
        parse_init_expr(reader, show, 0);
        if (aot.gc && is_multi_byte_type((uint8_t)elem)) {
            uint16_t res_type = rd_u16(reader, "elem ref type");
            uint16_t nullable = rd_u16(reader, "elem nullable");
            uint32_t heap_type = rd_u32(reader, "elem heap type");
            print_if(show, ", ref type %s nullable %u heap type %" PRId32,
                     vt((uint8_t)res_type), nullable, (int32_t)heap_type);
            if (res_type != elem) {
                error_at(back_u32(reader, 2),
                         "elem ref type does not match the elem type");
            }
        } else {
            /* 8 bytes: u16 elem_type, u16 0, u32 0 as written by wamrc; skipped
             */
            rd_u16(reader, "elem ref type placeholder");
            rd_u16(reader, "elem ref type placeholder");
            rd_u32(reader, "elem ref type placeholder");
        }
        count = rd_count(reader, 4, "elem value count");
        print_if(show, ", %" PRIu32 " value(s):", count);
        for (uint32_t j = 0; j < count && !reader->failed; j++) {
            print_if(show, j % 4 == 0 ? "\n      " : "  ");
            parse_init_expr(reader, show, 0);
        }
        print_if(show, "\n");
    }
}

static void print_func_type(const type_t *type)
{
    printf("(");
    for (uint32_t j = 0; j < type->param_count; j++) {
        printf("%s%s", j ? " " : "", vt(type->types[j]));
    }
    printf(") -> (");
    for (uint32_t j = 0; j < type->result_count; j++) {
        printf("%s%s", j ? " " : "", vt(type->types[type->param_count + j]));
    }
    printf(")");
}

static void parse_ref_type_maps(rd_t *reader, bool show, uint16_t n)
{
    for (uint16_t j = 0; j < n && !reader->failed; j++) {
        uint16_t index = rd_u16(reader, "ref type map index");
        uint8_t ref_type = rd_u8(reader, "ref type map type");
        uint8_t nullable = rd_u8(reader, "ref type map nullable");
        uint32_t heap_type = rd_u32(reader, "ref type map heap type");
        print_if(show,
                 "      ref_type_map[%u]: index %u, %s, nullable %u, heap type "
                 "%" PRId32 "\n",
                 j, index, vt(ref_type), nullable, (int32_t)heap_type);
    }
}

/* index of `type` in aot.types */
static uint32_t type_index(const type_t *type)
{
    return (uint32_t)(type - aot.types);
}

/* a function type without GC: flag, param count, result count, types */
static void parse_plain_func_type(rd_t *reader, bool show, type_t *type,
                                  uint16_t flag)
{
    const uint32_t idx = type_index(type);
    /* the type flag is not checked by the loader */
    type->kind = WASM_TYPE_FUNC;
    type->param_count = rd_u16(reader, "param count");
    type->result_count = rd_u16(reader, "result count");
    type->types = rd_bytes(
        reader, (size_t)type->param_count + type->result_count, "value types");
    if (reader->failed) {
        return;
    }
    if (flag != WASM_TYPE_FUNC) {
        warn_at(reader->off,
                "type %" PRIu32 " has type flag %u, wamrc writes 0", idx, flag);
    }
    for (uint32_t j = 0; j < (uint32_t)type->param_count + type->result_count;
         j++) {
        if (!is_valid_valtype(type->types[j])) {
            error_at(reader->off,
                     "type %" PRIu32 " has invalid value type 0x%02x", idx,
                     type->types[j]);
        }
    }
    if (g_print && show) {
        printf("    type[%" PRIu32 "]: func ", idx);
        print_func_type(type);
        printf("\n");
    }
}

/* the fields every GC type starts with */
typedef struct {
    uint8_t sub_final;
    uint16_t rec_count;
    uint16_t rec_idx;
    uint32_t parent;
} gc_type_header;

static gc_type_header read_gc_type_header(rd_t *reader)
{
    gc_type_header hdr;
    hdr.sub_final = rd_u8(reader, "is_sub_final");
    hdr.parent = rd_u32(reader, "parent type index");
    hdr.rec_count = rd_u16(reader, "rec count");
    hdr.rec_idx = rd_u16(reader, "rec index");
    return hdr;
}

static void print_gc_type_header(bool show, const gc_type_header *hdr)
{
    print_if(show, ", sub_final %u, parent %" PRId32 ", rec %u/%u",
             hdr->sub_final, (int32_t)hdr->parent, hdr->rec_idx,
             hdr->rec_count);
}

/* a GC type equivalent to an earlier one */
static void parse_equivalent_type(rd_t *reader, bool show, type_t *type)
{
    const uint32_t idx = type_index(type);
    uint32_t equiv_idx;
    rd_u8(reader, "padding");
    equiv_idx = rd_u32(reader, "equivalence type index");
    if (equiv_idx >= idx) {
        error_at(back_u32(reader, 1),
                 "equivalence type index %" PRIu32 " is not before %" PRIu32,
                 equiv_idx, idx);
        reader->failed = true;
        return;
    }
    if (!g_print) {
        *type = aot.types[equiv_idx];
        type->same_as = (int32_t)equiv_idx;
    }
    print_if(show, "    type[%" PRIu32 "]: same as type %" PRIu32 "\n", idx,
             equiv_idx);
}

static void parse_gc_func_type(rd_t *reader, bool show, type_t *type,
                               const gc_type_header *hdr)
{
    type->param_count = rd_u16(reader, "param count");
    type->result_count = rd_u16(reader, "result count");
    uint16_t map_count = rd_u16(reader, "ref type map count");
    type->types = rd_bytes(
        reader, (size_t)type->param_count + type->result_count, "value types");
    if (reader->failed) {
        return;
    }
    if (g_print && show) {
        printf("func ");
        print_func_type(type);
    }
    print_gc_type_header(show, hdr);
    print_if(show, ", %u ref type map(s)\n", map_count);
    if (map_count) {
        rd_align(reader, 4);
        parse_ref_type_maps(reader, show, map_count);
    }
}

static void parse_gc_struct_type(rd_t *reader, bool show, type_t *type,
                                 const gc_type_header *hdr)
{
    type->field_count = rd_u16(reader, "field count");
    uint16_t map_count = rd_u16(reader, "ref type map count");
    type->fields =
        rd_bytes(reader, (size_t)type->field_count * 2, "struct fields");
    if (reader->failed) {
        return;
    }
    print_if(show, "struct {");
    for (uint32_t j = 0; j < type->field_count; j++) {
        print_if(show, "%s%s%s", j ? ", " : " ",
                 type->fields[(size_t)2 * j] & 1 ? "mut " : "",
                 vt(type->fields[((size_t)2 * j) + 1]));
    }
    print_if(show, " }");
    print_gc_type_header(show, hdr);
    print_if(show, ", %u ref type map(s)\n", map_count);
    rd_align(reader, 4);
    parse_ref_type_maps(reader, show, map_count);
}

static void parse_gc_array_type(rd_t *reader, bool show, type_t *type,
                                const gc_type_header *hdr)
{
    uint16_t eflags = rd_u16(reader, "array elem flags");
    type->elem_type = rd_u8(reader, "array elem type");
    print_if(show, "array [%s%s]", eflags & 1 ? "mut " : "",
             vt(type->elem_type));
    if (is_multi_byte_type(type->elem_type)) {
        uint8_t nullable = rd_u8(reader, "array elem nullable");
        uint32_t heap_type = rd_u32(reader, "array elem heap type");
        print_if(show, " nullable %u heap type %" PRId32, nullable,
                 (int32_t)heap_type);
    }
    print_gc_type_header(show, hdr);
    print_if(show, "\n");
}

/* a type with GC: an equivalence, or a func, struct or array type */
static void parse_gc_type(rd_t *reader, bool show, type_t *type, uint16_t flag)
{
    if (rd_u8(reader, "equivalence type flag")) {
        parse_equivalent_type(reader, show, type);
        return;
    }
    gc_type_header hdr = read_gc_type_header(reader);
    type->kind = flag;
    print_if(show, "    type[%" PRIu32 "]: ", type_index(type));
    switch (flag) {
    case WASM_TYPE_FUNC:
        parse_gc_func_type(reader, show, type, &hdr);
        break;
    case WASM_TYPE_STRUCT:
        parse_gc_struct_type(reader, show, type, &hdr);
        break;
    case WASM_TYPE_ARRAY:
        parse_gc_array_type(reader, show, type, &hdr);
        break;
    default:
        error_at(reader->off, "invalid type flag %u", flag);
        reader->failed = true;
        break;
    }
}

static void parse_type_info(rd_t *reader, bool show)
{
    uint32_t count;

    print_if(show, "  [type]\n");
    count = rd_count(reader, MIN_SIZE_TYPE, "type count");
    if (!g_print) {
        aot.type_count = count;
        aot.types = xcalloc(count, sizeof(type_t));
    }
    print_if(show, "    type count: %" PRIu32 "\n", count);

    for (uint32_t i = 0; i < count && !reader->failed; i++) {
        type_t *type = &aot.types[i];
        rd_align(reader, 4);
        uint16_t flag = rd_u16(reader, "type flag");
        type->same_as = -1;
        if (aot.gc) {
            parse_gc_type(reader, show, type, flag);
        } else {
            parse_plain_func_type(reader, show, type, flag);
        }
    }
}

static void parse_import_globals(rd_t *reader, bool show)
{
    uint32_t count;

    print_if(show, "  [import global]\n");
    count = rd_count(reader, MIN_SIZE_IMPORT_GLOBAL, "import global count");
    aot.import_global_count = count;
    print_if(show, "    import global count: %" PRIu32 "\n", count);
    for (uint32_t i = 0; i < count && !reader->failed; i++) {
        uint8_t type;
        uint8_t mut;
        const char *mod;
        const char *name;
        rd_align(reader, 2);
        type = rd_u8(reader, "import global type");
        mut = rd_u8(reader, "import global mutability");
        mod = rd_str(reader, "import global module name");
        name = rd_str(reader, "import global name");
        print_if(show, "    import global[%" PRIu32 "]: %s%s \"%s\".\"%s\"\n",
                 i, mut ? "mut " : "", vt(type), mod, name);
        if (!reader->failed && !aot.gc && !is_valid_valtype(type)) {
            error_at(reader->off, "invalid import global type 0x%02x", type);
        }
    }
}

static void parse_globals(rd_t *reader, bool show)
{
    uint32_t count;

    print_if(show, "  [global]\n");
    count = rd_count(reader, MIN_SIZE_GLOBAL, "global count");
    aot.global_count = count;
    print_if(show, "    global count: %" PRIu32 "\n", count);
    if ((uint64_t)aot.import_global_count + count > UINT32_MAX) {
        error_at(reader->off, "too many globals");
    }
    for (uint32_t i = 0; i < count && !reader->failed; i++) {
        uint8_t type = rd_u8(reader, "global type");
        uint8_t mut = rd_u8(reader, "global mutability");
        print_if(show,
                 "    global[%" PRIu32 "] (index %" PRIu32 "): %s%s = ", i,
                 aot.import_global_count + i, mut ? "mut " : "", vt(type));
        parse_init_expr(reader, show, 0);
        print_if(show, "\n");
        if (!reader->failed && !aot.gc && !is_valid_valtype(type)) {
            error_at(reader->off, "invalid global type 0x%02x", type);
        }
    }
}

static void parse_import_funcs(rd_t *reader, bool show)
{
    uint32_t count;

    print_if(show, "  [import function]\n");
    count = rd_count(reader, MIN_SIZE_IMPORT_FUNC, "import function count");
    aot.import_func_count = count;
    print_if(show, "    import function count: %" PRIu32 "\n", count);
    for (uint32_t i = 0; i < count && !reader->failed; i++) {
        uint16_t type = rd_u16(reader, "import function type index");
        const char *mod = rd_str(reader, "import function module name");
        const char *name = rd_str(reader, "import function name");
        if (reader->failed) {
            break;
        }
        print_if(show, "    import func[%" PRIu32 "]: \"%s\".\"%s\" type %u", i,
                 mod, name, type);
        if (type >= aot.type_count) {
            error_at(reader->off, "import function type index %u out of range",
                     type);
        } else if (g_print && show && aot.types[type].kind == WASM_TYPE_FUNC) {
            printf(" ");
            print_func_type(&aot.types[type]);
        }
        print_if(show, "\n");
    }
}

static void print_global_ref(bool show, uint32_t idx)
{
    if (idx == UINT32_MAX) {
        print_if(show, "none (0xffffffff)");
    } else {
        print_if(show, "%" PRIu32, idx);
    }
}

static void parse_object_data_sections(rd_t *reader, bool show)
{
    uint32_t count;

    print_if(show, "  [object data sections]\n");
    count = rd_count(reader, MIN_SIZE_OBJECT_DATA, "object data section count");
    aot.data_section_count = count;
    print_if(show, "    object data section count: %" PRIu32 "\n", count);
    for (uint32_t i = 0; i < count && !reader->failed; i++) {
        const char *name = rd_str(reader, "object data section name");
        uint32_t size = rd_u32(reader, "object data section size");
        size_t pos = reader->off;
        const uint8_t *data =
            rd_bytes(reader, size, "object data section bytes");
        print_if(show,
                 "    data section[%" PRIu32 "]: \"%s\", %" PRIu32
                 " byte(s) at 0x%zx\n",
                 i, name, size, pos);
        if (data && g_print && show && opt.hexdump && size) {
            hexdump(pos, pos + size);
        }
    }
}

static void parse_init_data(const section_t *sec, bool show)
{
    rd_t reader = rd_sub(aot.buf, sec->start, sec->end);
    uint32_t func_count;
    uint32_t start;
    uint32_t de_g;
    uint32_t hb_g;
    uint32_t st_g;
    uint32_t st_size;
    uint64_t data_end;
    uint64_t heap_base;
    uint64_t stack_bottom;

    parse_memory_info(&reader, show);
    parse_table_info(&reader, show);
    parse_type_info(&reader, show);
    parse_import_globals(&reader, show);
    parse_globals(&reader, show);
    parse_import_funcs(&reader, show);

    print_if(show, "  [function count, start function, auxiliary data]\n");
    func_count = rd_u32(&reader, "function count");
    start = rd_u32(&reader, "start function index");
    de_g = rd_u32(&reader, "aux data end global index");
    data_end = rd_u64(&reader, "aux data end");
    hb_g = rd_u32(&reader, "aux heap base global index");
    heap_base = rd_u64(&reader, "aux heap base");
    st_g = rd_u32(&reader, "aux stack top global index");
    stack_bottom = rd_u64(&reader, "aux stack bottom");
    st_size = rd_u32(&reader, "aux stack size");
    if (reader.failed) {
        return;
    }

    aot.func_count = func_count;
    aot.start_func_index = start;
    print_if(show, "    function count (defined): %" PRIu32 "\n", func_count);
    print_if(show, "    start function index: ");
    if (start == UINT32_MAX) {
        print_if(show, "none (0xffffffff)\n");
    } else {
        print_if(show, "%" PRIu32 "\n", start);
    }
    print_if(show, "    aux data end:   global ");
    print_global_ref(show, de_g);
    print_if(show, ", value %" PRIu64 "\n", data_end);
    print_if(show, "    aux heap base:  global ");
    print_global_ref(show, hb_g);
    print_if(show, ", value %" PRIu64 "\n", heap_base);
    print_if(show, "    aux stack top:  global ");
    print_global_ref(show, st_g);
    print_if(show, ", stack bottom %" PRIu64 ", stack size %" PRIu32 "\n",
             stack_bottom, st_size);

    if ((uint64_t)aot.import_func_count + func_count > UINT32_MAX) {
        error_at(reader.off, "too many functions");
    }
    if (start != UINT32_MAX &&
        (uint64_t)start >= (uint64_t)aot.import_func_count + func_count) {
        error_at(reader.off, "start function index %" PRIu32 " out of range",
                 start);
    }
    if (data_end >= MAX_LINEAR_MEMORY_SIZE ||
        heap_base >= MAX_LINEAR_MEMORY_SIZE ||
        stack_bottom >= MAX_LINEAR_MEMORY_SIZE) {
        error_at(reader.off,
                 "aux data end / heap base / stack bottom must be below 4 GiB");
    }

    parse_object_data_sections(&reader, show);

    if (!reader.failed && reader.off != sec->end) {
        error_at(reader.off, "init data section has %zu trailing byte(s)",
                 sec->end - reader.off);
    }
}

/* ------------------------------------------------------------------------ */
/* Text section                                                             */
/* ------------------------------------------------------------------------ */

static const char *func_name(uint32_t idx)
{
    uint64_t total = (uint64_t)aot.import_func_count + aot.func_count;
    if (aot.func_names && idx < total && aot.func_names[idx]) {
        return aot.func_names[idx];
    }
    return NULL;
}

static int cmp_u64(const void *lhs, const void *rhs)
{
    uint64_t val = *(const uint64_t *)lhs;
    uint64_t val2 = *(const uint64_t *)rhs;
    return val < val2 ? -1 : val > val2;
}

/* end of function `idx`'s code: the next higher function start, or end of
 * code */
static uint64_t func_code_end(uint32_t idx, const uint64_t *sorted)
{
    uint64_t start = aot.func_offsets[idx];
    for (uint32_t k = 0; k < aot.func_count; k++) {
        if (sorted[k] > start) {
            return sorted[k];
        }
    }
    return aot.code_size;
}

static void parse_text(const section_t *sec, bool show)
{
    rd_t reader = rd_sub(aot.buf, sec->start, sec->end);
    uint32_t lit;

    if (aot.func_count > 0 && sec->start == sec->end) {
        error_at(sec->start,
                 "text section is empty but the module has functions");
    }
    lit = rd_u32(&reader, "literal size");
    if (reader.failed) {
        return;
    }
    if (lit > rd_left(&reader)) {
        error_at(back_u32(&reader, 1),
                 "literal size %" PRIu32 " exceeds the text section", lit);
        return;
    }
    aot.literal_size = lit;
    aot.code_off =
        reader.off + lit; /* the loader does NOT re-align after the literal */
    aot.code_size = sec->end - aot.code_off;
    aot.have_text = true;
    if (lit & 3) {
        warn_at(reader.off,
                "literal size %" PRIu32
                " is not a multiple of 4: wamrc pads the literal "
                "but the loader places the code right after literal_size bytes",
                lit);
    }

    print_if(show, "  literal size: %" PRIu32 " byte(s) at 0x%zx\n", lit,
             reader.off);
    if (lit && g_print && show && opt.hexdump) {
        hexdump(reader.off, reader.off + lit);
    }
    print_if(show, "  code: %zu byte(s) at file offset 0x%zx\n", aot.code_size,
             aot.code_off);

    if (g_print && show && aot.have_funcs && aot.func_count) {
        uint64_t *sorted = xcalloc(aot.func_count, sizeof(uint64_t));
        memcpy(sorted, aot.func_offsets, aot.func_count * sizeof(uint64_t));
        qsort(sorted, aot.func_count, sizeof(uint64_t), cmp_u64);
        printf("  function map (code offsets; end = next function start):\n");
        for (uint32_t i = 0; i < aot.func_count; i++) {
            uint64_t start = aot.func_offsets[i];
            uint64_t end = func_code_end(i, sorted);
            const char *fname = func_name(aot.import_func_count + i);
            printf("    aot_func#%-4" PRIu32 " [0x%06" PRIx64 ", 0x%06" PRIx64
                   ") %6" PRIu64 " bytes  file 0x%06" PRIx64 "%s%s\n",
                   i, start, end, end - start, (uint64_t)aot.code_off + start,
                   fname ? "  " : "", fname ? fname : "");
        }
        free(sorted);
    }
    if (g_print && show && opt.hexdump && aot.code_size) {
        hexdump(aot.code_off, aot.code_off + aot.code_size);
    }
}

/* ------------------------------------------------------------------------ */
/* Function section                                                         */
/* ------------------------------------------------------------------------ */

static void read_func_offsets(rd_t *reader, uint32_t count)
{
    for (uint32_t i = 0; i < count && !reader->failed; i++) {
        uint64_t off = aot.is64 ? rd_u64(reader, "text offset")
                                : rd_u32(reader, "text offset");
        if (!g_print) {
            aot.func_offsets[i] = off;
        }
        if (!reader->failed && aot.have_text && off >= aot.code_size) {
            error_at(reader->off,
                     "function %" PRIu32 " text offset 0x%" PRIx64
                     " is outside the code (%zu bytes)",
                     i, off, aot.code_size);
        }
    }
}

static void read_func_types(rd_t *reader, uint32_t count)
{
    for (uint32_t i = 0; i < count && !reader->failed; i++) {
        uint32_t type = rd_u32(reader, "function type index");
        if (!g_print) {
            aot.func_types[i] = type;
        }
        if (!reader->failed && type >= aot.type_count) {
            error_at(reader->off,
                     "function %" PRIu32 " type index %" PRIu32 " out of range",
                     i, type);
        }
    }
}

/* one line per function; its max locals and max stack (u32 each, `count`
 * of each) are at the reader's position */
static void print_func_table(const rd_t *reader, uint32_t count)
{
    const uint8_t *locals = aot.buf + reader->off;
    const uint8_t *stacks = locals + ((size_t)count * 4);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t nlocal = load_le32(locals + ((size_t)i * 4));
        uint32_t stack_size = load_le32(stacks + ((size_t)i * 4));
        const char *fname = func_name(aot.import_func_count + i);
        printf("  aot_func#%-3" PRIu32 " %-6" PRIu32 " 0x%-8" PRIx64
               " %-10" PRIu32 " %-10" PRIu32 " %-6" PRIu32,
               i, aot.import_func_count + i, aot.func_offsets[i],
               aot.func_types[i], nlocal, stack_size);
        if (aot.func_types[i] < aot.type_count &&
            aot.types[aot.func_types[i]].kind == WASM_TYPE_FUNC) {
            printf(" ");
            print_func_type(&aot.types[aot.func_types[i]]);
        }
        if (fname) {
            printf("  %s", fname);
        }
        printf("\n");
    }
}

/* GC: per function (imported ones included), the cells holding references */
static void parse_local_ref_flags(rd_t *reader, bool show, uint32_t count)
{
    uint64_t total = (uint64_t)aot.import_func_count + count;
    print_if(show, "  local ref flags (GC):\n");
    for (uint64_t i = 0; i < total && !reader->failed; i++) {
        uint32_t cells = rd_count(reader, 1, "local ref flag cell count");
        const uint8_t *flags = rd_bytes(reader, cells, "local ref flags");
        print_if(show, "    func %-5" PRIu64 " %" PRIu32 " cell(s): ", i,
                 cells);
        for (uint32_t j = 0; flags && j < cells; j++) {
            print_if(show, "%u", flags[j]);
        }
        print_if(show, "\n");
    }
}

static void parse_function(const section_t *sec, bool show)
{
    rd_t reader = rd_sub(aot.buf, sec->start, sec->end);
    uint32_t count = aot.func_count;
    /* per function: its code offset (pointer size), then u32 type index, max
     * locals and max stack size */
    size_t min = (size_t)count * (aot.ptr_size + (3 * sizeof(uint32_t)));

    if (min > sec->end - sec->start) {
        error_at(sec->start,
                 "function section (%zu bytes) is too small for %" PRIu32
                 " functions (%zu bytes needed)",
                 sec->end - sec->start, count, min);
        return;
    }
    if (!g_print) {
        aot.func_offsets = xcalloc(count, sizeof(uint64_t));
        aot.func_types = xcalloc(count, sizeof(uint32_t));
    }
    read_func_offsets(&reader, count);
    read_func_types(&reader, count);
    aot.have_funcs = !reader.failed;

    print_if(show, "  %-12s %-6s %-10s %-10s %-10s %s\n", "function", "index",
             "text off", "type", "locals", "stack");
    if (g_print && show) {
        print_func_table(&reader, count);
    }
    print_if(
        show,
        "  (locals = max_local_cell_num = param cells + local cells; stack = "
        "max_stack_cell_num;"
        " 1 cell = 4 bytes)\n");
    for (uint32_t i = 0; i < 2 * count && !reader.failed; i++) {
        rd_u32(&reader, "cell count");
    }
    if (aot.gc) {
        parse_local_ref_flags(&reader, show, count);
    }
    if (!reader.failed && reader.off != sec->end) {
        error_at(reader.off, "function section has %zu trailing byte(s)",
                 sec->end - reader.off);
    }
}

/* ------------------------------------------------------------------------ */
/* Export section                                                           */
/* ------------------------------------------------------------------------ */

static void parse_export(const section_t *sec, bool show)
{
    rd_t reader = rd_sub(aot.buf, sec->start, sec->end);
    uint32_t count = rd_count(&reader, MIN_SIZE_EXPORT, "export count");
    const char **names = (const char **)xcalloc(count, sizeof(char *));

    print_if(show, "  export count: %" PRIu32 "\n", count);
    for (uint32_t i = 0; i < count && !reader.failed; i++) {
        uint32_t index = rd_u32(&reader, "export index");
        uint8_t kind = rd_u8(&reader, "export kind");
        const char *name = rd_str(&reader, "export name");
        uint64_t limit = 0;

        if (reader.failed) {
            break;
        }
        names[i] = name;
        print_if(show, "  export[%" PRIu32 "]: %-6s %-5" PRIu32 " \"%s\"\n", i,
                 export_kind_name(kind), index, name);
        switch (kind) {
        case 0:
            limit = (uint64_t)aot.import_func_count + aot.func_count;
            break;
        case 1:
            limit = (uint64_t)aot.import_table_count + aot.table_count;
            break;
        case 2:
            limit = aot.memory_count;
            break;
        case 3:
            limit = (uint64_t)aot.import_global_count + aot.global_count;
            break;
        default:
            error_at(reader.off, "invalid export kind %u", kind);
            continue;
        }
        if (index >= limit) {
            error_at(reader.off, "export \"%s\" index %" PRIu32 " out of range",
                     name, index);
        }
        if (kind == 0 && !g_print && aot.func_names && index < limit &&
            !aot.func_names[index]) {
            aot.func_names[index] = name;
        }
    }
    for (uint32_t i = 0; i < count && !reader.failed && !g_print; i++) {
        for (uint32_t j = i + 1; j < count; j++) {
            if (names[i] && names[j] && !strcmp(names[i], names[j])) {
                error_at(sec->start, "duplicate export name \"%s\"", names[i]);
            }
        }
    }
    free((void *)names);

    if (!reader.failed && reader.off != sec->end) {
        error_at(reader.off, "export section has %zu trailing byte(s)",
                 sec->end - reader.off);
    }
}

/* ------------------------------------------------------------------------ */
/* Relocation section                                                       */
/* ------------------------------------------------------------------------ */

static const char *symbol_name(uint32_t idx)
{
    return idx < aot.symbol_count && aot.symbols ? aot.symbols[idx]
                                                 : "<bad symbol index>";
}

/* What the WAMR runtime resolves a relocation symbol to. */
static const char *symbol_kind(const char *sym)
{
    if (starts_with(sym, ".rel")) {
        return "relocation group name";
    }
    if (starts_with(sym, AOT_FUNC_PREFIX)) {
        return "AOT function";
    }
    if (starts_with(sym, "aot_func_internal#")) {
        return "AOT function (internal)";
    }
    if (!strcmp(sym, ".text") || !strcmp(sym, ".ltext")) {
        return "start of code";
    }
    if (!strcmp(sym, ".literal")) {
        return "literal pool";
    }
    if (!strcmp(sym, ".data") || !strcmp(sym, ".sdata") ||
        !strcmp(sym, ".rdata") || !strcmp(sym, ".rodata") ||
        !strcmp(sym, ".srodata") || starts_with(sym, ".rodata.cst") ||
        starts_with(sym, ".srodata.cst") || starts_with(sym, ".rodata.str") ||
        !strcmp(sym, ".aot_stack_sizes") || starts_with(sym, "__llvm_prf_")) {
        return "object data section";
    }
    return "runtime symbol";
}

static bool is_text_group(const char *group)
{
    return !strcmp(group, ".rel.text") || !strcmp(group, ".rela.text") ||
           !strcmp(group, ".rel.ltext") || !strcmp(group, ".rela.ltext") ||
           !strcmp(group, ".rela.literal") || !strcmp(group, ".text");
}

/* the symbol table of the relocation section */
typedef struct {
    uint32_t nsym;
    uint32_t total;    /* bytes of strings */
    size_t offsets_at; /* nsym u32 string offsets */
    size_t strings_at; /* the strings */
} symtab_t;

static bool read_symbol_table(rd_t *reader, symtab_t *tab)
{
    tab->nsym = rd_count(reader, 4 + 2, "symbol count");
    tab->offsets_at = reader->off;
    rd_bytes(reader, (size_t)tab->nsym * 4, "symbol offsets");
    tab->total = rd_u32(reader, "total string length");
    tab->strings_at = reader->off;
    if (reader->failed) {
        return false;
    }
    if (tab->total > rd_left(reader)) {
        error_at(back_u32(reader, 1),
                 "total string length %" PRIu32 " exceeds the section",
                 tab->total);
        return false;
    }
    if (!g_print) {
        aot.symbol_count = tab->nsym;
        aot.symbols = (const char **)xcalloc(tab->nsym, sizeof(char *));
    }
    return true;
}

/* The symbol names; the offsets must match the strings exactly
 * (validate_symbol_table). */
static void load_symbol_names(const symtab_t *tab)
{
    rd_t str_rd =
        rd_sub(aot.buf, tab->strings_at, tab->strings_at + tab->total);
    for (uint32_t i = 0; i < tab->nsym && !str_rd.failed; i++) {
        size_t off_at = tab->offsets_at + ((size_t)i * 4);
        uint32_t sym_off = load_le32(aot.buf + off_at);
        if (tab->strings_at + sym_off != str_rd.off) {
            error_at(off_at,
                     "symbol %" PRIu32 " offset %" PRIu32
                     " does not match the string table layout (expected %zu)",
                     i, sym_off, str_rd.off - tab->strings_at);
        }
        const char *name = rd_str(&str_rd, "symbol name");
        rd_align(&str_rd, 2);
        if (!g_print) {
            aot.symbols[i] = name;
        }
    }
    if (!str_rd.failed && str_rd.off != str_rd.end) {
        error_at(str_rd.off, "symbol string table has %zu unused byte(s)",
                 str_rd.end - str_rd.off);
    }
}

static void print_symbol_table(bool show, const symtab_t *tab)
{
    print_if(show,
             "  symbol table: %" PRIu32 " symbol(s), %" PRIu32
             " string byte(s)\n",
             tab->nsym, tab->total);
    for (uint32_t i = 0; i < tab->nsym && g_print && show; i++) {
        printf("    sym[%3" PRIu32 "] %-36s %s\n", i, symbol_name(i),
               symbol_kind(symbol_name(i)));
    }
}

/* a relocation group: its name and whether it patches the code */
typedef struct {
    const char *name;
    bool text;
} reloc_group_t;

typedef struct {
    uint64_t off;
    int64_t addend;
    uint32_t type;
    uint32_t sym;
} reloc_t;

static reloc_t read_reloc(rd_t *reader)
{
    reloc_t rel;
    rel.off = aot.is64 ? rd_u64(reader, "relocation offset")
                       : rd_u32(reader, "relocation offset");
    rel.addend = aot.is64
                     ? (int64_t)rd_u64(reader, "relocation addend")
                     : (int64_t)(int32_t)rd_u32(reader, "relocation addend");
    rel.type = rd_u32(reader, "relocation type");
    rel.sym = rd_u32(reader, "relocation symbol index");
    return rel;
}

static void print_reloc(const reloc_t *rel, const char *tname)
{
    char tbuf[RELOC_LABEL_LEN];
    const char *sym = symbol_name(rel->sym);
    snprintf(tbuf, sizeof tbuf, "%s(%" PRIu32 ")", tname, rel->type);
    printf("    0x%08" PRIx64 " %-12" PRId64 " %-26s %s", rel->off, rel->addend,
           tbuf, sym);
    if (starts_with(sym, AOT_FUNC_PREFIX)) {
        const char *fname =
            func_name(aot.import_func_count + aot_func_index(sym));
        if (fname) {
            printf("  <%s>", fname);
        }
    }
    printf("\n");
}

/* one relocation of `group`; false once the section cannot be read */
static bool parse_reloc(rd_t *reader, bool show, const reloc_group_t *group,
                        const symtab_t *tab)
{
    const reloc_t rel = read_reloc(reader);
    bool supported;
    if (reader->failed) {
        return false;
    }
    const char *tname = reloc_type_name(rel.type, &supported);
    if (rel.sym >= tab->nsym) {
        error_at(back_u32(reader, 1),
                 "relocation symbol index %" PRIu32 " out of range", rel.sym);
    }
    if (!supported) {
        warn_at(back_u32(reader, 2),
                "relocation type %" PRIu32 " (%s) is not applied by the WAMR "
                "runtime for this architecture",
                rel.type, tname);
    }
    if (group->text && aot.have_text) {
        uint64_t limit = strcmp(group->name, ".rela.literal") == 0
                             ? aot.literal_size
                             : aot.code_size;
        if (rel.off >= limit) {
            error_at(reader->off,
                     "relocation offset 0x%" PRIx64
                     " is outside the patched area",
                     rel.off);
        }
    }
    if (g_print && show) {
        print_reloc(&rel, tname);
    }
    return true;
}

static void parse_reloc_group(rd_t *reader, bool show, uint32_t gidx,
                              const symtab_t *tab)
{
    uint32_t name_idx = rd_u32(reader, "group name index");
    uint32_t count =
        rd_count(reader, aot.is64 ? MIN_SIZE_RELOC64 : MIN_SIZE_RELOC32,
                 "relocation count");
    reloc_group_t group;
    group.name = symbol_name(name_idx);
    group.text = is_text_group(group.name);

    if (name_idx >= tab->nsym) {
        error_at(back_u32(reader, 2),
                 "group name index %" PRIu32 " out of range", name_idx);
    }
    print_if(show,
             "  group[%" PRIu32 "] \"%s\" (%s): %" PRIu32 " relocation(s)\n",
             gidx, group.name,
             group.text ? "patches the code" : "patches an object data section",
             count);
    if (aot.xip && group.text && count) {
        warn_at(reader->off,
                "XIP module has %" PRIu32
                " text relocation(s) in \"%s\"; the runtime "
                "rejects them on non-Linux/Darwin/Windows platforms",
                count, group.name);
    }
    print_if(show, "    %-10s %-12s %-26s %s\n", "offset", "addend", "type",
             "symbol");
    for (uint32_t i = 0; i < count && !reader->failed; i++) {
        if (!parse_reloc(reader, show, &group, tab)) {
            break;
        }
    }
}

static void parse_relocation(const section_t *sec, bool show)
{
    rd_t reader = rd_sub(aot.buf, sec->start, sec->end);
    symtab_t tab;

    if (!read_symbol_table(&reader, &tab)) {
        return;
    }
    load_symbol_names(&tab);
    print_symbol_table(show, &tab);

    reader.off = tab.strings_at + tab.total;
    uint32_t ngroups =
        rd_count(&reader, MIN_SIZE_RELOC_GROUP, "relocation group count");
    print_if(show, "  relocation group count: %" PRIu32 "\n", ngroups);
    for (uint32_t gidx = 0; gidx < ngroups && !reader.failed; gidx++) {
        parse_reloc_group(&reader, show, gidx, &tab);
    }
    if (!reader.failed && reader.off != sec->end) {
        error_at(reader.off, "relocation section has %zu trailing byte(s)",
                 sec->end - reader.off);
    }
}

/* ------------------------------------------------------------------------ */
/* Custom sections                                                          */
/* ------------------------------------------------------------------------ */

static void custom_raw(rd_t *reader, const section_t *sec, bool show)
{
    const char *name = rd_str(reader, "custom section name");
    size_t pos = reader->off;
    print_if(show, "  raw WASM custom section \"%s\": %zu byte(s) at 0x%zx\n",
             name, sec->end - pos, pos);
    if (g_print && show && opt.hexdump && !reader->failed) {
        hexdump(pos, sec->end);
    }
}

/* what a native symbol name stands for, if it is not a function */
static const char *native_symbol_note(const char *sym)
{
    if (starts_with(sym, "__ignore")) {
        return "  (padding / high half of a 64-bit constant)";
    }
    if (starts_with(sym, "i32#") || starts_with(sym, "f32#") ||
        starts_with(sym, "i64#") || starts_with(sym, "f64#")) {
        return "  (constant: raw hex bits)";
    }
    return "";
}

static void custom_native_symbols(rd_t *reader, const section_t *sec, bool show)
{
    uint32_t count = rd_count(reader, 2, "native symbol count");
    /* stored in reverse: file entry k is native_symbol[n - 1 - k] at run
     * time */
    print_if(show,
             "  native symbol list (XIP): %" PRIu32
             " entr%s (slot = index in the runtime "
             "table; file order is reversed)\n",
             count, count == 1 ? "y" : "ies");
    for (uint32_t i = 0; i < count && !reader->failed; i++) {
        const char *sym = rd_str(reader, "native symbol");
        print_if(show, "    slot %3" PRIu32 ": %s%s\n", count - 1 - i, sym,
                 native_symbol_note(sym));
    }
    if (!reader->failed && reader->off != sec->end) {
        error_at(reader->off, "native symbol section has %zu trailing byte(s)",
                 sec->end - reader->off);
    }
}

/* name section subsection types (WASM) */
enum { NAME_MODULE = 0, NAME_FUNCTION = 1, NAME_LOCAL = 2 };

static const char *name_subsection_kind(uint32_t type)
{
    switch (type) {
    case NAME_MODULE:
        return "module";
    case NAME_FUNCTION:
        return "function";
    case NAME_LOCAL:
        return "local";
    default:
        return "?";
    }
}

/* the function names subsection: also fills aot.func_names in pass 1 */
static void parse_function_names(rd_t *reader, bool show)
{
    uint32_t count =
        rd_count(reader, MIN_SIZE_FUNC_NAME, "function name count");
    for (uint32_t i = 0; i < count && !reader->failed; i++) {
        uint32_t idx = rd_u32(reader, "function index");
        const char *fname = rd_str(reader, "function name");
        print_if(show, "      func %-5" PRIu32 " %s\n", idx, fname);
        if (!g_print && aot.func_names &&
            idx < (uint64_t)aot.import_func_count + aot.func_count) {
            aot.func_names[idx] = fname;
        }
    }
}

static void custom_name(rd_t *reader, bool show)
{
    uint32_t len = rd_u32(reader, "name length");
    const uint8_t *name_str = rd_bytes(reader, len, "\"name\"");
    uint32_t prev = 0;
    bool first = true;
    if (reader->failed) {
        return;
    }
    if (len != 4 || memcmp(name_str, "name", 4) != 0) {
        error_at(reader->off - len,
                 "name section does not start with \"name\"");
        return;
    }
    print_if(show, "  name section\n");
    while (rd_left(reader) && !reader->failed) {
        uint32_t type = rd_u32(reader, "name subsection type");
        uint32_t size = rd_u32(reader, "name subsection size");
        if (reader->failed) {
            return;
        }
        if (!first && type <= prev) {
            error_at(back_u32(reader, 2),
                     "name subsections out of order or duplicated");
        }
        first = false;
        prev = type;
        /* size is the original WASM (LEB128) subsection size: only checked
         * to fit */
        print_if(show,
                 "    subsection type %" PRIu32 " (%s), original size %" PRIu32
                 "\n",
                 type, name_subsection_kind(type), size);
        if (size > rd_left(reader)) {
            error_at(reader->off, "name subsection size exceeds the section");
            return;
        }
        if (type == NAME_FUNCTION && size) {
            parse_function_names(reader, show);
        } else {
            reader->off += size; /* wamrc only emits the function subsection */
        }
    }
}

static void custom_string_literals(rd_t *reader, bool show)
{
    uint32_t reserved = rd_u32(reader, "reserved");
    uint32_t count = rd_count(reader, 4, "string literal count");
    if (reserved) {
        error_at(back_u32(reader, 2),
                 "string literal reserved slot is not zero");
    }
    if (!reader->failed && count == 0) {
        error_at(back_u32(reader, 1), "string literal count is zero");
    }
    const uint8_t *lens =
        rd_bytes(reader, (size_t)count * 4, "string literal lengths");
    print_if(show, "  string literals: %" PRIu32 "\n", count);
    for (uint32_t i = 0; lens && i < count && !reader->failed; i++) {
        uint32_t slen = load_le32(lens + ((size_t)i * 4));
        const uint8_t *str = rd_bytes(reader, slen, "string literal");
        print_if(show, "    [%" PRIu32 "] ", i);
        if (str && g_print && show) {
            print_quoted(str, slen, QUOTE_STRING_MAX);
        }
        print_if(show, "\n");
    }
}

static void parse_custom(const section_t *sec, bool show)
{
    rd_t reader = rd_sub(aot.buf, sec->start, sec->end);
    uint32_t sub = rd_u32(&reader, "custom subsection type");

    if (reader.failed) {
        return;
    }
    switch (sub) {
    case CUSTOM_RAW:
        custom_raw(&reader, sec, show);
        break;
    case CUSTOM_NATIVE_SYMBOL:
        custom_native_symbols(&reader, sec, show);
        break;
    case CUSTOM_ACCESS_CONTROL:
        print_if(
            show,
            "  access control (reserved, ignored by the loader): %zu byte(s)\n",
            sec->end - reader.off);
        break;
    case CUSTOM_NAME:
        custom_name(&reader, show);
        break;
    case CUSTOM_STRING_LITERAL:
        custom_string_literals(&reader, show);
        break;
    default:
        print_if(show,
                 "  unknown custom subsection type %" PRIu32
                 " (ignored by the loader), %zu byte(s)\n",
                 sub, sec->end - reader.off);
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* Disassembly through a temporary ELF relocatable object                   */
/* ------------------------------------------------------------------------ */

typedef struct {
    uint8_t *p;
    size_t len, cap;
} buf_t;

#define BUF_MIN_GROWTH 256

static void b_put(buf_t *buf, const void *data, size_t n)
{
    if (buf->len + n > buf->cap) {
        buf->cap = ((buf->len + n) * 2) + BUF_MIN_GROWTH;
        uint8_t *ptr = realloc(buf->p, buf->cap);
        if (!ptr) {
            fprintf(stderr, "readaot: out of memory\n");
            exit(2);
        }
        buf->p = ptr;
    }
    memcpy(buf->p + buf->len, data, n);
    buf->len += n;
}

static void b_u8(buf_t *buf, uint8_t val)
{
    b_put(buf, &val, 1);
}
/* little endian: the low half first */
static void b_u16(buf_t *buf, uint16_t val)
{
    b_u8(buf, (uint8_t)val);
    b_u8(buf, (uint8_t)(val >> CHAR_BIT));
}
static void b_u32(buf_t *buf, uint32_t val)
{
    b_u16(buf, (uint16_t)val);
    b_u16(buf, (uint16_t)(val >> (CHAR_BIT * sizeof(uint16_t))));
}
static void b_u64(buf_t *buf, uint64_t val)
{
    b_u32(buf, (uint32_t)val);
    b_u32(buf, (uint32_t)(val >> (CHAR_BIT * sizeof(uint32_t))));
}
/* an ELF address or size: 4 or 8 bytes */
static void b_addr(buf_t *buf, uint64_t val)
{
    if (aot.is64) {
        b_u64(buf, val);
    } else {
        b_u32(buf, (uint32_t)val);
    }
}
static void b_zeros(buf_t *buf, size_t n)
{
    while (n--) {
        b_u8(buf, 0);
    }
}
static void b_str(buf_t *buf, const char *str)
{
    b_put(buf, str, strlen(str) + 1);
}
static void b_pad(buf_t *buf, size_t alignment)
{
    while (buf->len % alignment) {
        b_u8(buf, 0);
    }
}

/* the sections of the ELF object, in section header order */
enum {
    ELF_SEC_NULL,
    ELF_SEC_TEXT,
    ELF_SEC_SYMTAB,
    ELF_SEC_STRTAB,
    ELF_SEC_SHSTRTAB,
    ELF_SEC_COUNT,
};

typedef struct {
    uint32_t name; /* offset in .shstrtab */
    uint32_t type;
    uint64_t flags;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t align;
    uint64_t entsize;
} elf_shdr_t;

static size_t elf_sym_size(void)
{
    return aot.is64 ? sizeof(Elf64_Sym) : sizeof(Elf32_Sym);
}

static void put_shdr(buf_t *elf, const elf_shdr_t *shdr)
{
    b_u32(elf, shdr->name);
    b_u32(elf, shdr->type);
    b_addr(elf, shdr->flags);
    b_addr(elf, 0); /* sh_addr */
    b_addr(elf, shdr->offset);
    b_addr(elf, shdr->size);
    b_u32(elf, shdr->link);
    b_u32(elf, shdr->info);
    b_addr(elf, shdr->align);
    b_addr(elf, shdr->entsize);
}

/* The null symbol, then one global STT_FUNC per AOT function, named after
 * its export or name-section name, else "aot_func#N". */
static void put_symbols(buf_t *symtab, buf_t *strtab)
{
    const uint8_t info = ELF32_ST_INFO(STB_GLOBAL, STT_FUNC);

    b_u8(strtab, 0);
    b_zeros(symtab, elf_sym_size());
    for (uint32_t i = 0; aot.have_funcs && i < aot.func_count; i++) {
        const char *fname = func_name(aot.import_func_count + i);
        uint32_t name_off = (uint32_t)strtab->len;
        if (fname) {
            b_str(strtab, fname);
        } else {
            char name[sizeof AOT_FUNC_PREFIX + sizeof "4294967295"];
            snprintf(name, sizeof name, AOT_FUNC_PREFIX "%" PRIu32, i);
            b_str(strtab, name);
        }
        if (aot.is64) { /* Elf64_Sym */
            b_u32(symtab, name_off);
            b_u8(symtab, info);
            b_u8(symtab, 0); /* st_other */
            b_u16(symtab, ELF_SEC_TEXT);
            b_u64(symtab, aot.func_offsets[i]);
            b_u64(symtab, 0); /* st_size */
        } else {              /* Elf32_Sym */
            b_u32(symtab, name_off);
            b_u32(symtab, (uint32_t)aot.func_offsets[i]);
            b_u32(symtab, 0); /* st_size */
            b_u8(symtab, info);
            b_u8(symtab, 0); /* st_other */
            b_u16(symtab, ELF_SEC_TEXT);
        }
    }
}

/* Writes the ELF header; returns the file offset of e_shoff, which is
 * patched once the section headers' position is known. */
static size_t put_elf_header(buf_t *elf)
{
    size_t shoff_at;
    b_put(elf, ELFMAG, SELFMAG);
    b_u8(elf, aot.is64 ? ELFCLASS64 : ELFCLASS32);
    b_u8(elf, ELFDATA2LSB);
    b_u8(elf, EV_CURRENT);
    b_zeros(elf, EI_NIDENT - EI_VERSION - 1); /* rest of e_ident */
    b_u16(elf, ET_REL);
    b_u16(elf, aot.ti.e_machine);
    b_u32(elf, EV_CURRENT);
    b_addr(elf, 0); /* e_entry */
    b_addr(elf, 0); /* e_phoff */
    shoff_at = elf->len;
    b_addr(elf, 0); /* e_shoff */
    b_u32(elf, aot.ti.e_flags);
    b_u16(elf, aot.is64 ? sizeof(Elf64_Ehdr) : sizeof(Elf32_Ehdr));
    b_u16(elf, 0); /* e_phentsize */
    b_u16(elf, 0); /* e_phnum */
    b_u16(elf, aot.is64 ? sizeof(Elf64_Shdr) : sizeof(Elf32_Shdr));
    b_u16(elf, ELF_SEC_COUNT);
    b_u16(elf, ELF_SEC_SHSTRTAB);
    return shoff_at;
}

/* Writes the code bytes as an ELF REL object with one STT_FUNC symbol per
 * AOT function, so any ELF disassembler can be used. Only possible for ELF
 * bin types. */
static bool write_elf(const char *path)
{
    enum { TEXT_FILE_ALIGN = 16, TABLE_FILE_ALIGN = 8 };
    buf_t strtab = {0};
    buf_t symtab = {0};
    buf_t shstr = {0};
    buf_t elf = {0};
    elf_shdr_t shdrs[ELF_SEC_COUNT] = {{0}};
    FILE *file;
    bool good;

    if (!aot.have_text || aot.ti.bin_type > BIN_TYPE_ELF64B) {
        return false;
    }

    b_u8(&shstr, 0);
    shdrs[ELF_SEC_TEXT].name = (uint32_t)shstr.len;
    b_str(&shstr, ".text");
    shdrs[ELF_SEC_SYMTAB].name = (uint32_t)shstr.len;
    b_str(&shstr, ".symtab");
    shdrs[ELF_SEC_STRTAB].name = (uint32_t)shstr.len;
    b_str(&shstr, ".strtab");
    shdrs[ELF_SEC_SHSTRTAB].name = (uint32_t)shstr.len;
    b_str(&shstr, ".shstrtab");
    put_symbols(&symtab, &strtab);

    size_t shoff_at = put_elf_header(&elf);
    b_pad(&elf, TEXT_FILE_ALIGN);
    shdrs[ELF_SEC_TEXT].offset = elf.len;
    b_put(&elf, aot.buf + aot.code_off, aot.code_size);
    b_pad(&elf, TABLE_FILE_ALIGN);
    shdrs[ELF_SEC_SYMTAB].offset = elf.len;
    b_put(&elf, symtab.p, symtab.len);
    shdrs[ELF_SEC_STRTAB].offset = elf.len;
    b_put(&elf, strtab.p, strtab.len);
    shdrs[ELF_SEC_SHSTRTAB].offset = elf.len;
    b_put(&elf, shstr.p, shstr.len);
    b_pad(&elf, TABLE_FILE_ALIGN);

    /* e_shoff: the section headers follow */
    for (size_t i = 0; i < aot.ptr_size; i++) {
        elf.p[shoff_at + i] = (uint8_t)(elf.len >> (CHAR_BIT * i));
    }

    shdrs[ELF_SEC_TEXT].type = SHT_PROGBITS;
    shdrs[ELF_SEC_TEXT].flags = SHF_ALLOC | SHF_EXECINSTR;
    shdrs[ELF_SEC_TEXT].size = aot.code_size;
    shdrs[ELF_SEC_TEXT].align = sizeof(uint32_t);
    shdrs[ELF_SEC_SYMTAB].type = SHT_SYMTAB;
    shdrs[ELF_SEC_SYMTAB].size = symtab.len;
    shdrs[ELF_SEC_SYMTAB].link = ELF_SEC_STRTAB;
    shdrs[ELF_SEC_SYMTAB].info = 1; /* index of the first global symbol */
    shdrs[ELF_SEC_SYMTAB].align = TABLE_FILE_ALIGN;
    shdrs[ELF_SEC_SYMTAB].entsize = elf_sym_size();
    shdrs[ELF_SEC_STRTAB].type = SHT_STRTAB;
    shdrs[ELF_SEC_STRTAB].size = strtab.len;
    shdrs[ELF_SEC_STRTAB].align = 1;
    shdrs[ELF_SEC_SHSTRTAB].type = SHT_STRTAB;
    shdrs[ELF_SEC_SHSTRTAB].size = shstr.len;
    shdrs[ELF_SEC_SHSTRTAB].align = 1;
    for (unsigned i = 0; i < ELF_SEC_COUNT; i++) {
        put_shdr(&elf, &shdrs[i]);
    }

    file = fopen(path, "wb");
    good = file && fwrite(elf.p, 1, elf.len, file) == elf.len;
    if (file) {
        good = (fclose(file) == 0) && good;
    }
    free(strtab.p);
    free(symtab.p);
    free(shstr.p);
    free(elf.p);
    return good;
}

static void disassemble(void)
{
    char path[] = "/tmp/readaot-XXXXXX";
    enum { PROG_LEN = 128, ARGS_LEN = 256, MAX_ARGS = 32 };
    char prog[PROG_LEN];
    char dflag[] = "-d";
    char args[ARGS_LEN];
    char *argv[MAX_ARGS];
    int argc = 0;
    int fdesc;
    int status;
    pid_t pid;
    const char *objdump = getenv("OBJDUMP");
    const char *extra = "";

    if (aot.ti.bin_type > BIN_TYPE_ELF64B) {
        fprintf(stderr,
                "readaot: disassembly is only supported for ELF bin types\n");
        return;
    }
    fdesc = mkstemp(path);
    if (fdesc < 0 || (close(fdesc), !write_elf(path))) {
        fprintf(stderr, "readaot: cannot write temporary ELF file %s\n", path);
        if (fdesc >= 0) {
            unlink(path);
        }
        return;
    }
    if (!objdump) {
        objdump = "llvm-objdump";
    }
    /* LLVM's disassemblers only enable the extensions they are told about */
    if (aot.ti.e_machine == EM_RISCV && !getenv("OBJDUMP_ARGS")) {
        extra = "--mattr=+m,+a,+f,+d,+c";
    } else if (aot.ti.e_machine == EM_ARM && arch_is_thumb() &&
               !getenv("OBJDUMP_ARGS")) {
        extra = "--triple=thumbv7";
    }
    if (getenv("OBJDUMP_ARGS")) {
        extra = getenv("OBJDUMP_ARGS");
    }
    /* "objdump -d extra... path", run without a shell; the extra
     * arguments are split on whitespace */
    snprintf(prog, sizeof prog, "%s", objdump);
    snprintf(args, sizeof args, "%s", extra);
    argv[argc++] = prog;
    argv[argc++] = dflag;
    /* room for the file name and the final NULL */
    for (char *arg = strtok(args, " \t"); arg && argc < MAX_ARGS - 2;
         arg = strtok(NULL, " \t")) {
        argv[argc++] = arg;
    }
    argv[argc++] = path;
    argv[argc] = NULL;
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        execvp(prog, argv);
        enum { EXIT_NOT_FOUND = 127 }; /* as the shell does */
        _exit(EXIT_NOT_FOUND);
    }
    if (pid < 0 || waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0) {
        fprintf(stderr,
                "readaot: \"%s\" failed (set OBJDUMP / OBJDUMP_ARGS to "
                "choose the disassembler)\n",
                prog);
    }
    unlink(path);
}

/* ------------------------------------------------------------------------ */
/* Section table and driver                                                 */
/* ------------------------------------------------------------------------ */

/* The header of the next section; false if it cannot be used. */
static bool read_section_header(rd_t *reader, section_t *sec)
{
    sec->hdr = reader->off;
    sec->id = rd_u32(reader, "section id");
    uint32_t size = rd_u32(reader, "section size");
    if (reader->failed) {
        return false;
    }
    if (!(sec->id < SEC_SIGNATURE || sec->id == SEC_CUSTOM)) {
        error_at(sec->hdr, "invalid section id %" PRIu32, sec->id);
        return false;
    }
    if (size > rd_left(reader)) {
        error_at(sec->hdr + 4, "section size %" PRIu32 " exceeds the file",
                 size);
        return false;
    }
    sec->start = reader->off;
    sec->end = reader->off + size;
    return true;
}

/* sections come in id order, custom sections only at the end; `last` is
 * the id of the previous one, UINT32_MAX for the first */
static void check_section_order(const section_t *sec, uint32_t last)
{
    bool first = last == UINT32_MAX;
    if ((first && sec->id != SEC_TARGET_INFO) ||
        (!first && sec->id != last + 1 && sec->id != SEC_CUSTOM)) {
        error_at(sec->hdr,
                 "%s section out of order (after %s); only custom sections may "
                 "follow a custom section",
                 section_name(sec->id),
                 first ? "file header" : section_name(last));
    }
}

static void add_section(const section_t *sec, size_t *cap)
{
    if (aot.nsecs == *cap) {
        *cap *= 2;
        section_t *secs = realloc(aot.secs, *cap * sizeof(section_t));
        if (!secs) {
            exit(2);
        }
        aot.secs = secs;
    }
    if (sec->id < REQUIRED_SECTIONS && aot.idx[sec->id] < 0) {
        aot.idx[sec->id] = (int)aot.nsecs;
    }
    aot.secs[aot.nsecs++] = *sec;
}

/* Mirrors create_sections() + the order check of load_from_sections(). */
static bool scan_sections(void)
{
    enum { INITIAL_CAP = 16 };
    rd_t reader = rd_sub(aot.buf, AOT_HEADER_SIZE, aot.size);
    uint32_t last = UINT32_MAX;
    size_t cap = INITIAL_CAP;

    aot.secs = xcalloc(cap, sizeof(section_t));
    for (int i = 0; i < REQUIRED_SECTIONS; i++) {
        aot.idx[i] = -1;
    }
    while (reader.off < aot.size) {
        section_t sec;
        rd_align(&reader, 4);
        if (reader.off >= aot.size) {
            break; /* trailing padding */
        }
        if (!read_section_header(&reader, &sec)) {
            return false;
        }
        reader.off = sec.end;
        check_section_order(&sec, last);
        last = sec.id;
        add_section(&sec, &cap);
    }
    for (int i = 0; i < REQUIRED_SECTIONS; i++) {
        if (aot.idx[i] < 0) {
            error_at(aot.size, "missing required %s section",
                     section_name((uint32_t)i));
        }
    }
    return true;
}

static void header(const char *title, const section_t *sec)
{
    printf("\n=== %s section (id %" PRIu32
           ") at 0x%zx: body [0x%zx, 0x%zx) %zu bytes ===\n",
           title, sec->id, sec->hdr, sec->start, sec->end,
           sec->end - sec->start);
}

static void run_pass(void)
{
    for (uint32_t i = 0; i < aot.nsecs; i++) {
        const section_t *sec = &aot.secs[i];
        bool show = false;

        /* sections after a missing/broken target info cannot be decoded */
        if (sec->id != SEC_TARGET_INFO && !aot.have_target) {
            return;
        }
        switch (sec->id) {
        case SEC_TARGET_INFO:
            show = opt.target;
            break;
        case SEC_INIT_DATA:
            show = opt.init;
            break;
        case SEC_TEXT:
            show = opt.text;
            break;
        case SEC_FUNCTION:
            show = opt.func;
            break;
        case SEC_EXPORT:
            show = opt.exports;
            break;
        case SEC_RELOCATION:
            show = opt.reloc;
            break;
        case SEC_CUSTOM:
            show = opt.custom;
            break;
        default:
            break;
        }
        if (g_print && show) {
            header(section_name(sec->id), sec);
        }
        switch (sec->id) {
        case SEC_TARGET_INFO:
            parse_target_info(sec, show);
            break;
        case SEC_INIT_DATA:
            parse_init_data(sec, show);
            if (!g_print) {
                aot.func_names = (const char **)xcalloc(
                    (size_t)aot.import_func_count + aot.func_count + 1,
                    sizeof(char *));
            }
            break;
        case SEC_TEXT:
            parse_text(sec, show);
            break;
        case SEC_FUNCTION:
            parse_function(sec, show);
            break;
        case SEC_EXPORT:
            parse_export(sec, show);
            break;
        case SEC_RELOCATION:
            parse_relocation(sec, show);
            break;
        case SEC_CUSTOM:
            parse_custom(sec, show);
            break;
        default:
            break;
        }
        if (g_print && show && opt.hexdump && sec->id != SEC_TEXT &&
            sec->id != SEC_CUSTOM) {
            printf("  raw bytes:\n");
            hexdump(sec->start, sec->end);
        }
    }
}

/* The whole file in a zero-terminated buffer, its length in *size; NULL if
 * it cannot be read. */
static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    long len = -1;
    if (fseek(file, 0, SEEK_END) == 0) {
        len = ftell(file);
    }
    if (len < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *buf = xcalloc((size_t)len + 1, 1);
    if (fread(buf, 1, (size_t)len, file) != (size_t)len) {
        free(buf);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)len;
    return buf;
}

static void usage(FILE *stream)
{
    fprintf(
        stream,
        "Usage: readaot [options] file.aot\n"
        "Display and validate the contents of a WAMR (AOT format version 5) "
        "file.\n\n"
        "  -a, --all           show everything (default when no section is "
        "selected)\n"
        "  -S, --sections      show the section table\n"
        "  -h, --target-info   show the target info section\n"
        "  -i, --init-data     show the init data section\n"
        "  -t, --text          show the text section (literal, code, function "
        "map)\n"
        "  -f, --function      show the function section\n"
        "  -e, --export        show the export section\n"
        "  -r, --relocation    show the relocation section\n"
        "  -c, --custom        show the custom sections\n"
        "  -x, --hexdump       also hex dump the raw bytes of the shown "
        "sections\n"
        "  -d, --disassemble   disassemble the code with $OBJDUMP (default "
        "llvm-objdump)\n"
        "                      plus $OBJDUMP_ARGS, through a temporary ELF "
        "object\n"
        "      --elf=FILE      write the code as an ELF object with function "
        "symbols\n"
        "  -q, --quiet         only validate: print diagnostics, no contents\n"
        "  -H, --help          show this help\n\n"
        "Exit status: 0 valid, 1 errors found, 2 usage or I/O error.\n");
}

/* Sets `opt` from the command line: -1 to go on, else the exit status. */
static int parse_options(int argc, char **argv)
{
    static const struct option longopts[] = {
        {"all", no_argument, NULL, 'a'},
        {"sections", no_argument, NULL, 'S'},
        {"target-info", no_argument, NULL, 'h'},
        {"init-data", no_argument, NULL, 'i'},
        {"text", no_argument, NULL, 't'},
        {"function", no_argument, NULL, 'f'},
        {"export", no_argument, NULL, 'e'},
        {"relocation", no_argument, NULL, 'r'},
        {"custom", no_argument, NULL, 'c'},
        {"hexdump", no_argument, NULL, 'x'},
        {"disassemble", no_argument, NULL, 'd'},
        {"elf", required_argument, NULL, 'E'},
        {"quiet", no_argument, NULL, 'q'},
        {"help", no_argument, NULL, 'H'},
        {NULL, 0, NULL, 0},
    };
    bool any = false;
    bool all = false;
    bool quiet = false;
    int chr;

    while ((chr = getopt_long(argc, argv, "aShitfercxdqH", longopts, NULL)) !=
           -1) {
        switch (chr) {
        case 'a':
            all = true;
            break;
        case 'S':
            opt.sections = any = true;
            break;
        case 'h':
            opt.target = any = true;
            break;
        case 'i':
            opt.init = any = true;
            break;
        case 't':
            opt.text = any = true;
            break;
        case 'f':
            opt.func = any = true;
            break;
        case 'e':
            opt.exports = any = true;
            break;
        case 'r':
            opt.reloc = any = true;
            break;
        case 'c':
            opt.custom = any = true;
            break;
        case 'x':
            opt.hexdump = true;
            break;
        case 'd':
            opt.disasm = true;
            break;
        case 'E':
            opt.elf_out = optarg;
            break;
        case 'q':
            quiet = true;
            break;
        case 'H':
            usage(stdout);
            return 0;
        default:
            usage(stderr);
            return 2;
        }
    }
    if (optind != argc - 1) {
        usage(stderr);
        return 2;
    }
    if (all || (!any && !quiet && !opt.disasm && !opt.elf_out)) {
        opt.sections = opt.target = opt.init = opt.text = opt.func =
            opt.exports = opt.reloc = opt.custom = true;
    }
    if (quiet) {
        opt.sections = opt.target = opt.init = opt.text = opt.func =
            opt.exports = opt.reloc = opt.custom = opt.hexdump = false;
    }
    return -1;
}

/* the file header: magic and version */
static bool check_header(void)
{
    if (aot.size < AOT_HEADER_SIZE ||
        memcmp(aot.buf, AOT_MAGIC, AOT_MAGIC_SIZE) != 0) {
        error_at(0, "not an AOT file (magic must be 00 61 6f 74, \"\\0aot\")");
        return false;
    }
    aot.version = load_le32(aot.buf + AOT_MAGIC_SIZE);
    if (aot.version != AOT_CURRENT_VERSION) {
        error_at(AOT_MAGIC_SIZE,
                 "AOT version %" PRIu32
                 " is not supported (WAMR-2.4.4 requires %d)",
                 aot.version, AOT_CURRENT_VERSION);
        return false;
    }
    return true;
}

static void print_section_table(void)
{
    printf("%s: AOT version %" PRIu32 ", %zu bytes, %" PRIu32 " section(s)\n",
           g_path, aot.version, aot.size, aot.nsecs);
    printf("  %-3s %-22s %-10s %-10s %s\n", "#", "section", "header", "body",
           "size");
    for (uint32_t i = 0; i < aot.nsecs; i++) {
        printf("  %-3" PRIu32 " %-22s 0x%-8zx 0x%-8zx %zu\n", i,
               section_name(aot.secs[i].id), aot.secs[i].hdr, aot.secs[i].start,
               aot.secs[i].end - aot.secs[i].start);
    }
}

int main(int argc, char **argv)
{
    int status = parse_options(argc, argv);
    if (status >= 0) {
        return status;
    }

    g_path = argv[optind];
    uint8_t *buf = read_file(g_path, &aot.size);
    if (!buf) {
        fprintf(stderr, "readaot: cannot read %s\n", g_path);
        return 2;
    }
    aot.buf = buf;
    if (!check_header()) {
        free(buf);
        return 1;
    }

    if (scan_sections()) {
        /* pass 1: validate and collect */
        g_print = false;
        run_pass();
        /* pass 2: print */
        g_report = false;
        g_print = true;
        if (opt.sections) {
            print_section_table();
        }
        run_pass();
        if (opt.elf_out && !write_elf(opt.elf_out)) {
            fprintf(stderr, "readaot: cannot write ELF file %s\n", opt.elf_out);
        }
        if (opt.disasm) {
            disassemble();
        }
    }

    if (g_errors || g_warnings) {
        fprintf(stderr, "%s: %u error(s), %u warning(s)\n", g_path, g_errors,
                g_warnings);
    }
    free(buf);
    return g_errors ? 1 : 0;
}
