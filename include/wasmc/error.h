/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef WASMC_ERROR_H_INCLUDED
#define WASMC_ERROR_H_INCLUDED

/**
 * enum wasmc_err_t - Status and error codes returned by every wasmc module
 * (decoder/validator, IR builder, compiler). %WASMC_OK is 0, so `if (err)`
 * tests for failure.
 * @WASMC_OK: Success.
 * @WASMC_ERR_INVALID_ARG: Invalid argument to a wasmc API call.
 * @WASMC_ERR_TRUNCATED: A read ran past the end of the input buffer.
 * @WASMC_ERR_BAD_MAGIC: The module does not start with the "\0asm" magic
 *                       number.
 * @WASMC_ERR_BAD_VERSION: Unsupported binary format version.
 * @WASMC_ERR_BAD_SECTION_ID: A section id is not one of the known 0-11
 *                            values.
 * @WASMC_ERR_BAD_SECTION_ORDER: Known sections are not in strictly
 *                               increasing id order.
 * @WASMC_ERR_DUPLICATE_SECTION: A non-custom section id appears more
 *                               than once.
 * @WASMC_ERR_SECTION_SIZE_MISMATCH: A section's declared size does not
 *                                   match the size of its actual
 *                                   decoded content.
 * @WASMC_ERR_LEB128_OVERFLOW: A LEB128-encoded integer used more bytes,
 *                             or encoded a larger value, than its
 *                             target type allows.
 * @WASMC_ERR_LEB128_MALFORMED: A LEB128 encoding is malformed, e.g.
 *                              missing its terminating byte.
 * @WASMC_ERR_BAD_VALTYPE: A byte where a value type was expected is not
 *                         one of i32/i64/f32/f64.
 * @WASMC_ERR_BAD_BLOCKTYPE: A block/loop/if's blocktype byte is neither
 *                           0x40 (empty) nor a valid value type.
 * @WASMC_ERR_BAD_FUNCTYPE_TAG: A function type is not introduced by the
 *                              0x60 tag.
 * @WASMC_ERR_TOO_MANY_RESULTS: More than one result value (WebAssembly
 *                              1.0 allows at most one).
 * @WASMC_ERR_BAD_LIMITS: A limits pair has max < min, or a value outside
 *                        its allowed range.
 * @WASMC_ERR_BAD_ELEMTYPE: A table's element type is not funcref.
 * @WASMC_ERR_BAD_IMPORT_KIND: An import's kind byte is not one of
 *                             func/table/mem/global.
 * @WASMC_ERR_BAD_EXPORT_KIND: An export's kind byte is not one of
 *                             func/table/mem/global.
 * @WASMC_ERR_BAD_MUTABILITY: A global's mutability byte is neither 0
 *                            (const) nor 1 (var).
 * @WASMC_ERR_BAD_UTF8: A name (import, export, or custom section) is not
 *                      valid UTF-8.
 * @WASMC_ERR_BAD_OPCODE: An unrecognized or reserved instruction opcode.
 * @WASMC_ERR_BAD_RESERVED_BYTE: A reserved byte (call_indirect,
 *                               memory.size, memory.grow) is non-zero.
 * @WASMC_ERR_TRAILING_BYTES: A function body has bytes left over after
 *                            its closing 'end'.
 * @WASMC_ERR_TOO_MANY_LOCALS: A function declares 2^32 or more local
 *                             variables.
 * @WASMC_ERR_NOT_FOUND: A lookup (e.g. wdec_find_export()) found no
 *                       match.
 * @WASMC_ERR_TYPE_INDEX: A type index is out of range.
 * @WASMC_ERR_FUNC_INDEX: A function index is out of range.
 * @WASMC_ERR_TABLE_INDEX: A table index is out of range.
 * @WASMC_ERR_MEM_INDEX: A memory index is out of range.
 * @WASMC_ERR_GLOBAL_INDEX: A global index is out of range.
 * @WASMC_ERR_LOCAL_INDEX: A local index is out of range.
 * @WASMC_ERR_LABEL_INDEX: A branch label index is out of range.
 * @WASMC_ERR_FUNC_IS_IMPORTED: wdec_get_func() was called on an imported
 *                              (bodyless) function index.
 * @WASMC_ERR_FUNC_CODE_COUNT_MISMATCH: The function and code sections
 *                                      declare different entry counts.
 * @WASMC_ERR_TOO_MANY_TABLES: More than one table in total (imported
 *                             plus defined).
 * @WASMC_ERR_TOO_MANY_MEMORIES: More than one memory in total (imported
 *                               plus defined).
 * @WASMC_ERR_TOO_MANY_TYPES: More than 65536 function types, an
 *                            implementation limit: type indices are 16-bit
 *                            in the compiler and in WAMR's AOT format.
 * @WASMC_ERR_DUPLICATE_EXPORT_NAME: Two exports share the same name.
 * @WASMC_ERR_START_FUNC_SIGNATURE: The start function does not have type
 *                                  [] -> [].
 * @WASMC_ERR_ELEM_TABLE_REQUIRED: An element segment exists but the
 *                                 module has no table.
 * @WASMC_ERR_DATA_MEM_REQUIRED: A data segment exists but the module has
 *                               no memory.
 * @WASMC_ERR_BAD_CONST_EXPR: A constant expression is not one of the
 *                            allowed forms.
 * @WASMC_ERR_CONST_EXPR_TYPE: A constant expression's type does not
 *                             match what is expected of it.
 * @WASMC_ERR_CONST_GLOBAL_NOT_IMPORTED: A constant expression's
 *                                       global.get refers to a
 *                                       non-imported global.
 * @WASMC_ERR_CONST_GLOBAL_MUTABLE: A constant expression's global.get
 *                                  refers to a mutable global.
 * @WASMC_ERR_TYPE_MISMATCH: An instruction's operand types do not match
 *                           what it expects.
 * @WASMC_ERR_OPD_STACK_UNDERFLOW: An instruction pops an operand that is
 *                                 not there.
 * @WASMC_ERR_OPD_STACK_OVERFLOW: The caller-supplied operand-stack
 *                                buffer (see wdec_validator_init()) is
 *                                too small for this function.
 * @WASMC_ERR_CTRL_STACK_UNDERFLOW: A control instruction (e.g. 'end')
 *                                  has no matching frame to act on.
 * @WASMC_ERR_CTRL_STACK_OVERFLOW: The caller-supplied control-stack
 *                                 buffer (see wdec_validator_init()) is
 *                                 too small for this function's nesting
 *                                 depth.
 * @WASMC_ERR_UNBALANCED_ELSE: An 'else' appears without a matching 'if'.
 * @WASMC_ERR_MISSING_ELSE: An 'if' with a non-empty result type has no
 *                          'else' branch.
 * @WASMC_ERR_GLOBAL_NOT_MUTABLE: global.set targets an immutable global.
 * @WASMC_ERR_MEMORY_REQUIRED: An instruction needs a memory but the
 *                             module has none.
 * @WASMC_ERR_TABLE_REQUIRED: An instruction needs a table but the module
 *                            has none.
 * @WASMC_ERR_BAD_ALIGNMENT: A memory instruction's alignment hint
 *                           exceeds its natural alignment.
 * @WASMC_ERR_END_MISMATCH: A function body's result does not match its
 *                          declared type at its final 'end'.
 * @WASMC_ERR_IR_SLOTS: IR instruction/phi slot capacity exceeded.
 * @WASMC_ERR_IR_BLOCKS: IR basic-block capacity exceeded.
 * @WASMC_ERR_IR_EDGES: IR CFG edge capacity exceeded.
 * @WASMC_ERR_IR_SCRATCH: IR scratch area exhausted.
 * @WASMC_ERR_IR_STATE: IR builder API misuse (no open block, emit after
 *                      terminator, unterminated block, ...).
 * @WASMC_ERR_IR_MEM: ir_init(): buffer too small or misaligned.
 * @WASMC_ERR_OUT_OF_MEMORY: the caller's allocator (struct wasmc_allocator)
 *                           returned NULL.
 * @WASMC_ERR_CODE_TOO_LARGE: generated code out of the range of a RV32 jump.
 * @WASMC_ERR_UNSUPPORTED: a valid construct the compiler does not handle yet.
 */
