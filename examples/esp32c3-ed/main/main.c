/*
 * wasmc - WebAssembly to 32-bit RISC-V on-device AOT compiler WAMR compatible
 * Copyright (C) 2025-2026  Luca Orlandello
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/*
 * main.c - V7 ed on the ESP32-C3, from a terminal over the serial line.
 *
 * At every boot wasmc_compile() turns ed.wasm, which stays in flash, into a
 * WAMR AOT file in a heap buffer. WAMR loads it and ed runs: its host functions
 * (see ../wasm/port.c) are the terminal on UART0 and the files on a LittleFS
 * partition, mounted at /fs.
 */
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "wasm_export.h"
#include "wasmc/error.h"
#include "wasmc/wasmc.h"

#include "ed_wasm.h"

#define TTY_UART   UART_NUM_0
#define TTY_RX_BUF 1024 /* a pasted line must not be lost while ed works */

#define FS_ROOT      "/fs"
#define FS_PARTITION "storage"

/* wasmc's memory, for one compilation: work memory and AOT file (41 KB) */
#define WASMC_RAM (48 * 1024)

/* WAMR's exec env and the thread that runs wasmc, WAMR, ed and LittleFS */
#define WASM_STACK_SIZE      (8 * 1024)
#define ED_THREAD_STACK_SIZE (20 * 1024)

#define EXC_THROW "app: longjmp"
#define EXC_EXIT  "app: exit"

static wasm_module_inst_t g_inst;

/*
 * The terminal: what a Unix kernel does for ed on a tty. Input is edited a
 * line at a time and echoed here; output turns \n into \r\n.
 */
#define TTY_MAX_LINE 509                /* the most that ed takes on a line */
static char tty_line[TTY_MAX_LINE + 1]; /* with its \n */
static int tty_len;
static int tty_pos;

static void tty_put(const char *s, int n)
{
    uart_write_bytes(TTY_UART, s, (size_t)n);
}

static void tty_write(const char *s, int n)
{
    int start = 0;
    for (int i = 0; i < n; i++) {
        if (s[i] == '\n') {
            tty_put(s + start, i - start);
            tty_put("\r\n", 2);
            start = i + 1;
        }
    }
    tty_put(s + start, n - start);
}

static void tty_printf(const char *fmt, ...)
{
    char buf[160];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, args);
    va_end(args);
    if (n >= (int)sizeof buf) {
        n = sizeof buf - 1;
    }
    tty_write(buf, n);
}

/* Reads a line into tty_line. Returns false on Ctrl-D at its start (EOF). */
static bool tty_fill(void)
{
    static bool last_cr;
    uint8_t c;

    tty_len = 0;
    tty_pos = 0;
    for (;;) {
        if (uart_read_bytes(TTY_UART, &c, 1, portMAX_DELAY) != 1) {
            continue;
        }
        bool after_cr = last_cr;
        last_cr = c == '\r';
        if (c == '\n' && after_cr) {
            continue; /* the second half of a \r\n */
        }
        if (c == '\r' || c == '\n') {
            tty_line[tty_len++] = '\n';
            tty_put("\r\n", 2);
            return true;
        }
        if (c == 0x7f || c == '\b') { /* erase */
            if (tty_len > 0) {
                tty_len--;
                tty_put("\b \b", 3);
            }
        } else if (c == 0x15) { /* Ctrl-U: kill the line */
            while (tty_len > 0) {
                tty_len--;
                tty_put("\b \b", 3);
            }
        } else if (c == 0x04) { /* Ctrl-D */
            if (tty_len == 0) {
                return false;
            }
        } else if ((c >= 0x20 && c < 0x7f) || c == '\t') {
            /* a longer line would be an error in ed's gettty() */
            if (tty_len < TTY_MAX_LINE) {
                tty_line[tty_len++] = (char)c;
                tty_put((const char *)&c, 1);
            } else {
                tty_put("\a", 1);
            }
        } else if (c >= 0x80) {
            tty_put("\a", 1); /* ed is 7-bit: no UTF-8 */
        }
    }
}

static int tty_read(char *buf, int n)
{
    if (tty_pos == tty_len && !tty_fill()) {
        return 0;
    }
    int avail = tty_len - tty_pos;
    if (n > avail) {
        n = avail;
    }
    memcpy(buf, tty_line + tty_pos, (size_t)n);
    tty_pos += n;
    return n;
}

