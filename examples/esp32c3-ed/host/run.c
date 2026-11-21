/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * run.c - Runs the editor's module, ed.wasm, on a PC. Standard input and
 * output stand for the serial line, and the import functions are plain POSIX
 * calls.
 *
 * Built with -DRUN_AOT for riscv32 (run under qemu-riscv32) it compiles the
 * module with wasmc_compile() and runs it with the WAMR AOT runtime, as the
 * ESP32-C3 will. Built without, for the PC, it runs the same module with the
 * WAMR interpreter, as a reference that does not depend on wasmc.
 *
 * Usage: run module.wasm [arguments of the editor]
 */
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "wasm_export.h"
#ifdef RUN_AOT
#include "wasmc/error.h"
#include "wasmc/wasmc.h"
#endif

#define WASM_STACK_SIZE (16 * 1024)
#define EXC_THROW       "app: longjmp"
#define EXC_EXIT        "app: exit"

static wasm_module_inst_t g_inst;
static char **g_args;
static int g_nargs;
static int g_exit_code;

static int32_t sys_read(wasm_exec_env_t env, int32_t fd, void *buf, uint32_t n)
{
    (void)env;
    return (int32_t)read(fd, buf, n);
}

static int32_t sys_write(wasm_exec_env_t env, int32_t fd, const void *buf,
                         uint32_t n)
{
    (void)env;
    return (int32_t)write(fd, buf, n);
}

static int32_t sys_open(wasm_exec_env_t env, const char *path, int32_t mode)
{
    (void)env;
    return open(path, mode);
}

static int32_t sys_creat(wasm_exec_env_t env, const char *path, int32_t mode)
{
    (void)env;
    return creat(path, (mode_t)mode);
}

static int32_t sys_close(wasm_exec_env_t env, int32_t fd)
{
    (void)env;
    return fd > 2 ? close(fd) : -1;
}

static int32_t sys_lseek(wasm_exec_env_t env, int32_t fd, int32_t off,
                         int32_t whence)
{
    (void)env;
    return (int32_t)lseek(fd, off, whence);
}

static int32_t sys_unlink(wasm_exec_env_t env, const char *path)
{
    (void)env;
    return unlink(path);
}

static int32_t sys_getpid(wasm_exec_env_t env)
{
    (void)env;
    return (int32_t)getpid();
}

/* argument @i, argv[0] included, copied to @buf; -1 if there is none */
static int32_t sys_arg(wasm_exec_env_t env, int32_t i, char *buf, uint32_t n)
{
    (void)env;
    if (i < 0 || i >= g_nargs || strlen(g_args[i]) >= n) {
        return -1;
    }
    strcpy(buf, g_args[i]);
    return (int32_t)strlen(g_args[i]);
}

static void sys_throw(wasm_exec_env_t env)
{
    (void)env;
    wasm_runtime_set_exception(g_inst, EXC_THROW);
}

static void sys_exit(wasm_exec_env_t env, int32_t code)
{
    (void)env;
    g_exit_code = code;
    wasm_runtime_set_exception(g_inst, EXC_EXIT);
}

static NativeSymbol g_natives[] = {
    {"sys_read", (void *)sys_read, "(i*~)i", NULL},
    {"sys_write", (void *)sys_write, "(i*~)i", NULL},
    {"sys_open", (void *)sys_open, "($i)i", NULL},
    {"sys_creat", (void *)sys_creat, "($i)i", NULL},
    {"sys_close", (void *)sys_close, "(i)i", NULL},
    {"sys_lseek", (void *)sys_lseek, "(iii)i", NULL},
    {"sys_unlink", (void *)sys_unlink, "($)i", NULL},
    {"sys_getpid", (void *)sys_getpid, "()i", NULL},
    {"sys_arg", (void *)sys_arg, "(i*~)i", NULL},
    {"sys_throw", (void *)sys_throw, "()", NULL},
    {"sys_exit", (void *)sys_exit, "(i)", NULL},
};

