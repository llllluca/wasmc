/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * wasmc_alloc.h - Internal: what the library knows of its own allocator,
 * wasmc_static_allocator() (wasmc_alloc.c).
 */
#ifndef WASMC_ALLOC_H_INCLUDED
#define WASMC_ALLOC_H_INCLUDED

#include "wasmc/wasmc.h"

#include <stdbool.h>

/* True if `mem` is a wasmc_static_allocator(): its output block grows in
 * place, never moves, and shares one buffer with the work blocks. */
bool wasmc_alloc_is_static(const wasmc_allocator *mem);

#endif /* WASMC_ALLOC_H_INCLUDED */
