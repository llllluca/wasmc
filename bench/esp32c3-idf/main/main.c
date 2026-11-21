/*
 * main.c - Minimal wasmc benchmark: one module, one mode, chosen at build
 * time (main/CMakeLists.txt).
 *
 *   wasmc   wasmc_compile() of the .wasm: time and memory of the compile,
 *           then WAMR AOT runtime on the AOT file it produced: load,
 *           instantiate, run (the "aot" object of each line);
 *   aot     WAMR AOT runtime on wamrc's AOT file: load, instantiate, run;
 *   interp  WAMR interpreter on the .wasm: load, instantiate, run.
 *
 * Every measure is done BENCH_REPEAT times (the first one is cold: code
 * read from flash through the cache) and printed as one JSON object per
 * line, between BENCH_BEGIN and BENCH_END, with only the fields the mode
 * measures:
 *
 *   {"rep":0,"code_B":1444,"compile_us":631,"heap_peak_B":2140,...,
 *    "aot":{"code_B":1444,"load_us":...,"run_us":...,"status":"ok"}}
 *
 * heap_peak_B is the most heap in use
 * at once during the measure, above the heap in use before it; for aot
 * and interp it includes the copy of the input to RAM, the module, the
 * instance with its linear memory and WAMR stack. stack_B is the native
 * stack the measure's thread used.
 */
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if MODE_WASMC
#include "wasmc/error.h"
#include "wasmc/wasmc.h"
#endif
#include "wasm_export.h"

#ifndef MODE_INTERP
#define MODE_INTERP 0
#endif

#define CAPS MALLOC_CAP_INTERNAL
#define THREAD_STACK 32768
#define WASM_STACK 16384

extern const uint8_t module_start[] asm("_binary_module_bin_start");
extern const uint8_t module_end[] asm("_binary_module_bin_end");

typedef struct {
    int64_t code, compile_us, load_us, inst_us, run_us, peak, work, stack;
    char result[24];
    char status[256]; /* room for WAMR's whole error messages */
} record;

static size_t base;

static void peak_begin(void)
{
    base = heap_caps_get_free_size(CAPS);
    heap_caps_monitor_local_minimum_free_size_start();
}

static int64_t peak_end(void)
{
    size_t min = heap_caps_get_minimum_free_size(CAPS);
    heap_caps_monitor_local_minimum_free_size_stop();
    return (int64_t)base - (int64_t)min;
}

static void set_status(record *r, const char *what, const char *detail)
{
    /* A message longer than the status is cut, on purpose. Using the return
       value says so to GCC: at -O2 it sees that "load: " plus a whole err[]
       may not fit, and -Werror=format-truncation fails the build. */
    if (snprintf(r->status, sizeof r->status, "%s%s%s", what, detail ? ": " : "",
                 detail ? detail : "") >= (int)sizeof r->status)
        strcpy(r->status + sizeof r->status - 4, "...");
    for (char *p = r->status; *p; p++)
        if (*p == '"' || *p == '\\' || *p < ' ')
            *p = '\'';
}

#if MODE_WASMC

static void *h_alloc(void *c, uint32_t n) { (void)c; return malloc(n); }
static void h_free(void *c, void *p, uint32_t n) { (void)c; (void)n; free(p); }
static void *h_grow(void *c, void *p, uint32_t o, uint32_t n)
{
    (void)c; (void)o;
    return realloc(p, n);
}
static void *h_shrink(void *c, void *p, uint32_t o, uint32_t n)
{
    (void)c; (void)o;
    return realloc(p, n);
}
static int h_grow_in_place(void *c, void *p, uint32_t o, uint32_t n)
{
    (void)c; (void)p; (void)o; (void)n;
    return 0;
}
static void *h_out_grow(void *c, void *p, uint32_t o, uint32_t n)
{
    (void)c; (void)o;
    if (!n) {
        free(p);
        return NULL;
    }
    return realloc(p, n);
}

