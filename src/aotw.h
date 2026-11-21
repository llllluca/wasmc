/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * aotw.h - WAMR AOT file writer (format version 5, aotfmt/aotfmt.tex).
 *
 * The file is written in one pass into an output buffer that grows through
 * the caller's allocator (wasmc_allocator.out_grow):
 *
 *   aotw_begin()         header, target info, init data; opens the text section
 *   aotw_func_begin()    once per defined function, in order, then the code
 *   aotw_emit32() ...    of the function (and aotw_reloc() for its relocations)
 *   aotw_end()           closes the text section; function, export and
 *                        relocation sections
 *
 * Relocations are recorded while the code is emitted but belong to the last
 * section: they are kept temporarily at the end of the output buffer,
 * growing down, and moved into place by aotw_end(). Errors are sticky
 * (w->err); every call is a no-op after the first error.
 *
 * The buffer can move when it grows, so the writer only keeps offsets into
 * it; the relocation records move to its new end. At aotw_end() it is
 * shrunk to the file.
 */
#ifndef AOTW_H_INCLUDED
#define AOTW_H_INCLUDED

#include "wamr_abi.h"
#include "wasm_decode.h"
#include "wasmc/wasmc.h"
#include <stdint.h>

/**
 * struct aotw_t - State of the AOT file writer.
 * @m: the module, validated.
 * @mem: the allocator; its out_grow() grows @buf.
 * @buf: the output buffer, NULL until the first byte is written.
 * @cap: bytes of @buf.
 * @pos: next free byte for the sections (in a sizing pass, it counts the
 *     code that is not written).
 * @tail: the staged relocation records live in [@tail, @cap & ~3), pushed
 *     down from the top, the oldest at the top.
 * @err: the first error, sticky: every call is a no-op after it.
 * @dry: 1 in a sizing pass (aotw_dry_begin()): code is counted, not written.
 * @dry_start: @pos at aotw_dry_begin(), restored by aotw_dry_end().
 * @text_size_pos: offset of the text section's size field.
 * @code_start: file offset of code[0], the base of every code offset.
 * @func_off: per defined function, its fix-up chain until aotw_func_begin()
 *     binds it, then its code offset. In the caller's module data.
 * @canon_type: per type, the smallest index of an identical type
 *     (call_indirect compares type indices). In the caller's module data.
 *     16 bits suffice: the decoder rejects more than 65536 types.
 * @global_off: per global (imported, then defined), the offset of its value
 *     in the module instance. In the caller's module data.
 * @nglobals: globals, imported + defined.
 * @memory_count: memories as written in the init data (>= 1).
 * @init_pages_zero: 1: memory 0 starts with 0 pages.
 * @has_mem_grow: 1: some function contains memory.grow.
 * @has_mem_size: 1: some function contains memory.size.
 * @mem_bound_min: the smallest last valid address memory 0 can have when it
 *     is not empty (its size never decreases): 65535 or more with 64 KiB
 *     pages, possibly less for a shrunk memory, 0 for an always empty one.
 * @aux_stack_global: the __stack_pointer global, %AOT_NO_INDEX if none.
 * @table0_off: offset of table 0's instance in the module instance.
 * @ndefined: defined functions.
 * @nimported_funcs: imported functions.
 * @next_func: defined functions begun so far.
 * @sym_index: per runtime symbol (WAMR_SYM_*), its index in the AOT symbol
 *     table, 0 if not used yet (symbol 0 is the group name ".rela.text").
 * @sym_order: the runtime symbols used, in first-use order.
 * @nsyms: runtime symbols used.
 * @nrelocs: relocation records staged.
 *
 * The fields from @nglobals to @next_func are module facts for the code
 * generator, set by aotw_begin(); the others are private to the writer.
 * The writer keeps offsets, never pointers, into @buf, which may move when
 * it grows.
 */
typedef struct {
    const wdec_module_t *m;
    const wasmc_allocator *mem;
    uint8_t *buf;
    uint32_t cap;
    uint32_t pos;
    uint32_t tail;
    wasmc_err_t err;
    uint8_t dry;
    uint32_t dry_start;

    uint32_t text_size_pos;
    uint32_t code_start;

    /* module data, owned by the caller (aotw_module_data_size()) */
    uint32_t *func_off;
    uint16_t *canon_type;
    uint32_t *global_off;

    /* module facts for the code generator */
    uint32_t nglobals;
    uint32_t memory_count;
    uint8_t init_pages_zero;
    uint8_t has_mem_grow;
    uint8_t has_mem_size;
    uint32_t mem_bound_min;
    uint32_t aux_stack_global;
    uint32_t table0_off;
    uint32_t ndefined;
    uint32_t nimported_funcs;
    uint32_t next_func;

    /* relocation symbols */
    uint8_t sym_index[WAMR_SYM__COUNT];
    uint8_t sym_order[WAMR_SYM__COUNT];
    uint32_t nsyms;
    uint32_t nrelocs;
} aotw_t;

