/*
 * fib.c: a WebAssembly module that uses no libc.
 *
 * Build and compile with wasmc (from the repository root):
 *
 *   clang --target=wasm32 -mcpu=mvp -O2 -nostdlib -Wl,--no-entry \
 *         -o fib.wasm examples/fib.c
 *   build/wasmc -o fib.aot fib.wasm
 *
 * Run with WAMR's iwasm (make iwasm), under QEMU on a PC:
 *
 *   qemu-riscv32 build/iwasm-rv32/iwasm -f fib fib.aot 20   # prints 0x1a6d:i32
 *
 * - -mcpu=mvp: wasmc accepts WebAssembly 1.0 (the MVP) only, and recent
 *   clang versions enable some later features by default.
 * - -nostdlib: clang --target=wasm32 has no C library, only the
 *   freestanding headers (stdint.h, stddef.h, stdarg.h, ...). This module
 *   only computes, so it needs none.
 * - -Wl,--no-entry: there is no _start; the module is a library of exported
 *   functions.
 * - export_name("fib") exports the function under that name, which is the
 *   name iwasm's -f option looks up.
 */

__attribute__((export_name("fib"))) int fib(int n)
{
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}
