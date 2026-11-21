/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "wasmc/wasmc.h"
#include "aotw.h"
#include "compile.h"
#include "emit.h"
#include "ir.h"
#include "measure.h"
#include "wasm_decode.h"
#include "wasmc/error.h"
#include "wasmc_alloc.h"
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the output reserved for a function's code before it is compiled, as a
 * fraction of its body size (aotw_reserve()), for bodies of at least
 * PRESIZE_MIN_BODY bytes */
#define PRESIZE_NUM      5U
#define PRESIZE_DEN      2U
#define PRESIZE_MIN_BODY 8192U

#ifdef IR_ENABLE_DUMP
#include <stdio.h>
#endif

/* the first size of the validator's stacks; doubled while they overflow */
#define VALIDATOR_MIN 256U

/**
 * struct wc_run - The state of one wasmc_compile() run.
 * @module: the module.
 * @opt: the options.
 * @res: the result.
 * @user: the caller's allocator.
 * @mem: what the compiler allocates from: @user, counting the work bytes
 *     (aux blocks included).
 * @work_live: work bytes live now.
 * @work_peak: the most work bytes live at once.
 * @func_peak: the most work bytes live at once in the current function.
 * @caps: the validator's stack high-water marks.
 * @writer: the AOT writer.
 * @stats_head: --stats: the table's header is printed.
 */
typedef struct {
    const wdec_module_t *module;
    const wasmc_options *opt;
    wasmc_result *res;
    const wasmc_allocator *user;
    wasmc_allocator mem;
    uint32_t work_live;
    uint32_t work_peak;
    uint32_t func_peak;
    wdec_stack_depth caps;
    aotw_t *writer;
    bool presize; /* aotw_reserve() before each function (not static) */
#ifdef IR_ENABLE_DUMP
    bool stats_head;
#endif
} wc_run;

/**
 * struct ir_build - The IR of one function and how it was sized.
 * @func: the IR.
 * @lim: its limits.
 */
typedef struct {
    ir_func func;
    ir_limits lim;
} ir_build;

/* ------------------------------------------------------------------------ */
/* Memory: the caller's allocator, counting the work bytes                  */
/* ------------------------------------------------------------------------ */

static void count_more(wc_run *run, uint32_t size)
{
    run->work_live += size;
    if (run->work_live > run->work_peak) {
        run->work_peak = run->work_live;
    }
    if (run->work_live > run->func_peak) {
        run->func_peak = run->work_live;
    }
}

