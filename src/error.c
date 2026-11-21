/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "wasmc/error.h"

const char *wasmc_strerror(wasmc_err_t err)
{
    switch (err) {
    case WASMC_OK:
        return "no error";
    case WASMC_ERR_INVALID_ARG:
        return "invalid argument to API call";
    case WASMC_ERR_TRUNCATED:
        return "unexpected end of input";
    case WASMC_ERR_BAD_MAGIC:
        return "bad magic number";
    case WASMC_ERR_BAD_VERSION:
        return "unsupported binary format version";
    case WASMC_ERR_BAD_SECTION_ID:
        return "unknown section id";
    case WASMC_ERR_BAD_SECTION_ORDER:
        return "sections out of order";
    case WASMC_ERR_DUPLICATE_SECTION:
        return "duplicate section";
    case WASMC_ERR_SECTION_SIZE_MISMATCH:
        return "section size does not match its contents";
    case WASMC_ERR_LEB128_OVERFLOW:
        return "LEB128 value out of range";
    case WASMC_ERR_LEB128_MALFORMED:
        return "malformed LEB128 encoding";
    case WASMC_ERR_BAD_VALTYPE:
        return "invalid value type";
    case WASMC_ERR_BAD_BLOCKTYPE:
        return "invalid block type";
    case WASMC_ERR_BAD_FUNCTYPE_TAG:
        return "function type missing 0x60 tag";
    case WASMC_ERR_TOO_MANY_RESULTS:
        return "more than one result value";
    case WASMC_ERR_BAD_LIMITS:
        return "invalid limits";
    case WASMC_ERR_BAD_ELEMTYPE:
        return "invalid table element type";
    case WASMC_ERR_BAD_IMPORT_KIND:
        return "invalid import kind";
    case WASMC_ERR_BAD_EXPORT_KIND:
        return "invalid export kind";
    case WASMC_ERR_BAD_MUTABILITY:
        return "invalid global mutability flag";
    case WASMC_ERR_BAD_UTF8:
        return "invalid UTF-8 in name";
    case WASMC_ERR_BAD_OPCODE:
        return "invalid or unsupported opcode";
    case WASMC_ERR_BAD_RESERVED_BYTE:
        return "reserved byte must be zero";
    case WASMC_ERR_TRAILING_BYTES:
        return "function body has trailing bytes";
    case WASMC_ERR_TOO_MANY_LOCALS:
        return "too many local variables declared";
    case WASMC_ERR_NOT_FOUND:
        return "not found";
    case WASMC_ERR_TYPE_INDEX:
        return "type index out of range";
    case WASMC_ERR_FUNC_INDEX:
        return "function index out of range";
    case WASMC_ERR_TABLE_INDEX:
        return "table index out of range";
    case WASMC_ERR_MEM_INDEX:
        return "memory index out of range";
    case WASMC_ERR_GLOBAL_INDEX:
        return "global index out of range";
    case WASMC_ERR_LOCAL_INDEX:
        return "local index out of range";
    case WASMC_ERR_LABEL_INDEX:
        return "label index out of range";
    case WASMC_ERR_FUNC_IS_IMPORTED:
        return "function index refers to an imported function";
    case WASMC_ERR_FUNC_CODE_COUNT_MISMATCH:
        return "function and code section counts differ";
    case WASMC_ERR_TOO_MANY_TABLES:
        return "more than one table";
    case WASMC_ERR_TOO_MANY_MEMORIES:
        return "more than one memory";
    case WASMC_ERR_TOO_MANY_TYPES:
        return "more than 65536 function types";
    case WASMC_ERR_DUPLICATE_EXPORT_NAME:
        return "duplicate export name";
    case WASMC_ERR_START_FUNC_SIGNATURE:
        return "start function must take no parameters and return no results";
    case WASMC_ERR_ELEM_TABLE_REQUIRED:
        return "element segment requires a table";
    case WASMC_ERR_DATA_MEM_REQUIRED:
        return "data segment requires a memory";
    case WASMC_ERR_BAD_CONST_EXPR:
        return "invalid constant expression";
    case WASMC_ERR_CONST_EXPR_TYPE:
        return "constant expression type mismatch";
    case WASMC_ERR_CONST_GLOBAL_NOT_IMPORTED:
        return "constant expression may only reference an imported global";
    case WASMC_ERR_CONST_GLOBAL_MUTABLE:
        return "constant expression may only reference an immutable global";
    case WASMC_ERR_TYPE_MISMATCH:
        return "type mismatch";
    case WASMC_ERR_OPD_STACK_UNDERFLOW:
        return "operand stack underflow";
    case WASMC_ERR_OPD_STACK_OVERFLOW:
        return "operand stack buffer too small";
    case WASMC_ERR_CTRL_STACK_UNDERFLOW:
        return "control stack underflow";
    case WASMC_ERR_CTRL_STACK_OVERFLOW:
        return "control stack buffer too small";
    case WASMC_ERR_UNBALANCED_ELSE:
        return "'else' without matching 'if'";
    case WASMC_ERR_MISSING_ELSE:
        return "'if' with a result value has no 'else'";
    case WASMC_ERR_GLOBAL_NOT_MUTABLE:
        return "global.set on an immutable global";
    case WASMC_ERR_MEMORY_REQUIRED:
        return "instruction requires a memory";
    case WASMC_ERR_TABLE_REQUIRED:
        return "instruction requires a table";
    case WASMC_ERR_BAD_ALIGNMENT:
        return "alignment exceeds natural alignment";
    case WASMC_ERR_END_MISMATCH:
        return "function body result does not match its type";
    case WASMC_ERR_IR_SLOTS:
        return "IR slot capacity exceeded (function too big)";
    case WASMC_ERR_IR_BLOCKS:
        return "IR block capacity exceeded";
    case WASMC_ERR_IR_EDGES:
        return "IR CFG edge capacity exceeded";
    case WASMC_ERR_IR_SCRATCH:
        return "IR scratch area exhausted";
    case WASMC_ERR_IR_STATE:
        return "IR builder misuse";
    case WASMC_ERR_IR_MEM:
        return "IR buffer too small or misaligned";
    case WASMC_ERR_OUT_OF_MEMORY:
        return "out of memory";
    case WASMC_ERR_CODE_TOO_LARGE:
        return "code too large for a RV32 jump";
    case WASMC_ERR_UNSUPPORTED:
        return "not supported yet";
    case WASMC_ERR_COUNT:
        break;
    }
    return "unknown error";
}