typedef enum {
    WASMC_OK = 0,

    /* API misuse */
    WASMC_ERR_INVALID_ARG,

    /* structural / binary-format errors */
    WASMC_ERR_TRUNCATED,
    WASMC_ERR_BAD_MAGIC,
    WASMC_ERR_BAD_VERSION,
    WASMC_ERR_BAD_SECTION_ID,
    WASMC_ERR_BAD_SECTION_ORDER,
    WASMC_ERR_DUPLICATE_SECTION,
    WASMC_ERR_SECTION_SIZE_MISMATCH,
    WASMC_ERR_LEB128_OVERFLOW,
    WASMC_ERR_LEB128_MALFORMED,
    WASMC_ERR_BAD_VALTYPE,
    WASMC_ERR_BAD_BLOCKTYPE,
    WASMC_ERR_BAD_FUNCTYPE_TAG,
    WASMC_ERR_TOO_MANY_RESULTS,
    WASMC_ERR_BAD_LIMITS,
    WASMC_ERR_BAD_ELEMTYPE,
    WASMC_ERR_BAD_IMPORT_KIND,
    WASMC_ERR_BAD_EXPORT_KIND,
    WASMC_ERR_BAD_MUTABILITY,
    WASMC_ERR_BAD_UTF8,
    WASMC_ERR_BAD_OPCODE,
    WASMC_ERR_BAD_RESERVED_BYTE,
    WASMC_ERR_TRAILING_BYTES,
    WASMC_ERR_TOO_MANY_LOCALS,
    WASMC_ERR_NOT_FOUND,

    /* index / reference errors */
    WASMC_ERR_TYPE_INDEX,
    WASMC_ERR_FUNC_INDEX,
    WASMC_ERR_TABLE_INDEX,
    WASMC_ERR_MEM_INDEX,
    WASMC_ERR_GLOBAL_INDEX,
    WASMC_ERR_LOCAL_INDEX,
    WASMC_ERR_LABEL_INDEX,
    WASMC_ERR_FUNC_IS_IMPORTED,

    /* module-level semantic errors */
    WASMC_ERR_FUNC_CODE_COUNT_MISMATCH,
    WASMC_ERR_TOO_MANY_TABLES,
    WASMC_ERR_TOO_MANY_MEMORIES,
    WASMC_ERR_TOO_MANY_TYPES,
    WASMC_ERR_DUPLICATE_EXPORT_NAME,
    WASMC_ERR_START_FUNC_SIGNATURE,
    WASMC_ERR_ELEM_TABLE_REQUIRED,
    WASMC_ERR_DATA_MEM_REQUIRED,

    /* constant expression errors */
    WASMC_ERR_BAD_CONST_EXPR,
    WASMC_ERR_CONST_EXPR_TYPE,
    WASMC_ERR_CONST_GLOBAL_NOT_IMPORTED,
    WASMC_ERR_CONST_GLOBAL_MUTABLE,

    /* function-body (stack machine) validation errors, spec section 7.3 */
    WASMC_ERR_TYPE_MISMATCH,
    WASMC_ERR_OPD_STACK_UNDERFLOW,
    WASMC_ERR_OPD_STACK_OVERFLOW,
    WASMC_ERR_CTRL_STACK_UNDERFLOW,
    WASMC_ERR_CTRL_STACK_OVERFLOW,
    WASMC_ERR_UNBALANCED_ELSE,
    WASMC_ERR_MISSING_ELSE,
    WASMC_ERR_GLOBAL_NOT_MUTABLE,
    WASMC_ERR_MEMORY_REQUIRED,
    WASMC_ERR_TABLE_REQUIRED,
    WASMC_ERR_BAD_ALIGNMENT,
    WASMC_ERR_END_MISMATCH,

    /* IR builder errors (sticky in ir_func.err) */
    WASMC_ERR_IR_SLOTS,
    WASMC_ERR_IR_BLOCKS,
    WASMC_ERR_IR_EDGES,
    WASMC_ERR_IR_SCRATCH,
    WASMC_ERR_IR_STATE,
    WASMC_ERR_IR_MEM,

    /* compiler / output errors */
    WASMC_ERR_OUT_OF_MEMORY,
    WASMC_ERR_CODE_TOO_LARGE,
    WASMC_ERR_UNSUPPORTED,

    WASMC_ERR_COUNT /* number of codes; not an error */
} wasmc_err_t;

/**
 * wasmc_strerror() - Look up a short, human-readable description of an
 * error code.
 * @err: an error code returned by any wasmc function.
 *
 * Backed by a static string table; performs no allocation.
 *
 * Return: A NUL-terminated string, never NULL, valid for the lifetime
 * of the program.
 */
const char *wasmc_strerror(wasmc_err_t err);

#endif /* WASMC_ERROR_H_INCLUDED */