/**
 * aotw_module_data_size() - Bytes of module data the writer needs.
 * @mod: the module.
 *
 * The module data holds &aotw_t.func_off, &aotw_t.global_off and
 * &aotw_t.canon_type, in this order.
 *
 * Return: its size in bytes, rounded up to a multiple of 8.
 */
uint32_t aotw_module_data_size(const wdec_module_t *mod);

/**
 * aotw_begin() - Start the AOT file.
 * @writer: the writer, initialized here.
 * @mod: the module, validated.
 * @mem: the allocator: its out_grow() allocates the output buffer and grows
 *     it as needed.
 * @module_data: aotw_module_data_size() bytes, 4-byte aligned, valid until
 *     aotw_end(); NULL if that size is 0 (no functions, globals or types).
 *
 * Writes the file header, the target info and the init data, sets the
 * module facts of @writer, and opens the text section.
 *
 * The writer owns the output buffer until aotw_end() succeeds: the caller
 * then takes it (&aotw_t.buf, &aotw_t.cap). On any failure, also of this
 * call, the caller gives it back with aotw_abort().
 *
 * Return: %WASMC_OK, or the error, also left in &aotw_t.err.
 */
wasmc_err_t aotw_begin(aotw_t *writer, const wdec_module_t *mod,
                       const wasmc_allocator *mem, void *module_data);

/**
 * aotw_reserve() - Make room for @n more bytes of output, if memory allows.
 * @writer: the writer.
 * @n: the bytes expected.
 *
 * A hint: grows the output buffer now (by the usual step, or to fit @n)
 * instead of while the next function's code is written. If out_grow()
 * fails nothing changes and the buffer grows on demand as before. Called
 * before a function's IR is built, so that a move of the buffer (a heap's
 * realloc copies it: old and new copies live at once) happens while little
 * work memory is live, not at the end of a large function.
 */
void aotw_reserve(aotw_t *writer, uint32_t n);

/**
 * aotw_func_begin() - Start the code of the next defined function.
 * @writer: the writer.
 *
 * Aligns the code to 4 bytes and binds the function's chain to it, which
 * patches the calls to it emitted so far (aotw_call()). One call per
 * defined function, in order: one more sets %WASMC_ERR_INVALID_ARG.
 */
void aotw_func_begin(aotw_t *writer);

/*
 * Fix-up chains: a jump target whose code offset is
 * not known yet keeps, in a u32, the code offset of the last site (jal or
 * branch) emitted to it; each such placeholder stores in its own immediate
 * the distance to the previous site (0 = first). aotw_bind() patches them
 * all. Once bound, the u32 is the code offset of the target.
 */

/**
 * define AOTW_CHAIN_EMPTY - Chain of a target not bound yet, with no site.
 */
#define AOTW_CHAIN_EMPTY 0xFFFFFFFFu

/**
 * define AOTW_CHAIN_PENDING - Bit set in a chain whose target is not bound
 * yet: the low bits are the code offset of the last site.
 */
#define AOTW_CHAIN_PENDING 0x80000000u

/**
 * aotw_chain_init() - Make a chain empty: no target, no site.
 * @chain: the chain.
 */
static inline void aotw_chain_init(uint32_t *chain)
{
    *chain = AOTW_CHAIN_EMPTY;
}

/**
 * aotw_jump() - Emit `jal dst, <target of chain>`.
 * @writer: the writer.
 * @chain: the target's chain.
 * @dst: the link register (x0 for a plain jump).
 *
 * If the target is not bound yet, the jal is a placeholder added to
 * @chain. The target, or the previous site of @chain, must be within the
 * jal range (+-1 MiB), else %WASMC_ERR_CODE_TOO_LARGE.
 */
void aotw_jump(aotw_t *writer, uint32_t *chain, unsigned dst);

