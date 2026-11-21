/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * main.c - wasmc demo for the ESP32-C3.
 *
 * For each WebAssembly module in wasm_modules.h (built from the C programs
 * in ../wasm/ by ../wasm/build.sh):
 *
 *   1. wasmc_compile() turns the .wasm into a WAMR AOT file with RV32IM
 *      code, here on the device, in one static buffer;
 *   2. the WAMR AOT runtime loads that file and instantiates it;
 *   3. its exported functions are called, and the results checked.
 */
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "wasm_export.h"
#include "wasmc/error.h"
#include "wasmc/wasmc.h"

#include "wasm_modules.h"

/* WAMR: stack of the exec env (AOT code runs on the native stack) */
#define WASM_STACK_SIZE (8 * 1024)

/* the demo thread: wasmc, WAMR and the AOT code run on its stack */
#define DEMO_STACK_SIZE (12 * 1024)

/*
 * wasmc's memory: the work memory of a compilation and the AOT file, in
 * one static buffer (the largest module here takes under 6 KB). The AOT
 * file stays at the start of it until the next compilation, so a module
 * is unloaded from WAMR before the next one is compiled.
 */
static uint8_t wasmc_ram[8 * 1024];

/* env.putchar, imported by mandel.wasm: the firmware's putchar */
static void env_putchar(wasm_exec_env_t env, int32_t c)
{
    (void)env;
    putchar(c);
}

static NativeSymbol env_natives[] = {
    {"putchar", (void *)env_putchar, "(i)", NULL},
};

/* The same function as fib.wasm, compiled natively, for comparison. */
static int native_fib(int n)
{
    return n < 2 ? n : native_fib(n - 1) + native_fib(n - 2);
}

/* A module compiled and instantiated. */
typedef struct {
    wasm_module_t module;
    wasm_module_inst_t inst;
    wasm_exec_env_t env;
} instance;

/*
 * call() - Call the exported function `name` with one i32 argument.
 * Returns its i32 result in *result, and the time it took in microseconds.
 */
static bool call(const instance *in, const char *name, int32_t arg,
                 int32_t *result, int64_t *us)
{
    wasm_function_inst_t fn = wasm_runtime_lookup_function(in->inst, name);
    if (!fn) {
        printf("  no export %s\n", name);
        return false;
    }
    uint32_t argv[1] = {(uint32_t)arg}; /* in: the argument; out: result */
    int64_t start = esp_timer_get_time();
    bool ok = wasm_runtime_call_wasm(in->env, fn, 1, argv);
    *us = esp_timer_get_time() - start;
    if (!ok) {
        printf("  %s: trap: %s\n", name, wasm_runtime_get_exception(in->inst));
        return false;
    }
    *result = (int32_t)argv[0];
    return true;
}

/* Prints one call: its result, the expected value, the time. */
static void report(const char *what, int32_t got, int32_t want, int64_t us)
{
    printf("  %-28s = %-8" PRId32 " %s  %8" PRId64 " us\n", what, got,
           got == want ? "ok   " : "WRONG", us);
}

/*
 * load() - Compile `wasm` with wasmc, then load and instantiate the AOT
 * file with WAMR.
 */
static bool load(const char *name, const uint8_t *wasm, uint32_t len,
                 instance *in)
{
    char err[128];
    wasmc_allocator mem;
    wasmc_static_arena arena;
    wasmc_result res;
    wasmc_static_allocator(&mem, &arena, wasmc_ram, sizeof wasmc_ram);

    int64_t start = esp_timer_get_time();
    wasmc_err_t e = wasmc_compile(wasm, len, &mem, NULL, &res);
    int64_t us = esp_timer_get_time() - start;
    if (e != WASMC_OK) {
        printf("%s: wasmc: %s\n", name, wasmc_strerror(e));
        return false;
    }
    printf("\n%s: %" PRIu32 " bytes of WASM -> %" PRIu32
           " bytes of AOT in %" PRId64 " us (wasmc memory: %" PRIu32
           " bytes)\n",
           name, len, res.out_size, us, arena.work_peak + res.out_size);

    in->module = wasm_runtime_load(res.out, res.out_size, err, sizeof err);
    if (!in->module) {
        printf("  WAMR load: %s\n", err);
        return false;
    }
    in->inst = wasm_runtime_instantiate(in->module, WASM_STACK_SIZE, 0, err,
                                        sizeof err);
    if (!in->inst) {
        printf("  WAMR instantiate: %s\n", err);
        wasm_runtime_unload(in->module);
        return false;
    }
    in->env = wasm_runtime_create_exec_env(in->inst, WASM_STACK_SIZE);
    if (!in->env) {
        printf("  WAMR exec env: out of memory\n");
        wasm_runtime_deinstantiate(in->inst);
        wasm_runtime_unload(in->module);
        return false;
    }
    return true;
}

