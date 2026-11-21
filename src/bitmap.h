/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * bitmap.h - Sets of small integers stored as bit arrays.
 */
#ifndef BITMAP_H_INCLUDED
#define BITMAP_H_INCLUDED

#include <limits.h>
#include <stdint.h>

/* bytes needed for `nbits` bits */
static inline uint32_t bitmap_bytes(uint32_t nbits)
{
    return (nbits + CHAR_BIT - 1U) / CHAR_BIT;
}

static inline int bitmap_test(const uint8_t *map, uint32_t bit)
{
    return (map[bit / CHAR_BIT] & (1U << (bit % CHAR_BIT))) != 0;
}

static inline void bitmap_set(uint8_t *map, uint32_t bit)
{
    map[bit / CHAR_BIT] |= (uint8_t)(1U << (bit % CHAR_BIT));
}

#endif /* BITMAP_H_INCLUDED */
