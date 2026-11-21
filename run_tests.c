/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * run_tests.c - WebAssembly 1.0 spec-test runner for wasmc.
 *
 * A riscv32 Linux program (soft-float ilp32, run with qemu-riscv32) with
 * wasmc and the WAMR AOT runtime linked in, so every module is compiled
 * with wasmc_compile() and run with the WAMR C API in the same process.
 * The .wast scripts are converted to .json + .wasm beforehand with
 * wast2json (see `make test`).
 *
 *   run_tests [-v|--verbose] file.json [file.json ...]
 *   run_tests -h|--help
 *
 * Without arguments, prints the help like -h.
 *
 * Prints a line for every failed command ([FAILED]) and every runner error
 * ([ERROR]), then a summary per file and a TOTAL.
 * With -v, every passed command is printed too ([SUCCESS]).
 *
 * One module instance lives until the next module command, so its memory,
 * globals and tables persist across the assertions, as in the spec.
 *
 * Only single-module scripts are supported. The tests/ fork has the
 * multi-module commands (imports, `register`, assert_unlinkable) and the
 * text modules (wasmc only reads the binary format) commented out or
 * rewritten. If one is present anyway, `register`, assert_unlinkable,
 * actions on other (named) modules and text modules are reported as
 * [ERROR]. A module with imports is not detected: WAMR instantiates it
 * with unlinked functions and crashes if one is called.
 *
 * JSON is parsed with jsmn (https://github.com/zserge/jsmn).
 *
 * Exit status (enum exit_code):
 *   0  every check passed; also for -h, --help or no arguments
 *   1  at least one check failed ([FAILED]: wasmc/WAMR gave a wrong
 *      result) and there was no runner error
 *   2  at least one runner error ([ERROR]: run_tests could not run a
 *      command, e.g. unreadable .json or .wasm file, unsupported command,
 *      too many params/results); takes precedence over 1, since some
 *      results are unknown
 *   3  fatal error, nothing (more) was run: bad arguments, out of memory,
 *      WAMR init failed, or wasmc broke its allocator contract (a work
 *      block freed out of LIFO order, or left allocated)
 * With 0, 1 and 2 all files are run and the TOTAL line is printed.
 */

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define JSMN_STATIC
/* jsmn is third-party code, not written for -Wsign-conversion */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "jsmn.h"
#pragma GCC diagnostic pop
#include "wasm_export.h"
#include "wasmc/wasmc.h"

#define STACK_SIZE (64u * 1024u) /* WAMR exec env stack              */
#define MAX_VALS   16            /* params/results per call  */

/* ------------------------------------------------------------------ */
/* Logging                                                            */
/* ------------------------------------------------------------------ */

static struct {
    int passed, failed, errors;
    int files, files_bad; /* total only: files run, files not all passed */
} stats, total;

static const char *file_name;    /* .wast file being run, for messages */
static int color_out, color_err; /* stdout/stderr is a terminal */
static int verbose;              /* -v: also print [SUCCESS] lines */

enum { LOG_SUCCESS, LOG_FAILED, LOG_ERROR };

#define COLOR_WHITE "\033[37m"
#define COLOR_RED   "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_RESET "\033[0m"

/*
 * Counts one result and prints it as "[TAG] file.wast:line: message";
 * [SUCCESS] is printed only with -v. line <= 0 leaves the line out.
 * [ERROR] (a problem of the runner itself, not of the code under test)
 * goes to stderr, the rest to stdout. Colors are used only when the
 * stream is a terminal.
 */
static void report(int kind, long line, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
static void report(int kind, long line, const char *fmt, ...)
{
    static const char *const tags[] = {"SUCCESS", "FAILED", "ERROR"};
    static const char *const colors[] = {COLOR_WHITE, COLOR_RED, COLOR_RED};
    FILE *f = kind == LOG_ERROR ? stderr : stdout;
    int color = kind == LOG_ERROR ? color_err : color_out;

    switch (kind) {
    case LOG_SUCCESS:
        stats.passed++;
        break;
    case LOG_FAILED:
        stats.failed++;
        break;
    default:
        stats.errors++;
        break;
    }
    if (kind == LOG_SUCCESS && !verbose) {
        return;
    }

    if (kind == LOG_ERROR) {
        fflush(stdout); /* keep the order when both go to the same place */
    }
    fprintf(f, "%s[%s]%s%*s", color ? colors[kind] : "", tags[kind],
            color ? COLOR_RESET : "", (int)(8 - strlen(tags[kind])), "");
    if (color) {
        fputs(COLOR_WHITE, f);
    }
    if (file_name && line > 0) {
        fprintf(f, "%s:%ld: ", file_name, line);
    } else if (file_name) {
        fprintf(f, "%s: ", file_name);
    }
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "%s\n", color ? COLOR_RESET : "");
}

#define NAME_WIDTH                                                             \
    30 /* >= skip-stack-guard-page.wast (26) and the                           \
          TOTAL text "71 files, 0 not fully passed" */

static int file_index, file_count; /* progress: file i of n */
static int count_width;            /* digits of file_count */

/*
 * Prints one summary line on stdout, aligned with the report() lines:
 *   "[PASS]    [ 1/71] name          checks passed" or
 *   "[FAIL]    [ 2/71] name          checks passed, N failed, M errors"
 * The [i/n] progress count is printed when index > 0, else blanks
 * (TOTAL line). The failed and errors counts are printed only when they
 * are not 0.
 */
static void summary(const char *tag, int index, const char *name, int passed,
                    int failed, int errors)
{
    int bad = failed || errors;
    const char *color = bad ? COLOR_RED : COLOR_GREEN;
    int progress_width = 2 * count_width + 4; /* "[i/n] " */

    printf("%s[%s]%s%*s", color_out ? color : "", tag,
           color_out ? COLOR_RESET : "", (int)(8 - strlen(tag)), "");
    if (index > 0) {
        printf("[%*d/%d] ", count_width, index, file_count);
    } else {
        printf("%*s", progress_width, "");
    }
    printf("%-*s %6d checks passed", NAME_WIDTH, name, passed);
    if (failed) {
        printf(", %s%d failed%s", color_out ? COLOR_RED : "", failed,
               color_out ? COLOR_RESET : "");
    }
    if (errors) {
        printf(", %s%d error%s%s", color_out ? COLOR_RED : "", errors,
               errors == 1 ? "" : "s", color_out ? COLOR_RESET : "");
    }
    putchar('\n');
}

/* ------------------------------------------------------------------ */
/* Utilities                                                          */
/* ------------------------------------------------------------------ */

/* Exit status of run_tests, see the comment at the top of the file. */
enum exit_code {
    EXIT_PASSED = 0,
    EXIT_FAILED = 1,
    EXIT_ERROR = 2,
    EXIT_FATAL = 3,
};

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) {
        report(LOG_ERROR, 0, "out of memory (%zu bytes)", n);
        exit(EXIT_FATAL);
    }
    return p;
}