/* ed's names are relative to /fs; /tmp/e00000 is /fs/tmp/e00000. */
static bool fs_path(const char *name, char *path, size_t size)
{
    while (*name == '/') {
        name++;
    }
    return snprintf(path, size, FS_ROOT "/%s", name) < (int)size;
}

/* Below 3 are the terminal: ed never gets them from a file. */
static int fs_open(const char *name, int flags)
{
    char path[96];
    if (!fs_path(name, path, sizeof path)) {
        return -1;
    }
    int fd = open(path, flags, 0666);
    if (fd >= 0 && fd < 3) {
        close(fd);
        return -1;
    }
    return fd;
}

static int32_t sys_read(wasm_exec_env_t env, int32_t fd, char *buf, uint32_t n)
{
    (void)env;
    return fd == 0 ? tty_read(buf, (int)n) : (int32_t)read(fd, buf, n);
}

static int32_t sys_write(wasm_exec_env_t env, int32_t fd, const char *buf,
                         uint32_t n)
{
    (void)env;
    if (fd == 1 || fd == 2) {
        tty_write(buf, (int)n);
        return (int32_t)n;
    }
    return (int32_t)write(fd, buf, n);
}

static int32_t sys_open(wasm_exec_env_t env, const char *name, int32_t mode)
{
    (void)env;
    return fs_open(name, mode & (O_RDONLY | O_WRONLY | O_RDWR));
}

static int32_t sys_creat(wasm_exec_env_t env, const char *name, int32_t mode)
{
    (void)env;
    (void)mode;
    return fs_open(name, O_WRONLY | O_CREAT | O_TRUNC);
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
    return fd > 2 ? (int32_t)lseek(fd, off, whence) : -1;
}

static int32_t sys_unlink(wasm_exec_env_t env, const char *name)
{
    (void)env;
    char path[96];
    return fs_path(name, path, sizeof path) ? unlink(path) : -1;
}

static int32_t sys_getpid(wasm_exec_env_t env)
{
    (void)env;
    return 0;
}

/* ed takes no arguments here, only its name: it starts with an empty buffer */
static int32_t sys_arg(wasm_exec_env_t env, int32_t i, char *buf, uint32_t n)
{
    (void)env;
    if (i != 0 || n < 3) {
        return -1;
    }
    memcpy(buf, "ed", 3);
    return 2;
}

static void sys_throw(wasm_exec_env_t env)
{
    (void)env;
    wasm_runtime_set_exception(g_inst, EXC_THROW);
}

static void sys_exit(wasm_exec_env_t env, int32_t code)
{
    (void)env;
    (void)code;
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

static bool tty_init(void)
{
    /* only the UART driver writes from now on */
    esp_log_level_set("*", ESP_LOG_NONE);
    return uart_driver_install(TTY_UART, TTY_RX_BUF, 0, 0, NULL, 0) == ESP_OK;
}

static bool fs_init(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = FS_ROOT,
        .partition_label = FS_PARTITION,
        .format_if_mount_failed = true,
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        tty_printf("LittleFS: %s\n", esp_err_to_name(err));
        return false;
    }
    mkdir(FS_ROOT "/tmp",
          0777); /* ed's temp file; already there after a boot */
    return true;
}

/*
 * The compiler, the point of all this. The AOT file stays in RAM as long as
 * the module, which points into it: in WAMR 2.4.4 wasm_binary_freeable does
 * not copy the data segments (the loader reads the flag before it is set),
 * and the next instantiation would read freed memory.
 */