static const wasmc_allocator heap_alloc = {
    .work_alloc = h_alloc, .work_free = h_free, .work_grow = h_grow,
    .aux_alloc = h_alloc, .aux_free = h_free, .aux_grow = h_grow_in_place,
    .out_grow = h_out_grow, .work_shrink = h_shrink,
};

/* compile the embedded .wasm; on success *out is the AOT file (malloc) */
static void compile_measure(record *r, uint8_t **out)
{
    uint32_t len = module_end - module_start;
    wasmc_result res;

    peak_begin();
    int64_t t = esp_timer_get_time();
    wasmc_err_t e = wasmc_compile(module_start, len, &heap_alloc, NULL, &res);
    r->compile_us = esp_timer_get_time() - t;
    r->peak = peak_end(); /* with the output, still held */
    r->work = res.work_peak;
    if (e != WASMC_OK) {
        set_status(r, "wasmc", wasmc_strerror(e));
        return;
    }
    r->code = res.out_size;
    *out = res.out;
}

#endif

/*
 * Load, instantiate and run a module with WAMR (aot: an AOT file; interp:
 * a .wasm). @in_ram NULL: the module is [src, src + len) in flash and is
 * first copied to RAM, inside the measure, as a module downloaded or read
 * from a file. Otherwise it is already in RAM at @in_ram (wasmc's output),
 * is taken over and freed, and its len bytes are added to heap_peak_B, so
 * the measure is the same as for a copy.
 */
static void wamr_measure(record *r, const uint8_t *src, uint32_t len, uint8_t *in_ram)
{
    char err[sizeof ((record *)0)->status];
    wasm_module_t mod = NULL;
    wasm_module_inst_t inst = NULL;
    wasm_exec_env_t env = NULL;

    r->code = len;
    peak_begin();
    uint8_t *buf = in_ram;
    if (!buf) {
        /* src is NULL only with in_ram; checked so that GCC -O2, inlining
           the wasmc call (src NULL), doesn't flag the memcpy (-Wnonnull) */
        buf = src ? malloc(len) : NULL;
        if (!buf) {
            set_status(r, "out of memory", NULL);
            goto out;
        }
        memcpy(buf, src, len);
    }

    /* An AOT file is loaded as not freeable and kept until the unload:
       WAMR 2.4.4's AOT loader reads module->is_binary_freeable before
       wasm_runtime_load_ex() sets it, so with wasm_binary_freeable the
       data segments are not copied but keep pointing into buf, and the
       unload then free()s those pointers (assert in heap_caps_free).
       Freeing buf after the load also made the instantiate copy garbage
       into the linear memory (heapsort-Os started from seed 0, not 42). */
    LoadArgs args = {.name = BENCH_NAME, .wasm_binary_freeable = MODE_INTERP};
    int64_t t = esp_timer_get_time();
    mod = wasm_runtime_load_ex(buf, len, &args, err, sizeof err);
    r->load_us = esp_timer_get_time() - t;
    if (!mod) {
        set_status(r, "load", err);
        goto out;
    }
    if (wasm_runtime_is_underlying_binary_freeable(mod)) {
        free(buf);
        buf = NULL;
    }

    t = esp_timer_get_time();
    inst = wasm_runtime_instantiate(mod, WASM_STACK, 0, err, sizeof err);
#if !BENCH_EXECUTE_FUNC
    if (inst)
        env = wasm_runtime_create_exec_env(inst, WASM_STACK);
#endif
    r->inst_us = esp_timer_get_time() - t;
    if (!inst) {
        set_status(r, "instantiate", err);
        goto out;
    }

#if BENCH_EXECUTE_FUNC
    /* WAMR's command-line way (iwasm -f): looks the function up by name,
       creates the instance's singleton exec env (WASM_STACK, from the
       instantiate) and prints the result itself as "0x...:i32", so the
       timed call includes all that and r->result stays empty */
    char *no_args[1] = {NULL};
    t = esp_timer_get_time();
    bool ok = wasm_application_execute_func(inst, BENCH_ENTRY, 0, no_args);
    r->run_us = esp_timer_get_time() - t;
    if (!ok)
        set_status(r, "trap", wasm_runtime_get_exception(inst));
#else
    if (!env) {
        set_status(r, "instantiate", "exec env");
        goto out;
    }

    wasm_function_inst_t fn = wasm_runtime_lookup_function(inst, BENCH_ENTRY);
    if (!fn) {
        set_status(r, "no export", BENCH_ENTRY);
        goto out;
    }
    uint32_t argv[2] = {0, 0}; /* entry: no params, i32 result */
    t = esp_timer_get_time();
    bool ok = wasm_runtime_call_wasm(env, fn, 0, argv);
    r->run_us = esp_timer_get_time() - t;
    if (!ok) {
        set_status(r, "trap", wasm_runtime_get_exception(inst));
        goto out;
    }
    snprintf(r->result, sizeof r->result, "0x%08" PRIx32, argv[0]);
#endif

out:
    if (env)
        wasm_runtime_destroy_exec_env(env);
    if (inst)
        wasm_runtime_deinstantiate(inst);
    if (mod)
        wasm_runtime_unload(mod);
    free(buf);
    r->peak = peak_end() + (in_ram ? (int64_t)len : 0);
}

