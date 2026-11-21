/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/* sieve.c - Sieve of Eratosthenes: loops, loads and stores in linear
 * memory. */

#define MAX_N 50000

static unsigned char composite[MAX_N];

/* Number of primes below n (n <= MAX_N). */
__attribute__((export_name("count_primes"))) int count_primes(int n)
{
    if (n > MAX_N) {
        n = MAX_N;
    }
    for (int i = 0; i < n; i++) {
        composite[i] = 0;
    }
    int count = 0;
    for (int i = 2; i < n; i++) {
        if (composite[i]) {
            continue;
        }
        count++;
        for (int j = i * 2; j < n; j += i) {
            composite[j] = 1;
        }
    }
    return count;
}