/* Reads a whole file; NUL-terminated so it can be parsed as text too. */
static uint8_t *read_file(const char *path, uint32_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    long len = -1;
    if (fseek(f, 0, SEEK_END) == 0) {
        len = ftell(f);
    }
    if (len < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    uint8_t *data = xmalloc((size_t)len + 1);
    if (fread(data, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(data);
        return NULL;
    }
    fclose(f);
    data[len] = '\0';
    *len_out = (uint32_t)len;
    return data;
}

/* ------------------------------------------------------------------ */
/* JSON helpers on top of jsmn                                        */
/* ------------------------------------------------------------------ */

/**
 * struct json_doc_t - A JSON file parsed into jsmn tokens.
 * @js:   File contents, NUL-terminated; tokens point into this buffer.
 * @t:    Token array; t[0] is the root object.
 * @ntok: Number of valid tokens in @t.
 *
 * Tokens are referred to by their index in @t. Helpers that look up a
 * token take or return an index, with -1 meaning "not present".
 */
typedef struct {
    char *js;
    jsmntok_t *t;
    int ntok;
} json_doc_t;

/**
 * json_load() - Read a JSON file and tokenize it.
 * @path: Path of the .json file written by wast2json.
 * @doc:  Document to fill in. Must be zero-initialized by the caller.
 *
 * Runs jsmn twice: once to count the tokens, once to fill a token
 * array of exactly that size.
 *
 * Context: @doc may be partly filled in on failure, so the caller
 * must call json_free() whatever the result.
 *
 * Return: 1 if the file was read and its root is an object, 0 otherwise.
 */
static int json_load(const char *path, json_doc_t *doc)
{
    uint32_t len;
    doc->js = (char *)read_file(path, &len);
    if (!doc->js) {
        return 0;
    }

    jsmn_parser p;
    jsmn_init(&p);
    int count = jsmn_parse(&p, doc->js, len, NULL, 0);
    if (count <= 0) {
        return 0;
    }
    doc->t = xmalloc(sizeof(jsmntok_t) * (size_t)count);
    jsmn_init(&p);
    doc->ntok = jsmn_parse(&p, doc->js, len, doc->t, (unsigned)count);
    return doc->ntok > 0 && doc->t[0].type == JSMN_OBJECT;
}

/**
 * json_free() - Release the memory owned by a document.
 * @doc: Document filled in by json_load().
 */
static void json_free(json_doc_t *doc)
{
    free(doc->js);
    free(doc->t);
}

/**
 * tok_skip() - Skip over a token and all of its children.
 * @doc: Parsed document.
 * @i:   Index of the token to skip.
 *
 * jsmn stores tokens in pre-order, and an object's size counts its keys,
 * so an object has 2 * size children (key, value) and an array has size.
 *
 * Return: Index of the first token after the subtree rooted at @i.
 */
static int tok_skip(const json_doc_t *doc, int i)
{
    int n = doc->t[i].type == JSMN_OBJECT  ? 2 * doc->t[i].size
            : doc->t[i].type == JSMN_ARRAY ? doc->t[i].size
                                           : 0;
    int j = i + 1;
    while (n--) {
        j = tok_skip(doc, j);
    }
    return j;
}

/**
 * tok_eq() - Compare a token's text with a C string.
 * @doc: Parsed document.
 * @i:   Index of the token.
 * @s:   String to compare with.
 *
 * Return: 1 if the token's raw (still escaped) text equals @s, else 0.
 */
static int tok_eq(const json_doc_t *doc, int i, const char *s)
{
    const jsmntok_t *tk = &doc->t[i];
    size_t len = (size_t)(tk->end - tk->start);
    return strlen(s) == len && strncmp(doc->js + tk->start, s, len) == 0;
}

/**
 * obj_get() - Look up a key in a JSON object.
 * @doc: Parsed document.
 * @obj: Index of the object token; may be -1.
 * @key: Key to look for.
 *
 * Return: Index of the value token for @key, or -1 if @obj is -1, is not
 * an object, or has no such key.
 */
static int obj_get(const json_doc_t *doc, int obj, const char *key)
{
    if (obj < 0 || doc->t[obj].type != JSMN_OBJECT) {
        return -1;
    }
    int j = obj + 1;
    for (int k = 0; k < doc->t[obj].size; k++) {
        if (tok_eq(doc, j, key)) {
            return j + 1;
        }
        j = tok_skip(doc, j + 1);
    }
    return -1;
}

/**
 * arr_len() - Number of elements in a JSON array.
 * @doc: Parsed document.
 * @arr: Index of the array token; may be -1.
 *
 * Return: The element count, or 0 if @arr is -1 or not an array.
 */
static int arr_len(const json_doc_t *doc, int arr)
{
    return arr >= 0 && doc->t[arr].type == JSMN_ARRAY ? doc->t[arr].size : 0;
}

/**
 * get_str_slot() - Get the text of obj[key] as a C string.
 * @doc:  Parsed document.
 * @obj:  Index of the object token; may be -1.
 * @key:  Key to look for.
 * @slot: Which of the 4 static buffers to use (0-3).
 *
 * Copies the raw token text (JSON escapes are not decoded; see
 * json_unescape()) into a static buffer, truncated to 1023 bytes.
 * Works for numbers as well as strings.
 *
 * Context: Not reentrant. Each call overwrites the previous result
 * returned for the same @slot; use different slots to keep several
 * results at once.
 *
 * Return: Pointer to the static buffer; "" if the key is absent.
 */
static const char *get_str_slot(const json_doc_t *doc, int obj, const char *key,
                                int slot)
{
    static char bufs[4][1024];
    char *b = bufs[slot];
    int v = obj_get(doc, obj, key);
    size_t len = v < 0 ? 0 : (size_t)(doc->t[v].end - doc->t[v].start);
    if (len >= sizeof bufs[0]) {
        len = sizeof bufs[0] - 1;
    }
    if (v >= 0) {
        memcpy(b, doc->js + doc->t[v].start, len);
    }
    b[len] = '\0';
    return b;
}
/**
 * get_str() - get_str_slot() using slot 0.
 * @doc: Parsed document.
 * @obj: Index of the object token.
 * @key: Key to look for.
 */
#define get_str(doc, obj, key) get_str_slot(doc, obj, key, 0)

/**
 * has_key() - Check whether a JSON object has a key.
 * @doc: Parsed document.
 * @obj: Index of the object token; may be -1.
 * @key: Key to look for.
 *
 * Return: 1 if obj[key] exists, else 0.
 */
static int has_key(const json_doc_t *doc, int obj, const char *key)
{
    return obj_get(doc, obj, key) >= 0;
}

/**
 * json_unescape() - Decode a JSON string body to UTF-8.
 * @s:   String text without the surrounding quotes, NUL-terminated.
 * @out: Output buffer.
 * @cap: Size of @out in bytes.
 *
 * Handles the standard escapes and \uXXXX, including UTF-16 surrogate
 * pairs. Export names in the spec tests use these escapes for unusual
 * characters, including NUL. Output stops early if fewer than 5 bytes
 * of room are left, and is always NUL-terminated.
 *
 * Return: Number of bytes written, not counting the final NUL. Use it
 * instead of strlen(), since the decoded name may contain NUL bytes.
 */
static size_t json_unescape(const char *s, char *out, size_t cap)
{
    size_t n = 0;
    while (*s && n + 4 < cap) {
        if (*s != '\\') {
            out[n++] = *s++;
            continue;
        }
        s++;
        char c = *s++;
        switch (c) {
        case 'n':
            out[n++] = '\n';
            break;
        case 't':
            out[n++] = '\t';
            break;
        case 'r':
            out[n++] = '\r';
            break;
        case 'b':
            out[n++] = '\b';
            break;
        case 'f':
            out[n++] = '\f';
            break;
        case 'u': {
            char hex[5] = {0};
            memcpy(hex, s, 4);
            s += 4;
            uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
            if (cp >= 0xD800 && cp < 0xDC00 && s[0] == '\\' && s[1] == 'u') {
                memcpy(hex, s + 2, 4);
                s += 6;
                cp = 0x10000 + ((cp - 0xD800) << 10) +
                     ((uint32_t)strtoul(hex, NULL, 16) - 0xDC00);
            }
            if (cp < 0x80) {
                out[n++] = (char)cp;
            } else if (cp < 0x800) {
                out[n++] = (char)(0xC0 | (cp >> 6));
                out[n++] = (char)(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out[n++] = (char)(0xE0 | (cp >> 12));
                out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                out[n++] = (char)(0x80 | (cp & 0x3F));
            } else {
                out[n++] = (char)(0xF0 | (cp >> 18));
                out[n++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                out[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                out[n++] = (char)(0x80 | (cp & 0x3F));
            }
            break;
        }
        default:
            out[n++] = c;
            break; /* \" \\ \/ */
        }
    }
    out[n] = '\0';
    return n;
}

/* ------------------------------------------------------------------ */
/* Runner state                                                       */
/* ------------------------------------------------------------------ */

static char json_dir[512];

/* The current module: compiled, loaded and instantiated once. */
typedef struct {
    int state;      /* CUR_xxx                                   */
    char name[256]; /* $name of the module command, or ""        */
    uint8_t *aot;   /* AOT file, alive as long as the module is  */
    wasm_module_t module;
    wasm_module_inst_t inst;
    wasm_exec_env_t env;
} module_t;
static module_t cur;

enum { CUR_NONE, CUR_READY, CUR_BROKEN };

static void module_close(void)
{
    if (cur.env) {
        wasm_runtime_destroy_exec_env(cur.env);
    }
    if (cur.inst) {
        wasm_runtime_deinstantiate(cur.inst);
    }
    if (cur.module) {
        wasm_runtime_unload(cur.module);
    }
    free(cur.aot);
    memset(&cur, 0, sizeof cur);
}

/* Reads a module file written by wast2json; [ERROR] if it cannot. */
static uint8_t *read_wasm(const char *filename, long line, uint32_t *len)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s%s", json_dir, filename);
    uint8_t *wasm = read_file(path, len);
    if (!wasm) {
        report(LOG_ERROR, line, "cannot read %s", path);
    }
    return wasm;
}

/*
 * wasmc's memory, from the heap (malloc() aligns to 8 at least). The live
 * work and aux blocks are kept on two stacks, to check wasmc's contract:
 * every free is of the newest live block of its stack, with its size, only
 * the newest work block grows, the output never grows while an aux block
 * is live, and no block is left after the call. Growing a work block
 * always moves it, so that wasmc's handling of a moved block is tested; an
 * aux block grows in place into AUX_SLACK spare bytes, then fails, so that
 * both outcomes are.
 */
#define MAX_BLOCKS 4096
#define AUX_SLACK  512u

typedef struct {
    void *ptr[MAX_BLOCKS];
    uint32_t size[MAX_BLOCKS];
    unsigned count;
} block_stack;

static block_stack work_blocks, aux_blocks;

static void contract_broken(const char *what)
{
    report(LOG_ERROR, 0, "wasmc allocator contract: %s", what);
    exit(EXIT_FATAL);
}

static void *stack_alloc(block_stack *stack, uint32_t size)
{
    if (stack->count == MAX_BLOCKS) {
        contract_broken("too many live blocks");
    }
    if (!size) {
        contract_broken("block of 0 bytes");
    }
    void *ptr = malloc(size);
    if (ptr) {
        stack->ptr[stack->count] = ptr;
        stack->size[stack->count++] = size;
    }
    return ptr;
}

static void stack_free(block_stack *stack, void *ptr, uint32_t size)
{
    if (!stack->count || stack->ptr[stack->count - 1] != ptr ||
        stack->size[stack->count - 1] != size) {
        contract_broken("block freed out of LIFO order");
    }
    stack->count--;
    free(ptr);
}

static void *heap_work_alloc(void *ctx, uint32_t size)
{
    (void)ctx;
    return stack_alloc(&work_blocks, size);
}

static void heap_work_free(void *ctx, void *ptr, uint32_t size)
{
    (void)ctx;
    stack_free(&work_blocks, ptr, size);
}

static void *heap_work_grow(void *ctx, void *ptr, uint32_t old_size,
                            uint32_t new_size)
{
    (void)ctx;
    block_stack *stack = &work_blocks;
    if (!stack->count || stack->ptr[stack->count - 1] != ptr ||
        stack->size[stack->count - 1] != old_size || new_size <= old_size) {
        contract_broken("work_grow() of a block that is not the newest");
    }
    void *grown = malloc(new_size);
    if (grown) {
        memcpy(grown, ptr, old_size);
        free(ptr);
        stack->ptr[stack->count - 1] = grown;
        stack->size[stack->count - 1] = new_size;
    }
    return grown;
}

/* moves the block, as a heap may: stale pointers into it then fail */
static void *heap_work_shrink(void *ctx, void *ptr, uint32_t old_size,
                              uint32_t new_size)
{
    (void)ctx;
    block_stack *stack = &work_blocks;
    if (!stack->count || stack->ptr[stack->count - 1] != ptr ||
        stack->size[stack->count - 1] != old_size || !new_size ||
        new_size >= old_size) {
        contract_broken("work_shrink() of a block that is not the newest");
    }
    void *shrunk = malloc(new_size);
    if (shrunk) {
        memcpy(shrunk, ptr, new_size);
        memset(ptr, 0xA5, old_size); /* poison the old copy */
        free(ptr);
        stack->ptr[stack->count - 1] = shrunk;
        stack->size[stack->count - 1] = new_size;
    }
    return shrunk;
}

/* the size each live aux block was allocated with */
static uint32_t aux_first_size[MAX_BLOCKS];

static void *heap_aux_alloc(void *ctx, uint32_t size)
{
    (void)ctx;
    void *ptr = stack_alloc(&aux_blocks, size);
    if (ptr) { /* the spare bytes it may grow into */
        aux_first_size[aux_blocks.count - 1] = size;
        void *spare = realloc(ptr, (size_t)size + AUX_SLACK);
        if (!spare) {
            aux_blocks.count--;
            free(ptr);
            return NULL;
        }
        aux_blocks.ptr[aux_blocks.count - 1] = spare;
        ptr = spare;
    }
    return ptr;
}

static int heap_aux_grow(void *ctx, void *ptr, uint32_t old_size,
                         uint32_t new_size)
{
    (void)ctx;
    block_stack *stack = &aux_blocks;
    const unsigned top = stack->count - 1;
    if (!stack->count || stack->ptr[top] != ptr ||
        stack->size[top] != old_size || new_size <= old_size) {
        contract_broken("aux_grow() of a block that is not the newest");
    }
    if (new_size > aux_first_size[top] + AUX_SLACK) {
        return 0;
    }
    stack->size[top] = new_size;
    return 1;
}

static void heap_aux_free(void *ctx, void *ptr, uint32_t size)
{
    (void)ctx;
    stack_free(&aux_blocks, ptr, size);
}

static void *heap_out_grow(void *ctx, void *ptr, uint32_t old_size,
                           uint32_t new_size)
{
    (void)ctx;
    (void)old_size;
    if (aux_blocks.count) {
        contract_broken("output resized while an aux block is live");
    }
    if (!new_size) {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, new_size);
}

/*
 * Compiles a wasm binary with wasmc, its memory from the heap. Returns the
 * AOT file, malloc'd, or NULL with the error in err.
 */
static uint8_t *compile(const uint8_t *wasm, uint32_t len, uint32_t *aot_len,
                        char *err, size_t err_len)
{
    static const wasmc_allocator heap = {.work_alloc = heap_work_alloc,
                                         .work_free = heap_work_free,
                                         .work_grow = heap_work_grow,
                                         .aux_alloc = heap_aux_alloc,
                                         .aux_free = heap_aux_free,
                                         .aux_grow = heap_aux_grow,
                                         .out_grow = heap_out_grow,
                                         .work_shrink = heap_work_shrink};
    wasmc_result res;
    wasmc_err_t e = wasmc_compile(wasm, len, &heap, NULL, &res);
    if (work_blocks.count || aux_blocks.count) {
        contract_broken("blocks left after the call");
    }
    if (e != WASMC_OK && res.func_index == WASMC_NO_FUNC) {
        snprintf(err, err_len, "%s", wasmc_strerror(e));
        return NULL;
    }
    if (e != WASMC_OK) {
        snprintf(err, err_len, "%s (function %" PRIu32 ")", wasmc_strerror(e),
                 res.func_index);
        return NULL;
    }
    *aot_len = res.out_size;
    return res.out;
}

/* Compile + load + instantiate. Returns 1 on success, else the error in err. */
static int instantiate(const uint8_t *wasm, uint32_t len, char *err,
                       size_t err_len)
{
    uint32_t aot_len;
    cur.aot = compile(wasm, len, &aot_len, err, err_len);
    if (!cur.aot) {
        return 0;
    }
    cur.module = wasm_runtime_load(cur.aot, aot_len, err, (uint32_t)err_len);
    if (!cur.module) {
        return 0;
    }
    cur.inst = wasm_runtime_instantiate(cur.module, STACK_SIZE, 0, err,
                                        (uint32_t)err_len);
    if (!cur.inst) {
        return 0;
    }
    cur.env = wasm_runtime_create_exec_env(cur.inst, STACK_SIZE);
    if (!cur.env) {
        snprintf(err, err_len, "cannot create exec env");
        return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Commands                                                           */
/* ------------------------------------------------------------------ */

static void cmd_module(const json_doc_t *doc, int cmd, long line)
{
    module_close();
    snprintf(cur.name, sizeof(cur.name), "%s", get_str(doc, cmd, "name"));

    uint32_t len;
    const char *filename = get_str(doc, cmd, "filename");
    uint8_t *wasm = read_wasm(filename, line, &len);
    char err[256] = "";
    if (!wasm) {
        cur.state = CUR_BROKEN;
    } else if (!instantiate(wasm, len, err, sizeof(err))) {
        cur.state = CUR_BROKEN;
        report(LOG_FAILED, line, "module %s: %s", filename, err);
    } else {
        cur.state = CUR_READY;
        report(LOG_SUCCESS, line, "module %s compiled and instantiated",
               filename);
    }
    free(wasm);
}

/* assert_invalid, assert_malformed: wasmc must reject the module. */
static void cmd_reject(const json_doc_t *doc, int cmd, long line)
{
    const char *type = get_str_slot(doc, cmd, "type", 1);
    const char *filename = get_str(doc, cmd, "filename");
    const char *text = get_str_slot(doc, cmd, "text", 2);
    if (strcmp(get_str_slot(doc, cmd, "module_type", 3), "binary") != 0) {
        report(LOG_ERROR, line,
               "%s %s: text module not supported, wasmc only reads the "
               "binary format",
               type, filename);
        return;
    }
    uint32_t len, aot_len;
    uint8_t *wasm = read_wasm(filename, line, &len);
    if (!wasm) {
        return;
    }
    char err[256];
    uint8_t *aot = compile(wasm, len, &aot_len, err, sizeof err);
    if (aot) {
        report(LOG_FAILED, line, "%s %s: compiled, expected rejection \"%s\"",
               type, filename, text);
    } else {
        report(LOG_SUCCESS, line, "%s %s rejected: %s (expected \"%s\")", type,
               filename, err, text);
    }
    free(aot);
    free(wasm);
}

/* assert_uninstantiable: compiles, but instantiation fails (start trap,
   segment out of bounds). The module does not become current. */
static void cmd_uninstantiable(const json_doc_t *doc, int cmd, long line)
{
    uint32_t len;
    const char *filename = get_str(doc, cmd, "filename");
    const char *text = get_str_slot(doc, cmd, "text", 1);
    uint8_t *wasm = read_wasm(filename, line, &len);
    if (!wasm) {
        return;
    }
    /* use a scratch slot, then restore the current module */
    module_t saved = cur;
    memset(&cur, 0, sizeof(cur));
    char err[256] = "";
    if (instantiate(wasm, len, err, sizeof(err))) {
        report(LOG_FAILED, line,
               "assert_uninstantiable %s: instantiated, expected \"%s\"",
               filename, text);
    } else if (cur.module) {
        /* failed at instantiation, not compile/load */
        report(LOG_SUCCESS, line,
               "assert_uninstantiable %s: instantiation failed: %s "
               "(expected \"%s\")",
               filename, err, text);
    } else {
        report(LOG_FAILED, line,
               "assert_uninstantiable %s: failed before instantiation: %s",
               filename, err);
    }
    module_close();
    cur = saved;
    free(wasm);
}

/* Value from the JSON {"type","value"}: raw bits; nan:* reported in *nan. */
static uint64_t parse_value(const json_doc_t *doc, int v, wasm_valkind_t *kind,
                            int *nan)
{
    const char *type = get_str_slot(doc, v, "type", 2);
    const char *value = get_str_slot(doc, v, "value", 3);
    *kind = !strcmp(type, "i32")   ? WASM_I32
            : !strcmp(type, "i64") ? WASM_I64
            : !strcmp(type, "f32") ? WASM_F32
                                   : WASM_F64;
    *nan = !strcmp(value, "nan:canonical")    ? 1
           : !strcmp(value, "nan:arithmetic") ? 2
                                              : 0;
    return strtoull(value, NULL, 10);
}

static uint64_t val_bits(const wasm_val_t *v)
{
    switch (v->kind) {
    case WASM_I32:
        return (uint32_t)v->of.i32;
    case WASM_I64:
        return (uint64_t)v->of.i64;
    case WASM_F32: {
        uint32_t b;
        memcpy(&b, &v->of.f32, 4);
        return b;
    }
    default: {
        uint64_t b;
        memcpy(&b, &v->of.f64, 8);
        return b;
    }
    }
}

static const char *kind_name(wasm_valkind_t k)
{
    return k == WASM_I32   ? "i32"
           : k == WASM_I64 ? "i64"
           : k == WASM_F32 ? "f32"
           : k == WASM_F64 ? "f64"
                           : "?";
}

/* Does the result match the expected JSON value (incl. the NaN rules)? */
static int value_matches(const json_doc_t *doc, int exp, const wasm_val_t *got)
{
    wasm_valkind_t kind;
    int nan;
    uint64_t want = parse_value(doc, exp, &kind, &nan);
    uint64_t bits = val_bits(got);
    if (got->kind != kind) {
        return 0;
    }
    if (nan && kind == WASM_F32) {
        uint32_t m = nan == 1 ? 0x7FFFFFFFu : 0x7FC00000u;
        return (bits & m) == 0x7FC00000u;
    }
    if (nan && kind == WASM_F64) {
        uint64_t m = nan == 1 ? 0x7FFFFFFFFFFFFFFFull : 0x7FF8000000000000ull;
        return (bits & m) == 0x7FF8000000000000ull;
    }
    return bits == want;
}

enum { ACT_OK, ACT_TRAP, ACT_FAIL, ACT_ERROR };

/*
 * Performs an action (invoke or get) on the current module.
 * Returns ACT_OK with the results, or ACT_TRAP (exception text in msg),
 * ACT_FAIL (test failure, e.g. missing export) or ACT_ERROR (runner
 * limit, e.g. an action on another module), with the reason in msg.
 */
static int do_action(const json_doc_t *doc, int action, wasm_val_t *res,
                     uint32_t *nres, char *msg, size_t msg_len)
{
    if (has_key(doc, action, "module") &&
        strcmp(get_str(doc, action, "module"), cur.name)) {
        snprintf(msg, msg_len,
                 "action on module %.100s not supported, only on the "
                 "current module",
                 get_str(doc, action, "module"));
        return ACT_ERROR;
    }
    if (cur.state != CUR_READY) {
        snprintf(msg, msg_len, "no module: the last module command failed");
        return ACT_FAIL;
    }

    char name[1024];
    json_unescape(get_str(doc, action, "field"), name, sizeof name);

    if (!strcmp(get_str(doc, action, "type"), "get")) {
        wasm_global_inst_t g;
        if (!wasm_runtime_get_export_global_inst(cur.inst, name, &g)) {
            snprintf(msg, msg_len, "no global export \"%.200s\"", name);
            return ACT_FAIL;
        }
        res[0].kind = g.kind;
        memcpy(&res[0].of, g.global_data,
               g.kind == WASM_I32 || g.kind == WASM_F32 ? 4 : 8);
        *nres = 1;
        return ACT_OK;
    }

    wasm_function_inst_t fn = wasm_runtime_lookup_function(cur.inst, name);
    if (!fn) {
        snprintf(msg, msg_len, "no function export \"%.200s\"", name);
        return ACT_FAIL;
    }

    int args = obj_get(doc, action, "args");
    uint32_t nargs = (uint32_t)arr_len(doc, args);
    wasm_val_t argv[MAX_VALS];
    if (nargs > MAX_VALS ||
        wasm_func_get_result_count(fn, cur.inst) > MAX_VALS) {
        snprintf(msg, msg_len, "more than %d params or results", MAX_VALS);
        return ACT_ERROR;
    }
    for (uint32_t k = 0, a = (uint32_t)args + 1; k < nargs;
         k++, a = (uint32_t)tok_skip(doc, (int)a)) {
        int nan;
        uint64_t bits = parse_value(doc, (int)a, &argv[k].kind, &nan);
        memset(&argv[k].of, 0, sizeof argv[k].of);
        memcpy(&argv[k].of, &bits,
               argv[k].kind == WASM_I32 || argv[k].kind == WASM_F32 ? 4 : 8);
    }

    *nres = wasm_func_get_result_count(fn, cur.inst);
    wasm_valkind_t kinds[MAX_VALS];
    wasm_func_get_result_types(fn, cur.inst, kinds);
    if (!wasm_runtime_call_wasm_a(cur.env, fn, *nres, res, nargs, argv)) {
        const char *e = wasm_runtime_get_exception(cur.inst);
        snprintf(msg, msg_len, "%s", e ? e : "?");
        wasm_runtime_clear_exception(cur.inst);
        return ACT_TRAP;
    }
    for (uint32_t k = 0; k < *nres; k++) {
        res[k].kind = kinds[k];
    }
    return ACT_OK;
}

/* Results as "i32:0x2a f64:0x0", or "nothing". */
static void format_results(const wasm_val_t *res, uint32_t n, char *out,
                           size_t len)
{
    size_t pos = 0;
    snprintf(out, len, "nothing");
    for (uint32_t k = 0; k < n && pos < len; k++) {
        pos += (size_t)snprintf(out + pos, len - pos, "%s%s:0x%" PRIx64,
                                k ? " " : "", kind_name(res[k].kind),
                                val_bits(&res[k]));
    }
}

/* A JSON value array, formatted like format_results(). */
static void format_values(const json_doc_t *doc, int arr, char *out, size_t len)
{
    size_t pos = 0;
    int n = arr_len(doc, arr);
    snprintf(out, len, "%s", n ? "" : "nothing");
    for (int k = 0, v = arr + 1; k < n && pos < len;
         k++, v = tok_skip(doc, v)) {
        wasm_valkind_t kind;
        int nan;
        uint64_t bits = parse_value(doc, v, &kind, &nan);
        const char *sep = k ? " " : "";
        if (nan) {
            pos += (size_t)snprintf(out + pos, len - pos, "%s%s:%s", sep,
                                    kind_name(kind),
                                    get_str_slot(doc, v, "value", 3));
        } else {
            pos += (size_t)snprintf(out + pos, len - pos, "%s%s:0x%" PRIx64,
                                    sep, kind_name(kind), bits);
        }
    }
}

/* assert_return, assert_trap, assert_exhaustion, action */
static void cmd_action(const json_doc_t *doc, int cmd, long line,
                       const char *type)
{
    int action = obj_get(doc, cmd, "action");
    wasm_val_t res[MAX_VALS];
    uint32_t nres = 0;
    char msg[256] = "";
    int r = do_action(doc, action, res, &nres, msg, sizeof msg);

    /* what = "assert_return fac-rec(i64:0x19)" or "assert_return get e" */
    char what[600], args[400];
    if (!strcmp(get_str(doc, action, "type"), "get")) {
        snprintf(what, sizeof what, "%s get %s", type,
                 get_str(doc, action, "field"));
    } else {
        format_values(doc, obj_get(doc, action, "args"), args, sizeof args);
        snprintf(what, sizeof what, "%s %s(%s)", type,
                 get_str(doc, action, "field"),
                 arr_len(doc, obj_get(doc, action, "args")) ? args : "");
    }

    char got[256];
    format_results(res, nres, got, sizeof got);
    switch (r) {
    case ACT_FAIL:
        report(LOG_FAILED, line, "%s: %s", what, msg);
        return;
    case ACT_ERROR:
        report(LOG_ERROR, line, "%s: %s", what, msg);
        return;
    }

    if (!strcmp(type, "action")) {
        if (r == ACT_OK) {
            report(LOG_SUCCESS, line, "%s returned %s", what, got);
        } else {
            report(LOG_FAILED, line, "%s: unexpected trap: %s", what, msg);
        }
        return;
    }

    if (!strcmp(type, "assert_return")) {
        int expected = obj_get(doc, cmd, "expected");
        char want[256];
        format_values(doc, expected, want, sizeof want);
        if (r == ACT_TRAP) {
            report(LOG_FAILED, line, "%s: expected %s, got trap: %s", what,
                   want, msg);
            return;
        }
        int ok = (uint32_t)arr_len(doc, expected) == nres;
        for (uint32_t k = 0, e = (uint32_t)expected + 1; ok && k < nres;
             k++, e = (uint32_t)tok_skip(doc, (int)e)) {
            ok = value_matches(doc, (int)e, &res[k]);
        }
        if (ok) {
            report(LOG_SUCCESS, line, "%s returned %s", what, got);
        } else {
            report(LOG_FAILED, line, "%s: expected %s, got %s", what, want,
                   got);
        }
        return;
    }

    /* assert_trap / assert_exhaustion: the WAMR message must contain the
       spec text; stack exhaustion is reported as "... stack overflow". */
    const char *text = get_str_slot(doc, cmd, "text", 1);
    char want[256];
    snprintf(want, sizeof want, "%s",
             !strcmp(type, "assert_exhaustion") ? "stack overflow" : text);
    /* "uninitialized element 7": WAMR does not report the index */
    size_t n = strlen(want);
    while (n > 0 && want[n - 1] >= '0' && want[n - 1] <= '9') {
        n--;
    }
    if (n < strlen(want) && n > 0 && want[n - 1] == ' ') {
        want[n - 1] = '\0';
    }
    if (r == ACT_OK) {
        report(LOG_FAILED, line, "%s: expected trap \"%s\", returned %s", what,
               text, got);
    } else if (!strstr(msg, want)) {
        report(LOG_FAILED, line, "%s: expected trap \"%s\", got \"%s\"", what,
               text, msg);
    } else {
        report(LOG_SUCCESS, line, "%s trapped: %s (expected \"%s\")", what, msg,
               text);
    }
}

/* Runs one wast2json .json file. Returns 0 if it cannot be read. */
static int run_file(const char *path)
{
    json_doc_t doc = {0};
    memset(&stats, 0, sizeof stats);
    file_name = path;
    if (!json_load(path, &doc)) {
        report(LOG_ERROR, 0, "cannot read or parse the file");
        fflush(stderr);
        summary("FAIL", file_index,
                strrchr(path, '/') ? strrchr(path, '/') + 1 : path, 0, 0,
                stats.errors);
        total.errors += stats.errors;
        total.files++;
        total.files_bad++;
        json_free(&doc);
        file_name = NULL;
        return 0;
    }

    const char *slash = strrchr(path, '/');
    size_t dlen = slash ? (size_t)(slash - path + 1) : 0;
    snprintf(json_dir, sizeof json_dir, "%.*s", (int)dlen, path);

    char name[256];
    const char *src = get_str(&doc, 0, "source_filename");
    snprintf(name, sizeof name, "%s",
             strrchr(src, '/') ? strrchr(src, '/') + 1 : src);
    file_name = name;

    int commands = obj_get(&doc, 0, "commands");
    int ncmds = arr_len(&doc, commands);
    int cmd = commands + 1;
    for (int k = 0; k < ncmds; k++, cmd = tok_skip(&doc, cmd)) {
        char type[64];
        snprintf(type, sizeof type, "%s", get_str(&doc, cmd, "type"));
        long line = strtol(get_str(&doc, cmd, "line"), NULL, 10);

        if (!strcmp(type, "module")) {
            cmd_module(&doc, cmd, line);
        } else if (!strcmp(type, "assert_invalid") ||
                   !strcmp(type, "assert_malformed")) {
            cmd_reject(&doc, cmd, line);
        } else if (!strcmp(type, "assert_uninstantiable")) {
            cmd_uninstantiable(&doc, cmd, line);
        } else if (!strcmp(type, "assert_return") ||
                   !strcmp(type, "assert_trap") ||
                   !strcmp(type, "assert_exhaustion") ||
                   !strcmp(type, "action")) {
            cmd_action(&doc, cmd, line, type);
        } else {
            /* register, assert_unlinkable: need multi-module linking */
            report(LOG_ERROR, line, "%s: command not supported by run_tests",
                   type);
        }
    }
    module_close();

    fflush(stderr);
    summary(stats.failed || stats.errors ? "FAIL" : "PASS", file_index, name,
            stats.passed, stats.failed, stats.errors);
    total.files++;
    total.files_bad += stats.failed || stats.errors;
    total.passed += stats.passed;
    total.failed += stats.failed;
    total.errors += stats.errors;
    json_free(&doc);
    file_name = NULL;
    return 1;
}

/**
 * usage() - Print the command line help.
 * @out: stream to print to (stdout for -h, stderr for a usage error)
 */
static void usage(FILE *out)
{
    fputs("usage: run_tests [-v|--verbose] file.json [file.json ...]\n"
          "       run_tests -h|--help\n"
          "\n"
          "Run WebAssembly spec tests (wast2json output) with wasmc and "
          "WAMR.\n"
          "\n"
          "options:\n"
          "  -v, --verbose  also print every passed command ([SUCCESS])\n"
          "  -h, --help     print this help and exit\n"
          "  --             end of options\n"
          "\n"
          "exit status:\n"
          "  0  every command passed\n"
          "  1  a check failed ([FAILED]: wasmc/WAMR gave a wrong result)\n"
          "  2  a runner error ([ERROR]: run_tests could not run a\n"
          "     command, e.g. unreadable file or unsupported command);\n"
          "     wins over 1, since some results are unknown\n"
          "  3  fatal: bad arguments, out of memory or WAMR init failed\n",
          out);
}

int main(int argc, char **argv)
{
    color_out = isatty(STDOUT_FILENO);
    color_err = isatty(STDERR_FILENO);
    if (argc < 2) {
        usage(stdout);
        return EXIT_PASSED;
    }
    int first = 1;
    for (; first < argc && argv[first][0] == '-'; first++) {
        if (!strcmp(argv[first], "-v") || !strcmp(argv[first], "--verbose")) {
            verbose = 1;
        } else if (!strcmp(argv[first], "-h") ||
                   !strcmp(argv[first], "--help")) {
            usage(stdout);
            return EXIT_PASSED;
        } else if (!strcmp(argv[first], "--")) {
            first++;
            break;
        } else {
            report(LOG_ERROR, 0, "unknown option %s", argv[first]);
            usage(stderr);
            return EXIT_FATAL;
        }
    }
    if (first >= argc) {
        report(LOG_ERROR, 0, "no input files");
        usage(stderr);
        return EXIT_FATAL;
    }
    if (!wasm_runtime_init()) {
        report(LOG_ERROR, 0, "WAMR runtime init failed");
        return EXIT_FATAL;
    }
    wasm_runtime_set_log_level(WASM_LOG_LEVEL_FATAL);

    file_count = argc - first;
    count_width = snprintf(NULL, 0, "%d", file_count);
    for (int i = first; i < argc; i++) {
        file_index = i - first + 1;
        run_file(argv[i]);
    }

    char files[64];
    snprintf(files, sizeof files, "%d files, %d not fully passed", total.files,
             total.files_bad);
    /* as wide as a summary() line up to "checks passed": "[TAG]" padded
       to 10, "[i/n] ", name, " %6d checks passed" (21) */
    int width = 10 + 2 * count_width + 4 + NAME_WIDTH + 21;
    for (int k = 0; k < width; k++) {
        putchar('-');
    }
    putchar('\n');
    summary("TOTAL", 0, files, total.passed, total.failed, total.errors);
    wasm_runtime_destroy();
    if (total.errors) {
        return EXIT_ERROR;
    }
    return total.failed ? EXIT_FAILED : EXIT_PASSED;
}