static uint8_t *read_file(const char *path, uint32_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    uint8_t *buf = malloc((size_t)size);
    if (buf && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = (uint32_t)size;
    return buf;
}

/* The module WAMR loads: the AOT file wasmc makes, or the .wasm as it is. */
static uint8_t *prepare_module(uint8_t *wasm, uint32_t wasm_len, uint32_t *len)
{
#ifdef RUN_AOT
    static uint8_t ram[512 * 1024];
    wasmc_allocator mem;
    wasmc_static_arena arena;
    wasmc_result res;

    wasmc_static_allocator(&mem, &arena, ram, sizeof ram);
    wasmc_err_t err = wasmc_compile(wasm, wasm_len, &mem, NULL, &res);
    if (err != WASMC_OK) {
        fprintf(stderr, "run: wasmc error %d, function %u\n", (int)err,
                (unsigned)res.func_index);
        return NULL;
    }
    if (getenv("ED_HOST_STATS")) {
        fprintf(stderr, "wasm %u, aot %u, work peak %u bytes\n",
                (unsigned)wasm_len, (unsigned)res.out_size,
                (unsigned)res.work_peak);
    }
    *len = res.out_size;
    return res.out;
#else
    *len = wasm_len;
    return wasm;
#endif
}

/*
 * Calls app_start or app_loop. Returns false on a real trap; otherwise @done
 * tells if the editor exited, or else it returned or unwound on purpose, and
 * app_loop is to be called (again). Only ed does the last.
 */
static bool call_app(wasm_exec_env_t env, const char *name, bool *done)
{
    wasm_function_inst_t func = wasm_runtime_lookup_function(g_inst, name);
    uint32_t argv[1];

    if (!func) {
        fprintf(stderr, "run: no export %s\n", name);
        return false;
    }
    if (wasm_runtime_call_wasm(env, func, 0, argv)) {
        return true;
    }
    const char *exc = wasm_runtime_get_exception(g_inst);
    /* WAMR prefixes the message with "Exception: " */
    if (strstr(exc, EXC_EXIT)) {
        *done = true;
        return true;
    }
    if (strstr(exc, EXC_THROW)) {
        wasm_runtime_clear_exception(g_inst);
        return true;
    }
    fprintf(stderr, "run: %s\n", exc);
    return false;
}

int main(int argc, char **argv)
{
    char err[128];
    uint32_t wasm_len, mod_len;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s module.wasm [arguments]\n", argv[0]);
        return 2;
    }
    g_args = argv + 1;
    g_nargs = argc - 1;

    uint8_t *wasm = read_file(argv[1], &wasm_len);
    if (!wasm) {
        perror(argv[1]);
        return 2;
    }
    uint8_t *mod = prepare_module(wasm, wasm_len, &mod_len);
    if (!mod) {
        return 2;
    }

    RuntimeInitArgs init;
    memset(&init, 0, sizeof init);
    init.mem_alloc_type = Alloc_With_System_Allocator;
    init.native_module_name = "env";
    init.native_symbols = g_natives;
    init.n_native_symbols = sizeof g_natives / sizeof g_natives[0];
    if (!wasm_runtime_full_init(&init)) {
        fprintf(stderr, "run: WAMR init failed\n");
        return 2;
    }
    wasm_module_t module = wasm_runtime_load(mod, mod_len, err, sizeof err);
    if (!module) {
        fprintf(stderr, "run: load: %s\n", err);
        return 2;
    }
    g_inst =
        wasm_runtime_instantiate(module, WASM_STACK_SIZE, 0, err, sizeof err);
    if (!g_inst) {
        fprintf(stderr, "run: instantiate: %s\n", err);
        return 2;
    }
    wasm_exec_env_t env = wasm_runtime_create_exec_env(g_inst, WASM_STACK_SIZE);
    if (!env) {
        fprintf(stderr, "run: no exec env\n");
        return 2;
    }

    bool done = false;
    if (!call_app(env, "app_start", &done)) {
        return 3;
    }
    while (!done) {
        if (!call_app(env, "app_loop", &done)) {
            return 3;
        }
    }
    return g_exit_code;
}
