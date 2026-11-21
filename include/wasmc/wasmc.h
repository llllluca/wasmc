/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * wasmc/wasmc.h - The wasmc library: WASM 1.0 module -> WAMR AOT file for
 * RV32IM.
 *
 * One call, wasmc_compile(), decodes, validates and compiles a module into
 * a WAMR AOT file (format version 5) with RV32IM soft-float code.
 *
 * The library allocates nothing itself: it asks the caller for memory,
 * through the callbacks of a struct wasmc_allocator, as it goes, and only
 * as much as it needs. The caller decides where the memory comes from: the
 * heap, or one static buffer (wasmc_static_allocator()). Besides it, the
 * library uses about 1.6 KB of C stack (no recursion) and has no global
 * state, so calls with different allocators may run concurrently.
 *
 * On a device, one static buffer is enough:
 *
 *   static uint8_t ram[96 * 1024];
 *   wasmc_allocator mem;
 *   wasmc_static_arena arena;
 *   wasmc_result res;
 *   wasmc_static_allocator(&mem, &arena, ram, sizeof ram);
 *   if (wasmc_compile(wasm, len, &mem, NULL, &res) == WASMC_OK)
 *       the AOT file is [res.out, res.out + res.out_size), at the start of
 *       ram; arena.work_peak + res.out_size is about the RAM it took.
 */
#ifndef WASMC_H_INCLUDED
#define WASMC_H_INCLUDED

#include "wasmc/error.h"
#include <stdint.h>

/**
 * struct wasmc_options - Compilation options.
 * @no_bounds_checks: 1: no software memory bounds checks.
 * @no_aux_stack_check: 1: no __stack_pointer overflow/underflow check.
 * @long_branches: 1: always use the long branch forms (testing).
 * @dump_ir: 1: print the IR of every function to stdout.
 * @stats: 1: print a table of the sizes of every function to stdout:
 *         measured vs actual IR sizes, and the work memory it took.
 *
 * The first three change the generated code. @dump_ir and @stats are host
 * debugging aids: they work only in builds with -DIR_ENABLE_DUMP, and are
 * silently ignored in the others.
 *
 * A zeroed struct (or a NULL pointer passed to wasmc_compile()) means the
 * defaults: every runtime check enabled, nothing printed.
 */
typedef struct {
    /* code generation */
    uint8_t no_bounds_checks;
    uint8_t no_aux_stack_check;
    uint8_t long_branches;
    /* host debugging (-DIR_ENABLE_DUMP) */
    uint8_t dump_ir;
    uint8_t stats;
} wasmc_options;

/**
 * struct wasmc_allocator - The memory a compilation asks its caller for.
 * @ctx: passed back to every callback.
 * @work_alloc: returns a block of @size bytes (@size > 0), 8-byte aligned,
 *     or NULL if there is no memory. The library frees every work block
 *     before it returns, also on failure.
 * @work_free: releases a block from @work_alloc, @size as allocated. The
 *     library frees the work blocks in LIFO order: @ptr is always the most
 *     recently allocated block still live, so a stack allocator suffices.
 * @work_grow: enlarges the most recently allocated work block still live,
 *     @ptr of @old_size bytes, to @new_size bytes (@new_size > @old_size),
 *     keeping its contents, possibly moving it. Returns the block, 8-byte
 *     aligned, or NULL if there is no memory (the block is then left as it
 *     was). From then on the block is @new_size bytes for @work_free.
 * @aux_alloc: like @work_alloc, for a second stack of blocks, independent
 *     of the work blocks (it holds the scratch of the function being
 *     built, while its IR may still grow on top of the work stack).
 * @aux_free: like @work_free, for the blocks of @aux_alloc, also freed in
 *     LIFO order among themselves.
 * @aux_grow: enlarges the most recently allocated aux block still live,
 *     @ptr of @old_size bytes, to @new_size bytes, IN PLACE: returns 1 on
 *     success (the block is then @new_size bytes for @aux_free), 0 if it
 *     cannot (the block is left as it was). An allocator that cannot grow
 *     blocks in place may always return 0.
 * @out_grow: resizes the output block, which holds the AOT file and is
 *     independent of the work blocks: from @ptr NULL and @old_size 0 it
 *     allocates the block, otherwise it resizes the block @ptr of
 *     @old_size bytes to @new_size bytes, keeping its first
 *     min(@old_size, @new_size) bytes, possibly moving it. @new_size 0
 *     frees it, and returns NULL. Otherwise it returns the block, 4-byte
 *     aligned, or NULL if there is no memory (the old block is then left as
 *     it was, and the library frees it). It is never called while an aux
 *     block is live.
 * @work_shrink: optional (NULL: blocks are never shrunk). Shrinks the most
 *     recently allocated work block still live, @ptr of @old_size bytes, to
 *     @new_size bytes (0 < @new_size < @old_size), keeping its first
 *     @new_size bytes, possibly moving it. Returns the block, 8-byte
 *     aligned, or NULL if it does not (the block is then left as it was).
 *     From then on the block is @new_size bytes for @work_free. The library
 *     gives back the unused end of a function's IR this way (its phi
 *     region is sized from an estimate) before the code generator
 *     allocates on top of it.
 *
 * The callbacks are never called concurrently by one compilation. Set every
 * field: a structure not zero-initialized must set @work_shrink too.
 */