/* one measure, in a thread of its own, for its stack_B */
typedef struct {
    record *r;
    uint8_t *aot; /* wasmc: compile output, then wamr_measure() input */
    uint32_t aot_len;
    bool compile; /* wasmc: the compile, otherwise the WAMR run */
} job;

static void *measure_thread(void *arg)
{
    job *j = arg;
#if MODE_WASMC
    if (j->compile)
        compile_measure(j->r, &j->aot);
    else
        wamr_measure(j->r, NULL, j->aot_len, j->aot);
#else
    wamr_measure(j->r, module_start, module_end - module_start, NULL);
#endif
    j->r->stack = THREAD_STACK - (int64_t)uxTaskGetStackHighWaterMark(NULL);
    return NULL;
}

static void run_job(job *j)
{
    pthread_attr_t attr;
    pthread_t th;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, THREAD_STACK);
    if (pthread_create(&th, &attr, measure_thread, j) != 0)
        set_status(j->r, "cannot start thread", NULL);
    else
        pthread_join(th, NULL);
    pthread_attr_destroy(&attr);
}

static void record_init(record *r)
{
    *r = (record){.code = -1, .compile_us = -1, .load_us = -1, .inst_us = -1,
                  .run_us = -1, .peak = -1, .work = -1, .stack = -1};
    strcpy(r->status, "ok");
}

/* the optimization level the firmware (and WAMR) was compiled with */
#if CONFIG_COMPILER_OPTIMIZATION_PERF
#define OPT "-O2"
#elif CONFIG_COMPILER_OPTIMIZATION_SIZE
#define OPT "-Os"
#elif CONFIG_COMPILER_OPTIMIZATION_DEBUG
#define OPT "-Og"
#elif CONFIG_COMPILER_OPTIMIZATION_NONE
#define OPT "-O0"
#else
#define OPT "?"
#endif

/* a JSON field, left out if not measured (< 0) */
static void field(const char *key, int64_t v)
{
    if (v >= 0)
        printf(",\"%s\":%" PRId64, key, v);
}

/* the fields of a record, after the opening '{' */
static void print_fields(const record *r)
{
    field("code_B", r->code);
    field("compile_us", r->compile_us);
    field("load_us", r->load_us);
    field("inst_us", r->inst_us);
    field("run_us", r->run_us);
    field("heap_peak_B", r->peak);
    field("wasmc_work_B", r->work);
    field("stack_B", r->stack);
    if (r->result[0])
        printf(",\"result\":\"%s\"", r->result);
    printf(",\"status\":\"%s\"", r->status);
}

