/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/* fib.c - Recursive Fibonacci: calls and i32 arithmetic. */

__attribute__((export_name("fib"))) int fib(int n)
{
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}
