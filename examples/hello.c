/*
 * hello.c: a WebAssembly module that gets printf from WAMR.
 *
 * Build and compile with wasmc (from the repository root):
 *
 *   clang --target=wasm32 -mcpu=mvp -O2 -nostdlib -Wl,--no-entry \
 *         -Wl,--allow-undefined -o hello.wasm examples/hello.c
 *   build/wasmc -o hello.aot hello.wasm
 *
 * Run with WAMR's iwasm (make iwasm), under QEMU on a PC:
 *
 *   qemu-riscv32 build/iwasm-rv32/iwasm hello.aot   # prints Hello World! 42
 *
 * clang --target=wasm32 has no C library: #include <stdio.h> fails with
 * "file not found". WAMR's libc-builtin, on by default, gives modules
 * printf, puts, strlen, memcpy and some other libc functions as imports
 * from "env", so this file declares printf itself.
 *
 * - -Wl,--allow-undefined leaves printf undefined, so it becomes the import
 *   env.printf, which WAMR links when it loads the module.
 * - The function is exported as "main", the name iwasm runs by default, but
 *   is not called main: clang renames int main(void) to __main_void, which
 *   only a libc's startup code would call.
 * - Variadic arguments go in a buffer in linear memory, which is the layout
 *   WAMR's printf expects.
 *
 * A full libc, with stdio.h unchanged, needs a WASI sysroot (wasi-libc).
 * Its printf writes through the WASI import fd_write, which needs WAMR built
 * with WAMR_BUILD_LIBC_WASI=1 (make iwasm leaves it out), and it adds much
 * more code to the module.
 */

int printf(const char *fmt, ...); /* imported from WAMR's libc-builtin */

__attribute__((export_name("main"))) int hello(void)
{
    printf("Hello World! %d\n", 42);
    return 0;
}
