/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/* mandel.c - ASCII Mandelbrot set: double-precision floating point (soft
 * float on the ESP32-C3) and a host function imported from the firmware. */

/* provided by the firmware (main.c, wasm_runtime_register_natives()) */
__attribute__((import_module("env"), import_name("putchar"))) void
host_putchar(int c);

/* Draws a w x h view of the set; returns the points inside it. */
__attribute__((export_name("mandelbrot"))) int mandelbrot(int w, int h,
                                                          int max_iter)
{
    /* outside: by how fast the point escapes; inside: '@' */
    static const char shades[] = " .,:;-=+*#";
    int inside = 0;
    for (int y = 0; y < h; y++) {
        double ci = -1.2 + 2.4 * y / (h - 1);
        for (int x = 0; x < w; x++) {
            double cr = -2.1 + 2.8 * x / (w - 1);
            double zr = 0.0, zi = 0.0;
            int i = 0;
            while (i < max_iter && zr * zr + zi * zi < 4.0) {
                double t = zr * zr - zi * zi + cr;
                zi = 2.0 * zr * zi + ci;
                zr = t;
                i++;
            }
            if (i == max_iter) {
                inside++;
                host_putchar('@');
            } else {
                host_putchar(shades[i < 9 ? i : 9]);
            }
        }
        host_putchar('\n');
    }
    return inside;
}
