/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * main.c - The wasmc command: compile a WASM module to a WAMR AOT file.
 *
 * Reads the module, compiles it with wasmc_compile() and writes the AOT
 * file. It also shows the two ways to give the library memory (see
 * compile()): from the heap, an allocator over malloc(), free() and
 * realloc(); or, with --work-size, one buffer of a fixed size, as on a
 * device (wasmc_static_allocator()).
 */
#include "wasmc/error.h"
#include "wasmc/wasmc.h"
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/**
 * print_usage() - Print the command line help.
 * @stream: where to print it.
 */
static void print_usage(FILE *stream)
{
    static const char *usage =
        "Usage: wasmc <option(s)> path/to/file.wasm\n"
        "Compile a WASM module to a WAMR AOT file with RV32IM code.\n"
        "Options are:\n\n"
        "  -h, --help             Display this information.\n"
        "  -o <file>              Place the output into <file> (default "
        "a.aot).\n"
        "  --no-bounds-checks     Do not check linear memory accesses.\n"
        "  --no-aux-stack-check   Do not check the __stack_pointer global.\n"
        "  --long-branches        Always use the long branch forms (testing).\n"
        "  --dump-ir              Dump the IR of every function to stdout.\n"
        "  --stats                Print the memory the compilation took\n"
        "                         (and, in builds with -DIR_ENABLE_DUMP,\n"
        "                         measured vs actual IR sizes per function).\n"
        "  --work-size <bytes>    Compile in one buffer of that size, work\n"
        "                         memory and AOT file, as on a device; by\n"
        "                         default the memory comes from the heap.\n";
    fprintf(stream, "%s\n", usage);
}

/**
 * struct cli_args - What the command line asks for.
 * @opt: the compilation options.
 * @input_path: the WASM module.
 * @output_path: the AOT file to write.
 * @work_size: the size of the one buffer to compile in, 0 for the heap.
 */
typedef struct {
    wasmc_options opt;
    const char *input_path;
    const char *output_path;
    unsigned long work_size;
} cli_args;

/**
 * parse_size() - Read the size of --work-size.
 * @text: the argument: decimal, 0x hexadecimal or 0 octal, as strtoul().
 * @size: set to the size on success.
 *
 * Return: true if @text is all a number from 1 to UINT32_MAX (no sign, no
 * trailing characters), else false.
 */
static bool parse_size(const char *text, unsigned long *size)
{
    char *end;
    if (!isdigit((unsigned char)text[0])) { /* strtoul() takes "-1", " 1" */
        return false;
    }
    errno = 0;
    const unsigned long value = strtoul(text, &end, 0);
    if (errno || *end != '\0' || value == 0 || value > UINT32_MAX) {
        return false;
    }
    *size = value;
    return true;
}

/**
 * parse_args() - Read the command line.
 * @argc: the number of arguments.
 * @argv: the arguments.
 * @args: filled with what they ask for; zeroed by the caller.
 *
 * Without -o, the AOT file is a.aot.
 *
 * Return: -1 to go on, else the exit status (after --help, or after printing
 * the error).
 */
static int parse_args(int argc, char **argv, cli_args *args)
{
    enum {
        OPT_NO_BOUNDS = 256,
        OPT_NO_AUX,
        OPT_LONG_BR,
        OPT_DUMP_IR,
        OPT_STATS,
        OPT_WORK_SIZE
    };
    static const struct option long_options[] = {
        {"help", no_argument, NULL, 'h'},
        {"no-bounds-checks", no_argument, NULL, OPT_NO_BOUNDS},
        {"no-aux-stack-check", no_argument, NULL, OPT_NO_AUX},
        {"long-branches", no_argument, NULL, OPT_LONG_BR},
        {"dump-ir", no_argument, NULL, OPT_DUMP_IR},
        {"stats", no_argument, NULL, OPT_STATS},
        {"work-size", required_argument, NULL, OPT_WORK_SIZE},
        {NULL, 0, NULL, 0},
    };
    int chr;
    while ((chr = getopt_long(argc, argv, "ho:", long_options, NULL)) != -1) {
        switch (chr) {
        case 'h':
            print_usage(stdout);
            return EXIT_SUCCESS;
        case 'o':
            args->output_path = optarg;
            break;
        case OPT_NO_BOUNDS:
            args->opt.no_bounds_checks = 1;
            break;
        case OPT_NO_AUX:
            args->opt.no_aux_stack_check = 1;
            break;
        case OPT_LONG_BR:
            args->opt.long_branches = 1;
            break;
        case OPT_DUMP_IR:
            args->opt.dump_ir = 1;
            break;
        case OPT_STATS:
            args->opt.stats = 1;
            break;
        case OPT_WORK_SIZE:
            if (!parse_size(optarg, &args->work_size)) {
                fprintf(stderr,
                        "Error: --work-size: %s is not a size in bytes"
                        " (1 to 4294967295).\n",
                        optarg);
                return EXIT_FAILURE;
            }
            break;
        default:
            return EXIT_FAILURE;
        }
    }
    if (!args->output_path) {
        args->output_path = "a.aot";
    }
    if (optind == argc) {
        fprintf(stderr, "Error: provide an input file.\n");
        return EXIT_FAILURE;
    }
    if (optind != argc - 1) {
        fprintf(stderr, "Error: invalid number of arguments,"
                        " provide only one input file.\n");
        return EXIT_FAILURE;
    }
    args->input_path = argv[optind];
    return -1;
}