static wasm_module_t compile_and_load(void)
{
    char err[128];
    wasmc_allocator mem;
    wasmc_static_arena arena;
    wasmc_result res;

    void *ram = heap_caps_malloc(WASMC_RAM, MALLOC_CAP_8BIT);
    if (!ram) {
        tty_printf("no memory for wasmc\n");
        return NULL;
    }
    wasmc_static_allocator(&mem, &arena, ram, WASMC_RAM);

    int64_t start = esp_timer_get_time();
    wasmc_err_t e = wasmc_compile(ed_wasm, ed_wasm_len, &mem, NULL, &res);
    int64_t us = esp_timer_get_time() - start;
    if (e != WASMC_OK) {
        tty_printf("wasmc: %s\n", wasmc_strerror(e));
        heap_caps_free(ram);
        return NULL;
    }
    tty_printf("wasmc: %u bytes of WASM -> %" PRIu32 " bytes of AOT in %" PRId64
               " ms, memory %" PRIu32 "\n",
               ed_wasm_len, res.out_size, us / 1000,
               res.out_size + arena.work_peak);

    /* give back the work memory: the AOT file is at the start of the buffer */
    void *aot = heap_caps_realloc(ram, res.out_size, MALLOC_CAP_8BIT);
    wasm_module_t module =
        wasm_runtime_load(aot ? aot : ram, res.out_size, err, sizeof err);
    if (!module) {
        tty_printf("WAMR load: %s\n", err);
        heap_caps_free(aot ? aot : ram);
    }
    return module;
}

/* ed has no way to list files. */
static void list_files(void)
{
    DIR *dir = opendir(FS_ROOT);
    if (!dir) {
        return;
    }
    tty_printf("files:");
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, "tmp")) {
            tty_printf(" %s", entry->d_name);
        }
    }
    tty_printf("\n");
    closedir(dir);
}

/*
 * Calls app_start or app_loop. Returns false if ed is over: it exited, or a
 * real trap stopped it. Otherwise it returned or unwound on purpose, and
 * app_loop is to be called (again).
 */
static bool call_ed(wasm_exec_env_t env, const char *name)
{
    wasm_function_inst_t func = wasm_runtime_lookup_function(g_inst, name);
    uint32_t argv[1];

    if (!func) {
        tty_printf("no export %s\n", name);
        return false;
    }
    if (wasm_runtime_call_wasm(env, func, 0, argv)) {
        return true;
    }
    const char *exc = wasm_runtime_get_exception(g_inst);
    if (strstr(exc, EXC_THROW)) {
        wasm_runtime_clear_exception(g_inst);
        return true;
    }
    if (!strstr(exc, EXC_EXIT)) {
        tty_printf("ed stopped: %s\n", exc);
    }
    return false;
}

/* One session of ed, from an empty buffer to its exit. */
static void run_ed(wasm_module_t module)
{
    char err[128];

    g_inst =
        wasm_runtime_instantiate(module, WASM_STACK_SIZE, 0, err, sizeof err);
    if (!g_inst) {
        tty_printf("WAMR instantiate: %s\n", err);
        return;
    }
    wasm_exec_env_t env = wasm_runtime_create_exec_env(g_inst, WASM_STACK_SIZE);
    if (env) {
        tty_printf("free heap %u, lowest so far %u, free stack at worst %u\n",
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                   (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
                   (unsigned)uxTaskGetStackHighWaterMark(NULL));
        list_files();
        tty_printf("ed ready\n");
        if (call_ed(env, "app_start")) {
            while (call_ed(env, "app_loop")) {
            }
        }
        wasm_runtime_destroy_exec_env(env);
    } else {
        tty_printf("WAMR exec env: out of memory\n");
    }
    wasm_runtime_deinstantiate(g_inst);
}

/* In a pthread: WAMR's ESP-IDF port needs one (pthread_self()). */
static void *ed_thread(void *arg)
{
    (void)arg;
    if (!tty_init()) {
        return NULL;
    }
    tty_printf("\nV7 ed, compiled here by wasmc\n");
    if (!fs_init()) {
        return NULL;
    }

    RuntimeInitArgs init;
    memset(&init, 0, sizeof init);
    init.mem_alloc_type = Alloc_With_System_Allocator;
    init.native_module_name = "env";
    init.native_symbols = g_natives;
    init.n_native_symbols = sizeof g_natives / sizeof g_natives[0];
    if (!wasm_runtime_full_init(&init)) {
        tty_printf("WAMR init failed\n");
        return NULL;
    }
    wasm_module_t module = compile_and_load();
    if (!module) {
        return NULL;
    }
    /* q does not stop the board: a new, empty ed starts */
    for (;;) {
        run_ed(module);
    }
}

void app_main(void)
{
    pthread_attr_t attr;
    pthread_t thread;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, ED_THREAD_STACK_SIZE);
    if (pthread_create(&thread, &attr, ed_thread, NULL) != 0) {
        printf("cannot start the ed thread\n");
        return;
    }
    pthread_join(thread, NULL);
}