/*
 * WAMR's allocator: the one of its esp-idf port, with realloc fixed.
 *
 * The port's os_malloc() returns an 8-byte aligned pointer 4 or 8 bytes
 * into a malloc() block (ESP-IDF's heap aligns to 4 bytes only), with the
 * block's address just before it. WAMR 2.4.4's os_realloc(), when
 * realloc() moves the block to one aligned the other way, returns the new
 * aligned pointer but leaves the data where realloc() copied it: 4 bytes
 * off. The fast interpreter's loader reallocs its arrays all the time, so
 * interp got "type mismatch" and "fast interpreter offset overflow" on
 * valid modules, and a corrupted heap (crashes in tlsf_malloc/tlsf_free).
 * Same layout, so the same heap_peak_B; the data is moved when needed.
 */
#define W_EXTRA (8 + sizeof(uintptr_t))

/* the 8-byte aligned pointer of a block: 4 or 8 bytes in */
static uint8_t *w_fixed(uint8_t *block)
{
    return (uint8_t *)(((uintptr_t)block + sizeof(uintptr_t) + 7) & ~(uintptr_t)7);
}

static void *w_set(uint8_t *block)
{
    uint8_t *p = w_fixed(block);
    ((uintptr_t *)p)[-1] = (uintptr_t)block;
    return p;
}

static void *w_malloc(unsigned int size)
{
    uint8_t *block = malloc(size + W_EXTRA);
    return block ? w_set(block) : NULL;
}

static void w_free(void *p)
{
    if (p)
        free((void *)((uintptr_t *)p)[-1]);
}

static void *w_realloc(void *p, unsigned int size)
{
    if (!p)
        return w_malloc(size);
    uint8_t *old = (uint8_t *)((uintptr_t *)p)[-1];
    size_t off = (uint8_t *)p - old;
    uint8_t *block = realloc(old, size + W_EXTRA);
    if (!block)
        return NULL;
    /* realloc() copied the data at the old offset: move it to the new one
       (within the block: both offsets are at most 8), then the header */
    if ((size_t)(w_fixed(block) - block) != off)
        memmove(w_fixed(block), block + off, size);
    return w_set(block);
}

static void *bench_thread(void *arg)
{
    (void)arg;
    RuntimeInitArgs init = {
        .mem_alloc_type = Alloc_With_Allocator,
        .mem_alloc_option.allocator = {.malloc_func = w_malloc,
                                       .realloc_func = w_realloc,
                                       .free_func = w_free},
    };
    if (!wasm_runtime_full_init(&init)) {
        printf("WAMR init failed\n");
        return NULL;
    }
    /* the first monitor call allocates its state: keep it out */
    heap_caps_monitor_local_minimum_free_size_start();
    heap_caps_monitor_local_minimum_free_size_stop();

    printf("BENCH_BEGIN\n");
    printf("{\"mode\":\"%s\",\"module\":\"%s\",\"opt\":\"%s\","
           "\"embedded_B\":%u,\"free_heap_B\":%u}\n", BENCH_MODE, BENCH_NAME, OPT,
           (unsigned)(module_end - module_start),
           (unsigned)heap_caps_get_free_size(CAPS));
    for (int i = 0; i < BENCH_REPEAT; i++) {
        record r;
        record_init(&r);
        job j = {.r = &r, .compile = true};
        run_job(&j);

        printf("{\"rep\":%d", i);
        print_fields(&r);
#if MODE_WASMC
        /* run the AOT file wasmc produced, as --mode aot runs wamrc's */
        if (j.aot) {
            record a;
            record_init(&a);
            job ja = {.r = &a, .aot = j.aot, .aot_len = (uint32_t)r.code};
            run_job(&ja);
            if (strcmp(a.status, "cannot start thread") == 0)
                free(j.aot); /* wamr_measure() did not take it over */
            printf(",\"aot\":{\"rep\":%d", i);
            print_fields(&a);
            printf("}");
        }
#endif
        printf("}\n");
    }
    printf("BENCH_END\n");
    fflush(stdout);
    return NULL;
}

/* WAMR's ESP-IDF port needs a pthread (pthread_self()) */
void app_main(void)
{
    pthread_attr_t attr;
    pthread_t th;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8192);
    pthread_create(&th, &attr, bench_thread, NULL);
    pthread_join(th, NULL);
}