/**
 * file_size() - The size of an open file.
 * @file: the file; its position is left at the start.
 *
 * Return: the size in bytes, or -1 if unknown or above 4 GiB.
 */
static long file_size(FILE *file)
{
    long len = -1;
    if (fseek(file, 0L, SEEK_END) == 0) {
        len = ftell(file);
        if (fseek(file, 0L, SEEK_SET) != 0) {
            len = -1;
        }
    }
    return len < 0 || (unsigned long)len > UINT32_MAX ? -1 : len;
}

/**
 * read_wasm() - Read the whole input file.
 * @path: the file.
 * @len: set to its size in bytes.
 *
 * Return: the contents, to free(); NULL after printing the error.
 */
static uint8_t *read_wasm(const char *path, uint32_t *len)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Error: cannot open %s\n", path);
        return NULL;
    }
    long size = file_size(file);
    if (size < 0) {
        fprintf(stderr, "Error: cannot get the size of %s\n", path);
        fclose(file);
        return NULL;
    }
    *len = (uint32_t)size;
    uint8_t *buf = malloc(*len ? *len : 1);
    if (buf == NULL) {
        fprintf(stderr, "Error: out of memory\n");
        fclose(file);
        return NULL;
    }
    size_t nread = fread(buf, 1, *len, file);
    fclose(file);
    if (nread != *len) {
        fprintf(stderr, "Error: cannot read %s\n", path);
        free(buf);
        return NULL;
    }
    return buf;
}

/**
 * report_error() - Print a compilation error.
 * @args: the command line, for the input path.
 * @err: the error.
 * @res: the result of the failed call, for the function and the memory.
 */
static void report_error(const cli_args *args, wasmc_err_t err,
                         const wasmc_result *res)
{
    fprintf(stderr, "Error: %s: %s", args->input_path, wasmc_strerror(err));
    if (res->func_index != WASMC_NO_FUNC) {
        fprintf(stderr, " (function %u)", res->func_index);
    }
    if (err == WASMC_ERR_OUT_OF_MEMORY && args->work_size) {
        fprintf(stderr, " (--work-size %lu)", args->work_size);
    }
    fputc('\n', stderr);
}

/**
 * write_aot() - Write the AOT file.
 * @path: the file.
 * @buf: its contents.
 * @size: its size in bytes.
 *
 * Return: true on success; false after printing the error.
 */
static bool write_aot(const char *path, const uint8_t *buf, uint32_t size)
{
    FILE *stream = fopen(path, "wb");
    bool good = stream && fwrite(buf, 1, size, stream) == size;
    if (stream && fclose(stream) != 0) {
        good = false;
    }
    if (!good) {
        fprintf(stderr, "Error: cannot write %s\n", path);
    }
    return good;
}

/**
 * print_stats() - Print the --stats summary of a successful compilation.
 * @res: its result.
 */
static void print_stats(const wasmc_result *res)
{
    printf("\nmemory (bytes)\n");
    printf("  work, peak    %10u\n", res->work_peak);
    printf("  AOT file      %10u\n", res->out_size);
}

