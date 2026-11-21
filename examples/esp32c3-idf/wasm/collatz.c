/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/* collatz.c - Collatz sequences: 64-bit integer arithmetic. */

/* Steps for `start` to reach 1 (3x+1 if odd, x/2 if even). */
__attribute__((export_name("collatz_steps"))) int collatz_steps(int start)
{
    unsigned long long x = (unsigned long long)start;
    int n = 0;
    while (x > 1) {
        x = (x & 1) ? 3 * x + 1 : x / 2;
        n++;
    }
    return n;
}

/* The start below `limit` with the longest sequence. */
__attribute__((export_name("longest_collatz"))) int longest_collatz(int limit)
{
    int best = 1;
    int best_steps = 0;
    for (int start = 1; start < limit; start++) {
        int n = collatz_steps(start);
        if (n > best_steps) {
            best_steps = n;
            best = start;
        }
    }
    return best;
}