typedef struct {
    void *ctx;
    void *(*work_alloc)(void *ctx, uint32_t size);
    void (*work_free)(void *ctx, void *ptr, uint32_t size);
    void *(*work_grow)(void *ctx, void *ptr, uint32_t old_size,
                       uint32_t new_size);
    void *(*aux_alloc)(void *ctx, uint32_t size);
    void (*aux_free)(void *ctx, void *ptr, uint32_t size);
    int (*aux_grow)(void *ctx, void *ptr, uint32_t old_size, uint32_t new_size);
    void *(*out_grow)(void *ctx, void *ptr, uint32_t old_size,
                      uint32_t new_size);
    void *(*work_shrink)(void *ctx, void *ptr, uint32_t old_size,
                         uint32_t new_size);
} wasmc_allocator;

/**
 * struct wasmc_static_arena - State of wasmc_static_allocator().
 * @base: start of the memory, 8-byte aligned.
 * @limit: end of the memory, 8-byte aligned.
 * @top: lowest byte of the work blocks, which grow down from @limit.
 * @out_size: bytes of the output block, which grows up from @base.
 * @aux_size: bytes of the aux blocks, which grow up from the end of the
 *     output block.
 * @work_peak: most bytes of work and aux blocks live at once.
 *
 * Its fields are private to wasmc_static_allocator(), but @work_peak, which
 * may be read.
 */
typedef struct {
    uint8_t *base;
    uint8_t *limit;
    uint8_t *top;
    uint32_t out_size;
    uint32_t aux_size;
    uint32_t work_peak;
} wasmc_static_arena;

/**
 * wasmc_static_allocator() - An allocator over one caller buffer.
 * @mem: set to the allocator.
 * @arena: its state, which must outlive the compilations that use @mem.
 * @buf: the memory, any alignment.
 * @len: its size in bytes.
 *
 * The output block grows up from the start of @buf, in place, and the work
 * blocks are a stack down from its end. The aux blocks are a stack up from
 * the end of the output block (the newest grows in place): the library
 * never grows the output while one is live. A compilation fails with
 * %WASMC_ERR_OUT_OF_MEMORY only if the two ends meet. Growing the newest work
 * block moves its contents down by the bytes added. The AOT file stays at the
 * start of @buf (8-byte aligned) until the next compilation that uses @mem;
 * freeing it is a no-op.
 *
 * It suits a device: no heap, no fragmentation, and the output block is
 * never copied. It allocates nothing itself and keeps no global state.
 */
void wasmc_static_allocator(wasmc_allocator *mem, wasmc_static_arena *arena,
                            void *buf, uint32_t len);

/**
 * define WASMC_NO_FUNC - &wasmc_result.func_index when the error is not
 * about a function.
 */
#define WASMC_NO_FUNC 0xFFFFFFFFu

/**
 * struct wasmc_result - Outcome of wasmc_compile().
 * @out: the AOT file, the output block of the allocator; NULL on failure.
 * @out_size: bytes of the AOT file; 0 on failure.
 * @out_cap: bytes of the output block, to free it with out_grow(); it is
 *           @out_size, unless out_grow() could not shrink the block.
 * @work_peak: the most bytes of work and aux blocks live at once, also on
 *             failure.
 * @func_index: function being compiled when the error occurred, or
 *              %WASMC_NO_FUNC if the error is not about a function.
 *
 * Filled by wasmc_compile(), also on failure.
 */
typedef struct {
    void *out;
    uint32_t out_size;
    uint32_t out_cap;
    uint32_t work_peak;
    uint32_t func_index;
} wasmc_result;

/**
 * wasmc_compile() - Compile a WASM module into a WAMR AOT file.
 * @wasm: the WASM module.
 * @wasm_len: length of @wasm in bytes.
 * @mem: the memory: where the work blocks and the AOT file go.
 * @opt: compilation options, or NULL for the defaults.
 * @res: filled with the result, also on failure; may be NULL.
 *
 * Decodes, validates and compiles the module in [@wasm, @wasm + @wasm_len)
 * into a WAMR AOT file (format version 5) with RV32IM code, in one pass.
 *
 * The work blocks hold the validator's stacks, then the data of one
 * function at a time and the data the AOT writer keeps for the whole
 * module; the aux blocks the scratch of the function being built. All of
 * them are freed before the call returns. The AOT file grows
 * in the output block, which on success is the caller's (@res->out, to free
 * with @mem->out_grow(@res->out, @res->out_cap, 0) when the allocator needs
 * it), and on failure is already freed.
 *
 * Return: %WASMC_OK, or the first %WASMC_ERR_* encountered:
 * %WASMC_ERR_OUT_OF_MEMORY if an allocation failed; %WASMC_ERR_INVALID_ARG
 * if @wasm or @mem, or one of @mem's callbacks, is NULL.
 */
wasmc_err_t wasmc_compile(const uint8_t *wasm, uint32_t wasm_len,
                          const wasmc_allocator *mem, const wasmc_options *opt,
                          wasmc_result *res);

#endif /* WASMC_H_INCLUDED */