/**
 * aotw_branch() - Emit a B-type branch to the target of a chain.
 * @writer: the writer.
 * @chain: the target's chain.
 * @insn: the branch, condition and registers encoded, any offset.
 *
 * Like aotw_jump(), but the target, and the previous site of @chain, must
 * be within +-4 KiB, else %WASMC_ERR_CODE_TOO_LARGE. A chain may mix jal
 * and branch sites.
 */
void aotw_branch(aotw_t *writer, uint32_t *chain, uint32_t insn);

/**
 * aotw_bind() - Bind a target to the current code position.
 * @writer: the writer.
 * @chain: the target's chain; it becomes the code offset of the target.
 *
 * Patches every site of @chain; one out of its range sets
 * %WASMC_ERR_CODE_TOO_LARGE.
 */
void aotw_bind(aotw_t *writer, uint32_t *chain);

/**
 * aotw_dry_begin() - Start a sizing pass.
 * @writer: the writer.
 *
 * Until aotw_dry_end(), nothing is written; code is only counted. Jumps,
 * branches and binds update their chains as usual (pending / bound) without
 * patching anything, so chains used in the pass must be initialized again
 * before the real one. Calls and relocations leave no trace.
 */
void aotw_dry_begin(aotw_t *writer);

/**
 * aotw_dry_end() - End a sizing pass.
 * @writer: the writer.
 *
 * Rewinds the output position to where aotw_dry_begin() found it.
 *
 * Return: the bytes of code emitted in the pass.
 */
uint32_t aotw_dry_end(aotw_t *writer);

/**
 * aotw_call() - Emit `jal ra, <defined function def_idx>`.
 * @writer: the writer.
 * @def_idx: the index of the function among the defined ones; out of range
 *     sets %WASMC_ERR_INVALID_ARG.
 *
 * A call to a function not begun yet is patched by its aotw_func_begin().
 */
void aotw_call(aotw_t *writer, uint32_t def_idx);

/**
 * aotw_read32() - Read an instruction already emitted.
 * @writer: the writer.
 * @code_off: its code offset.
 *
 * Return: the instruction; 0 in a sizing pass.
 */
uint32_t aotw_read32(const aotw_t *writer, uint32_t code_off);

/**
 * aotw_patch32() - Rewrite an instruction already emitted.
 * @writer: the writer.
 * @code_off: its code offset.
 * @insn: the new instruction.
 *
 * A no-op in a sizing pass or after an error.
 */
void aotw_patch32(const aotw_t *writer, uint32_t code_off, uint32_t insn);

/**
 * aotw_emit32() - Append one 32-bit instruction.
 * @writer: the writer.
 * @insn: the instruction.
 */
void aotw_emit32(aotw_t *writer, uint32_t insn);

/**
 * aotw_code_pos() - The code offset of the next instruction.
 * @writer: the writer.
 *
 * Return: its offset from code[0].
 */
static inline uint32_t aotw_code_pos(const aotw_t *writer)
{
    return writer->pos - writer->code_start;
}

/**
 * aotw_reloc() - Record a relocation.
 * @writer: the writer.
 * @code_off: the code offset of the instruction(s) to relocate.
 * @type: the relocation type (R_RISCV_*).
 * @addend: the addend.
 * @sym: the runtime symbol (WAMR_SYM_*); out of range sets
 *     %WASMC_ERR_INVALID_ARG.
 *
 * The record is staged at the tail of the output buffer until aotw_end()
 * writes the relocation section. A no-op in a sizing pass.
 */
void aotw_reloc(aotw_t *writer, uint32_t code_off, uint32_t type,
                int32_t addend, unsigned sym);

/**
 * aotw_end() - Finish the AOT file.
 * @writer: the writer.
 * @out_size: set to the bytes of the file, on success.
 *
 * Closes the text section and writes the function, export and relocation
 * sections. Then shrinks the output buffer to the file (&aotw_t.cap ==
 * *@out_size), if out_grow() can.
 *
 * Return: %WASMC_OK; %WASMC_ERR_INVALID_ARG if not every defined function
 * was begun; %WASMC_ERR_OUT_OF_MEMORY if the buffer could not grow; or the
 * first error of the writer.
 */
wasmc_err_t aotw_end(aotw_t *writer, uint32_t *out_size);

/**
 * aotw_abort() - Give the output buffer back, after an error.
 * @writer: the writer, after aotw_begin(), also if it failed.
 *
 * Frees the output buffer (out_grow() to 0 bytes), if one was allocated.
 * The writer must not be used again.
 */
void aotw_abort(aotw_t *writer);

#endif /* AOTW_H_INCLUDED */