static void unload(instance *in)
{
    wasm_runtime_destroy_exec_env(in->env);
    wasm_runtime_deinstantiate(in->inst);
    wasm_runtime_unload(in->module);
}

static void demo_fib(void)
{
    instance in;
    int32_t got;
    int64_t us;
    if (!load("fib.wasm", fib_wasm, fib_wasm_len, &in)) {
        return;
    }
    if (call(&in, "fib", 24, &got, &us)) {
        report("fib(24)", got, 46368, us);
    }
    int64_t start = esp_timer_get_time();
    int native = native_fib(24);
    report("fib(24), native C", native, 46368, esp_timer_get_time() - start);
    unload(&in);
}

static void demo_sieve(void)
{
    instance in;
    int32_t got;
    int64_t us;
    if (!load("sieve.wasm", sieve_wasm, sieve_wasm_len, &in)) {
        return;
    }
    if (call(&in, "count_primes", 50000, &got, &us)) {
        report("primes below 50000", got, 5133, us);
    }
    unload(&in);
}

static void demo_collatz(void)
{
    instance in;
    int32_t start;
    int32_t steps;
    int64_t us;
    if (!load("collatz.wasm", collatz_wasm, collatz_wasm_len, &in)) {
        return;
    }
    /* (about 1.3 s at 160 MHz: under the 5 s of the task watchdog) */
    if (call(&in, "longest_collatz", 30000, &start, &us)) {
        report("longest Collatz below 30000", start, 26623, us);
        if (call(&in, "collatz_steps", start, &steps, &us)) {
            report("  its steps", steps, 307, us);
        }
    }
    unload(&in);
}

static void demo_mandel(void)
{
    instance in;
    if (!load("mandel.wasm", mandel_wasm, mandel_wasm_len, &in)) {
        return;
    }
    wasm_function_inst_t fn =
        wasm_runtime_lookup_function(in.inst, "mandelbrot");
    uint32_t argv[3] = {60, 24, 64}; /* width, height, iterations */
    int64_t start = esp_timer_get_time();
    if (fn && wasm_runtime_call_wasm(in.env, fn, 3, argv)) {
        report("points inside the set", (int32_t)argv[0], 322,
               esp_timer_get_time() - start);
    } else {
        printf("  mandelbrot: %s\n",
               fn ? wasm_runtime_get_exception(in.inst) : "no export");
    }
    unload(&in);
}

/* The demo, in a pthread: WAMR's ESP-IDF port needs one (pthread_self()). */
static void *demo_thread(void *arg)
{
    (void)arg;
    RuntimeInitArgs init;
    memset(&init, 0, sizeof init);
    init.mem_alloc_type = Alloc_With_System_Allocator;
    init.native_module_name = "env";
    init.native_symbols = env_natives;
    init.n_native_symbols = sizeof env_natives / sizeof env_natives[0];
    if (!wasm_runtime_full_init(&init)) {
        printf("WAMR init failed\n");
        return NULL;
    }

    printf("wasmc demo: free heap %u bytes\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    demo_fib();
    demo_sieve();
    demo_collatz();
    demo_mandel();
    printf("\ndone: free heap %u bytes\n",
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));

    wasm_runtime_destroy();
    return NULL;
}

void app_main(void)
{
    pthread_attr_t attr;
    pthread_t thread;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, DEMO_STACK_SIZE);
    if (pthread_create(&thread, &attr, demo_thread, NULL) != 0) {
        printf("cannot start the demo thread\n");
        return;
    }
    pthread_join(thread, NULL);
}