static void *count_work_alloc(void *ctx, uint32_t size)
{
    wc_run *run = ctx;
    void *ptr = run->user->work_alloc(run->user->ctx, size);
    if (ptr) {
        count_more(run, size);
    }
    return ptr;
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void count_work_free(void *ctx, void *ptr, uint32_t size)
{
    wc_run *run = ctx;
    run->user->work_free(run->user->ctx, ptr, size);
    run->work_live -= size;
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void *count_work_grow(void *ctx, void *ptr, uint32_t old_size,
                             uint32_t new_size)
{
    wc_run *run = ctx;
    void *grown = run->user->work_grow(run->user->ctx, ptr, old_size, new_size);
    if (grown) {
        count_more(run, new_size - old_size);
    }
    return grown;
}

/* the caller's work_shrink, if it has one */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void *count_work_shrink(void *ctx, void *ptr, uint32_t old_size,
                               uint32_t new_size)
{
    wc_run *run = ctx;
    if (!run->user->work_shrink) {
        return NULL;
    }
    void *shrunk =
        run->user->work_shrink(run->user->ctx, ptr, old_size, new_size);
    if (shrunk) {
        run->work_live -= old_size - new_size;
    }
    return shrunk;
}

static void *count_aux_alloc(void *ctx, uint32_t size)
{
    wc_run *run = ctx;
    void *ptr = run->user->aux_alloc(run->user->ctx, size);
    if (ptr) {
        count_more(run, size);
    }
    return ptr;
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static int count_aux_grow(void *ctx, void *ptr, uint32_t old_size,
                          uint32_t new_size)
{
    wc_run *run = ctx;
    int grown = run->user->aux_grow(run->user->ctx, ptr, old_size, new_size);
    if (grown) {
        count_more(run, new_size - old_size);
    }
    return grown;
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void count_aux_free(void *ctx, void *ptr, uint32_t size)
{
    wc_run *run = ctx;
    run->user->aux_free(run->user->ctx, ptr, size);
    run->work_live -= size;
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void *pass_out_grow(void *ctx, void *ptr, uint32_t old_size,
                           uint32_t new_size)
{
    const wc_run *run = ctx;
    return run->user->out_grow(run->user->ctx, ptr, old_size, new_size);
}

static void *work_alloc(wc_run *run, uint32_t size)
{
    return run->mem.work_alloc(run->mem.ctx, size);
}

static void work_free(wc_run *run, void *ptr, uint32_t size)
{
    run->mem.work_free(run->mem.ctx, ptr, size);
}

/* ------------------------------------------------------------------------ */
/* Validation                                                               */
/* ------------------------------------------------------------------------ */

/* Validates the whole module (wcompile_func() requires it). Its stacks'
 * needs are known only once it completes: it starts with VALIDATOR_MIN
 * bytes, and validates again with twice as many while they overflow. */
static wasmc_err_t validate_module(wc_run *run)
{
    uint32_t size = VALIDATOR_MIN;
    for (;;) {
        uint8_t *stacks = work_alloc(run, size);
        if (!stacks) {
            return WASMC_ERR_OUT_OF_MEMORY;
        }
        wdec_validator_t validator;
        wdec_validator_init(&validator, stacks, size);
        wasmc_err_t err =
            wdec_validate(run->module, &validator, &run->res->func_index);
        work_free(run, stacks, size);
        if ((err == WASMC_ERR_OPD_STACK_OVERFLOW ||
             err == WASMC_ERR_CTRL_STACK_OVERFLOW) &&
            size <= UINT32_MAX / 2U) {
            size *= 2U;
            continue;
        }
        if (!err) {
            run->res->func_index = WASMC_NO_FUNC;
            /* they bound every function's stack depths, so they are the
             * exact capacities the compiler needs */
            run->caps = wdec_validator_max_depth(&validator);
        }
        return err;
    }
}

/* ------------------------------------------------------------------------ */
/* One function                                                             */
/* ------------------------------------------------------------------------ */

/* Measures the function before its IR is built (wmeasure_func()): upper
 * bounds of its blocks, edges and instruction slots, which the IR
 * cannot exceed, and a first capacity of the phi region, only an estimate:
 * the region grows while the IR is built. Fails with WASMC_ERR_IR_* if a
 * bound does not fit the IR's 16-bit ids. The pre-pass's temporary memory
 * (a bitmap of the locals, a byte per control frame) is a work block of its
 * own, freed before it returns. */
static wasmc_err_t measure_func(wc_run *run, const wdec_func_t *func,
                                const wdec_functype_t *ftype, wmeasure_t *meas)
{
    /* (wmeasure_func() rejects more than UINT16_MAX locals before it uses
     * the memory) */
    uint32_t nlocals = ftype->param_count + func->local_count;
    if (nlocals < func->local_count || nlocals > UINT16_MAX) {
        nlocals = UINT16_MAX;
    }
    const uint32_t size =
        (nlocals / CHAR_BIT) + 1U + run->caps.ctrl + 2U + WMEASURE_CONST_TMP;
    uint8_t *tmp = work_alloc(run, size);
    if (!tmp) {
        return WASMC_ERR_OUT_OF_MEMORY;
    }
    wasmc_err_t err =
        wmeasure_func(run->module, func, ftype, run->caps, tmp, size, meas);
    work_free(run, tmp, size);
    if (err) {
        return err;
    }
    if (meas->blocks >= IR_NIL) {
        return WASMC_ERR_IR_BLOCKS;
    }
    if (meas->edges >= IR_NIL) {
        return WASMC_ERR_IR_EDGES;
    }
    if (meas->insn_slots >= IR_MAX_SLOTS) {
        return WASMC_ERR_IR_SLOTS;
    }
    return WASMC_OK;
}

/* The IR limits of the function, from its measurements. */
static ir_limits func_limits(const wmeasure_t *meas)
{
    uint32_t phi = meas->phi_slots;
    if (meas->insn_slots + phi >= IR_MAX_SLOTS) {
        phi = IR_MAX_SLOTS - 1U - meas->insn_slots;
    }
    ir_limits lim;
    lim.insn_slots = (uint16_t)meas->insn_slots;
    lim.phi_slots = (uint16_t)phi;
    lim.max_blocks = (uint16_t)meas->blocks;
    lim.max_edges = (uint16_t)(meas->edges ? meas->edges : 1);
    return lim;
}

#ifdef IR_ENABLE_DUMP
/* --stats: one "used/reserved" column of the table */
static void print_pair(uint32_t used, uint32_t reserved)
{
    char pair[sizeof "4294967295/4294967295"];
    snprintf(pair, sizeof pair, "%u/%u", used, reserved);
    printf(" %11s", pair);
}

/* --stats: the table row of the compiled function, measured vs actual
 * sizes, and the most work memory live while it was compiled. The header
 * comes before the first row. */
static void print_stats(wc_run *run, uint32_t funcidx, const wmeasure_t *meas,
                        const ir_build *build)
{
    const ir_func *func = &build->func;
    if (!run->stats_head) {
        run->stats_head = true;
        printf("functions (x/y: used/reserved, sizes in bytes)\n");
        printf("%5s %7s %6s %7s %11s %11s %11s %11s %5s %8s %8s\n", "func",
               "body", "locals", "written", "blocks", "edges", "insn slots",
               "phi slots", "grows", "scratch", "work");
    }
    printf("%5u %7u %6u %7u", funcidx, meas->body_size, meas->nlocals,
           meas->nwritten);
    print_pair(func->nblocks, meas->blocks);
    print_pair(func->nedges, meas->edges);
    print_pair(func->ninsn, meas->insn_slots);
    print_pair((uint32_t)(func->phi_top - func->phi_base),
               (uint32_t)(func->max_slots - func->phi_base));
    printf(" %5u %8u %8u\n", func->phi_grows, func->scratch_peak,
           run->func_peak);
}

/* --dump-ir: the IR of the function, a blank line after all but the last */
static void print_dump(const ir_build *build, uint32_t funcidx, bool last)
{
    printf("=== Function %u ===\n", funcidx);
    ir_dump(&build->func, stdout);
    if (!last) {
        printf("\n");
    }
}
#endif

/* Compiles the defined function `func`, number `funcidx`; `last` if it is
 * the module's last: measures it, builds its IR, sized from the
 * measurements, and emits its code. The IR (whose phi region grows as
 * needed) is freed before it returns; an error after ir_init() jumps to
 * `error`, which frees it. */
static wasmc_err_t compile_function(wc_run *run, uint32_t funcidx,
                                    const wdec_func_t *func, bool last)
{
    wmeasure_t meas = {0};
    wdec_functype_t ftype;
    ir_build build;
    run->res->func_index = funcidx;
    /* the output buffer grows for this function's code now, while none of
     * its work memory is live: a heap's realloc that moves it holds both
     * copies for a moment, and at the end of a large function that would be
     * on top of its work memory (nsichneu's main function: 54 + 61 KB of
     * output next to 100 KB of work). Its code is typically 2-2.5 times its
     * body; a short estimate only brings back growth on demand. Only for
     * large bodies: a small function's work memory is small, so a move
     * during it costs little, and reserving ahead for every function made
     * the peak higher for 36 of 176 modules (switch2 +6.8 KB) and lower
     * for none but the large ones. Not for wasmc_static_allocator(), whose
     * output grows in place: an estimate too large would take room from
     * the work blocks. */
    const uint64_t body = (uint64_t)(func->body_end - func->body);
    if (run->presize && body >= PRESIZE_MIN_BODY) {
        aotw_reserve(run->writer, (uint32_t)(body * PRESIZE_NUM / PRESIZE_DEN));
    }
    run->func_peak = run->work_live;
    wasmc_err_t err = wdec_get_type(run->module, func->typeidx, &ftype);
    if (err) {
        return err;
    }
    err = measure_func(run, func, &ftype, &meas);
    if (err) {
        return err;
    }
    build.lim = func_limits(&meas);
    err = ir_init(&build.func, &build.lim, &run->mem);
    if (err) {
        return err; /* (nothing stays allocated) */
    }

    err = wcompile_func(run->module, func, &ftype, &build.func, run->caps);
    if (err) {
        goto error;
    }
#ifdef IR_ENABLE_DUMP
    if (run->opt->dump_ir) {
        print_dump(&build, funcidx, last);
    }
#else
    (void)last;
#endif
    err = emit_func(&build.func, run->module, funcidx, &ftype, run->writer,
                    run->opt);
    if (err) {
        goto error;
    }
#ifdef IR_ENABLE_DUMP
    if (run->opt->stats) {
        print_stats(run, funcidx, &meas, &build);
    }
#endif
    ir_free(&build.func);
    return WASMC_OK;

error:
    ir_free(&build.func);
    return err;
}

/* ------------------------------------------------------------------------ */
/* The module                                                               */
/* ------------------------------------------------------------------------ */

/* Writes the AOT file: the data the writer keeps for the whole module
 * (function offsets, global offsets, canonical type indices) is a work
 * block under every function's. On success the output block is in res;
 * every error jumps to `error`, which frees it (aotw_abort()). Both free
 * the module data. */
static wasmc_err_t compile_module(wc_run *run)
{
    /* the writer's bookkeeping, not part of the file: offsets and indices.
     * It lives from aotw_begin() to aotw_end(), across out_grow() calls, so
     * it cannot be an aux block: a work block, the oldest, it never grows */
    const uint32_t mod = aotw_module_data_size(run->module);
    void *data = mod ? work_alloc(run, mod) : NULL;
    if (mod && !data) {
        return WASMC_ERR_OUT_OF_MEMORY;
    }
    const uint32_t nfunc = wdec_func_count(run->module);
    uint32_t funcidx = wdec_imported_func_count(run->module);
    wdec_func_iter_t func_it;
    wdec_func_t func;
    uint32_t out_size = 0;
    aotw_t writer;
    run->writer = &writer;
    wasmc_err_t err = aotw_begin(&writer, run->module, &run->mem, data);
    if (err) {
        goto error;
    }

    WDEC_FOR_EACH_FUNC(func_it, func, run->module) {
        err = compile_function(run, funcidx, &func, funcidx == nfunc - 1);
        if (err) {
            goto error;
        }
        funcidx++;
    }
    err = wdec_func_iter_err(&func_it);
    if (err) {
        goto error;
    }

    run->res->func_index = WASMC_NO_FUNC;
    err = aotw_end(&writer, &out_size);
    if (err) {
        goto error;
    }
    run->writer = NULL;
    if (mod) {
        work_free(run, data, mod);
    }
    run->res->out = writer.buf;
    run->res->out_size = out_size;
    run->res->out_cap = writer.cap;
    return WASMC_OK;

error:
    run->writer = NULL;
    if (mod) {
        work_free(run, data, mod);
    }
    aotw_abort(&writer);
    return err;
}

wasmc_err_t wasmc_compile(const uint8_t *wasm, uint32_t wasm_len,
                          const wasmc_allocator *mem, const wasmc_options *opt,
                          wasmc_result *res)
{
    static const wasmc_options defaults;
    wasmc_result dummy;
    wdec_module_t module;

    if (!opt) {
        opt = &defaults;
    }
    if (!res) {
        res = &dummy;
    }
    res->out = NULL;
    res->out_size = 0;
    res->out_cap = 0;
    res->work_peak = 0;
    res->func_index = WASMC_NO_FUNC;

    if (!wasm || !mem || !mem->work_alloc || !mem->work_free ||
        !mem->work_grow || !mem->aux_alloc || !mem->aux_free ||
        !mem->aux_grow || !mem->out_grow) {
        return WASMC_ERR_INVALID_ARG;
    }
    wasmc_err_t err = wdec_decode(&module, wasm, wasm_len);
    if (err) {
        return err;
    }
    wc_run run = {.module = &module,
                  .opt = opt,
                  .res = res,
                  .user = mem,
                  .presize = !wasmc_alloc_is_static(mem)};
    run.mem.ctx = &run;
    run.mem.work_alloc = count_work_alloc;
    run.mem.work_free = count_work_free;
    run.mem.work_grow = count_work_grow;
    run.mem.aux_alloc = count_aux_alloc;
    run.mem.aux_free = count_aux_free;
    run.mem.aux_grow = count_aux_grow;
    run.mem.out_grow = pass_out_grow;
    run.mem.work_shrink = count_work_shrink;

    err = validate_module(&run);
    if (!err) {
        err = compile_module(&run);
    }
    res->work_peak = run.work_peak;
    return err;
}