/* The heap allocator: malloc() aligns to 8 at least. */
static void *heap_work_alloc(void *ctx, uint32_t size)
{
    (void)ctx;
    return malloc(size);
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void heap_work_free(void *ctx, void *ptr, uint32_t size)
{
    (void)ctx;
    (void)size;
    free(ptr);
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void *heap_work_grow(void *ctx, void *ptr, uint32_t old_size,
                            uint32_t new_size)
{
    (void)ctx;
    (void)old_size;
    return realloc(ptr, new_size);
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void *heap_work_shrink(void *ctx, void *ptr, uint32_t old_size,
                              uint32_t new_size)
{
    (void)ctx;
    (void)old_size;
    return realloc(ptr, new_size);
}

/* the heap cannot grow a block in place */
/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static int heap_aux_grow(void *ctx, void *ptr, uint32_t old_size,
                         uint32_t new_size)
{
    (void)ctx;
    (void)ptr;
    (void)old_size;
    (void)new_size;
    return 0;
}

/* NOLINTNEXTLINE(bugprone-easily-swappable-parameters) */
static void *heap_out_grow(void *ctx, void *ptr, uint32_t old_size,
                           uint32_t new_size)
{
    (void)ctx;
    (void)old_size;
    if (!new_size) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, new_size);
}

static const wasmc_allocator heap = {
    .work_alloc = heap_work_alloc,
    .work_free = heap_work_free,
    .work_grow = heap_work_grow,
    .aux_alloc = heap_work_alloc,
    .aux_free = heap_work_free,
    .aux_grow = heap_aux_grow,
    .out_grow = heap_out_grow,
    .work_shrink = heap_work_shrink,
};

/**
 * free_aot() - Give the AOT file of a call back to its allocator.
 * @mem: the allocator.
 * @res: the result of the call.
 */
static void free_aot(const wasmc_allocator *mem, const wasmc_result *res)
{
    if (res->out) {
        mem->out_grow(mem->ctx, res->out, res->out_cap, 0);
    }
}

/**
 * print_table() - Print the --stats table of the functions apart.
 * @args: the command line.
 * @wasm: the module.
 * @len: its size in bytes.
 * @mem: the allocator.
 * @res: the result of the call.
 *
 * With --dump-ir, the library would print each row of the table next to
 * the IR of its function: one more call, without the dumps, prints the
 * table after them, all in one piece.
 *
 * Return: %WASMC_OK, or the error of the call.
 */
static wasmc_err_t print_table(const cli_args *args, const uint8_t *wasm,
                               uint32_t len, const wasmc_allocator *mem,
                               wasmc_result *res)
{
    wasmc_options opt = args->opt;
    opt.dump_ir = 0;
    printf("\n");
    wasmc_err_t err = wasmc_compile(wasm, len, mem, &opt, res);
    free_aot(mem, res);
    return err;
}

/**
 * compile() - Compile the module, and write the AOT file.
 * @args: the command line.
 * @wasm: the module.
 * @len: its size in bytes.
 *
 * The memory comes from the heap, or with --work-size from one buffer of
 * that size. With --dump-ir, the --stats table is printed after the dumps
 * (print_table()).
 *
 * Return: true on success; false after printing the error.
 */
static bool compile(const cli_args *args, const uint8_t *wasm, uint32_t len)
{
    wasmc_allocator mem = heap;
    wasmc_static_arena arena;
    uint8_t *ram = NULL;
    if (args->work_size) {
        ram = args->work_size <= UINT32_MAX ? malloc(args->work_size) : NULL;
        if (!ram) {
            fprintf(stderr, "Error: out of memory\n");
            return false;
        }
        wasmc_static_allocator(&mem, &arena, ram, (uint32_t)args->work_size);
    }
    const bool table_apart = args->opt.stats && args->opt.dump_ir;
    wasmc_options opt = args->opt;
    opt.stats = args->opt.stats && !table_apart;
    wasmc_result res;
    wasmc_err_t err = wasmc_compile(wasm, len, &mem, &opt, &res);
    bool good = false;
    if (err) {
        report_error(args, err, &res);
    } else {
        good = write_aot(args->output_path, res.out, res.out_size);
        free_aot(&mem, &res);
    }
    if (!err && table_apart) {
        wasmc_result tres;
        wasmc_err_t terr = print_table(args, wasm, len, &mem, &tres);
        if (terr) {
            report_error(args, terr, &tres);
            good = false;
        }
    }
    if (!err && args->opt.stats) {
        print_stats(&res);
    }
    free(ram);
    return good;
}

/**
 * main() - The wasmc command.
 * @argc: the number of arguments.
 * @argv: the arguments (see print_usage()).
 *
 * Return: %EXIT_SUCCESS, or %EXIT_FAILURE after printing the error.
 */
int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage(stderr);
        return EXIT_FAILURE;
    }
    cli_args args = {0};
    int status = parse_args(argc, argv, &args);
    if (status >= 0) {
        return status;
    }
    uint32_t len;
    uint8_t *wasm = read_wasm(args.input_path, &len);
    if (!wasm) {
        return EXIT_FAILURE;
    }
    bool good = compile(&args, wasm, len);
    free(wasm);
    return good ? EXIT_SUCCESS : EXIT_FAILURE;
}
