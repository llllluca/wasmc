/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * wasmc_alloc.c - wasmc_static_allocator(): the output block, a stack of
 * aux blocks and a stack of work blocks in one caller buffer (see wasmc.h).
 *
 *   base                                                          limit
 *   [ output block -> | aux blocks -> |     free     | <- work blocks ]
 *          out_size        aux_size                   top
 *
 * The output block never grows while an aux block is live, so the aux
 * stack can sit right on top of it; its newest block grows in place. It trusts
 * the library's use of it: the blocks of each stack are freed in LIFO order,
 * only the newest work block grows, and there is one output block. Test
 * allocators check that instead.
 */
#include "wasmc_alloc.h"
#include "wasmc/wasmc.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* the alignment of the blocks, of the output block and of the ends */
#define ARENA_ALIGN 8U

/* `size` rounded up to the alignment, or 0 if that overflows */
static uint32_t block_size(uint32_t size)
{
    const uint32_t bytes = (size + ARENA_ALIGN - 1U) & ~(ARENA_ALIGN - 1U);
    return bytes < size ? 0U : bytes;
}

static uint32_t free_bytes(const wasmc_static_arena *arena)
{
    return (uint32_t)(arena->top - arena->base) - arena->out_size -
           arena->aux_size;
}

static void note_peak(wasmc_static_arena *arena)
{
    const uint32_t used =
        (uint32_t)(arena->limit - arena->top) + arena->aux_size;
    if (used > arena->work_peak) {
        arena->work_peak = used;
    }
}

static void *static_work_alloc(void *ctx, uint32_t size)
{
    wasmc_static_arena *arena = ctx;
    const uint32_t bytes = block_size(size);
    if (!bytes || bytes > free_bytes(arena)) {
        return NULL;
    }
    arena->top -= bytes;
    note_peak(arena);
    return arena->top;
}

/* LIFO: `ptr` is the newest block, the one at the top (the signatures of
 * the callbacks are wasmc_allocator's) */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void static_work_free(void *ctx, void *ptr, uint32_t size)
{
    wasmc_static_arena *arena = ctx;
    (void)ptr;
    arena->top += block_size(size);
}

/* the newest block, at the top, grows down: its contents move with it */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void *static_work_grow(void *ctx, void *ptr, uint32_t old_size,
                              uint32_t new_size)
{
    wasmc_static_arena *arena = ctx;
    const uint32_t old_bytes = block_size(old_size);
    const uint32_t new_bytes = block_size(new_size);
    if (!new_bytes || new_bytes < old_bytes ||
        new_bytes - old_bytes > free_bytes(arena)) {
        return NULL;
    }
    arena->top -= new_bytes - old_bytes;
    memmove(arena->top, ptr, old_size);
    note_peak(arena);
    return arena->top;
}

/* the newest block, at the top, shrinks up: its contents move with it */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void *static_work_shrink(void *ctx, void *ptr, uint32_t old_size,
                                uint32_t new_size)
{
    wasmc_static_arena *arena = ctx;
    const uint32_t old_bytes = block_size(old_size);
    const uint32_t new_bytes = block_size(new_size);
    if (!new_bytes || new_bytes >= old_bytes) {
        return NULL;
    }
    uint8_t *moved = arena->top + (old_bytes - new_bytes);
    memmove(moved, ptr, new_size);
    arena->top = moved;
    return moved;
}

static void *static_aux_alloc(void *ctx, uint32_t size)
{
    wasmc_static_arena *arena = ctx;
    const uint32_t bytes = block_size(size);
    if (!bytes || bytes > free_bytes(arena)) {
        return NULL;
    }
    void *ptr = arena->base + arena->out_size + arena->aux_size;
    arena->aux_size += bytes;
    note_peak(arena);
    return ptr;
}

/* LIFO: `ptr` is the newest aux block, the one at the end */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void static_aux_free(void *ctx, void *ptr, uint32_t size)
{
    wasmc_static_arena *arena = ctx;
    (void)ptr;
    arena->aux_size -= block_size(size);
}

/* the newest aux block, at the end, grows in place */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static int static_aux_grow(void *ctx, void *ptr, uint32_t old_size,
                           uint32_t new_size)
{
    wasmc_static_arena *arena = ctx;
    (void)ptr;
    const uint32_t old_bytes = block_size(old_size);
    const uint32_t new_bytes = block_size(new_size);
    if (!new_bytes || new_bytes < old_bytes ||
        new_bytes - old_bytes > free_bytes(arena)) {
        return 0;
    }
    arena->aux_size += new_bytes - old_bytes;
    note_peak(arena);
    return 1;
}

/* one output block, always at the base: it grows and shrinks in place (no
 * aux block is live then) */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void *static_out_grow(void *ctx, void *ptr, uint32_t old_size,
                             uint32_t new_size)
{
    wasmc_static_arena *arena = ctx;
    (void)ptr;
    (void)old_size;
    const uint32_t bytes = block_size(new_size);
    if (arena->aux_size ||
        (new_size &&
         (!bytes || bytes > (uint32_t)(arena->top - arena->base)))) {
        return NULL;
    }
    arena->out_size = bytes;
    return new_size ? arena->base : NULL;
}

bool wasmc_alloc_is_static(const wasmc_allocator *mem)
{
    return mem->out_grow == static_out_grow;
}

void wasmc_static_allocator(wasmc_allocator *mem, wasmc_static_arena *arena,
                            void *buf, uint32_t len)
{
    uint8_t *base = buf;
    uint8_t *limit = buf;
    const uint32_t skip = (uint32_t)(-(uintptr_t)buf & (ARENA_ALIGN - 1U));
    if (buf && len > skip) {
        base += skip;
        limit = base + ((len - skip) & ~(ARENA_ALIGN - 1U));
    }
    arena->base = base;
    arena->limit = limit;
    arena->top = limit;
    arena->out_size = 0;
    arena->aux_size = 0;
    arena->work_peak = 0;
    mem->ctx = arena;
    mem->work_alloc = static_work_alloc;
    mem->work_free = static_work_free;
    mem->work_grow = static_work_grow;
    mem->aux_alloc = static_aux_alloc;
    mem->aux_free = static_aux_free;
    mem->aux_grow = static_aux_grow;
    mem->out_grow = static_out_grow;
    mem->work_shrink = static_work_shrink;
}
