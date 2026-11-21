/*
 * port.c - The little of V7's libc that ed uses, over the host functions
 * imported from "env". No signals, one block of malloc.
 */
#define IMPORT(name) __attribute__((import_module("env"), import_name(#name)))
#define EXPORT(name) __attribute__((export_name(#name)))
#define PAGE         65536u

IMPORT(sys_read) int sys_read(int fd, char *buf, int n);
IMPORT(sys_write) int sys_write(int fd, const char *buf, int n);
IMPORT(sys_open) int sys_open(const char *path, int mode);
IMPORT(sys_creat) int sys_creat(const char *path, int mode);
IMPORT(sys_close) int sys_close(int fd);
IMPORT(sys_lseek) int sys_lseek(int fd, int off, int whence);
IMPORT(sys_unlink) int sys_unlink(const char *path);
IMPORT(sys_getpid) int sys_getpid(void);
IMPORT(sys_arg) int sys_arg(int i, char *buf, int n);
/* raise an exception: the callers of the wasm code unwind to the host */
IMPORT(sys_throw) void sys_throw(void);
IMPORT(sys_exit) void sys_exit(int code);

int read(int fd, char *buf, int n)
{
    return sys_read(fd, buf, n);
}
int write(int fd, char *buf, int n)
{
    return sys_write(fd, buf, n);
}
int open(char *path, int mode)
{
    return sys_open(path, mode);
}
int creat(char *path, int mode)
{
    return sys_creat(path, mode);
}
int close(int fd)
{
    return sys_close(fd);
}
int lseek(int fd, int off, int whence)
{
    return sys_lseek(fd, off, whence);
}
int unlink(char *path)
{
    return sys_unlink(path);
}

/*
 * For ed.c, K&R C, these two return int: with another signature wasm-ld links
 * a stub that traps. They never return, if the runtime checks the exception
 * after the import.
 */
int exit(int code)
{
    sys_exit(code);
    __builtin_trap();
}

int ed_throw(void)
{
    sys_throw();
    __builtin_trap();
}

/* signals never arrive: every signal is "already ignored", which keeps ed
   from installing its handlers */
int (*signal(int sig, int (*func)()))()
{
    (void)sig;
    (void)func;
    return (int (*)())1;
}

char *mktemp(char *tmpl)
{
    static char name[16];
    char *d = name;
    int pid = sys_getpid();
    while (*tmpl) {
        *d = *tmpl++;
        if (*d == 'X') {
            *d = (char)('0' + pid % 10);
            pid /= 10;
        }
        d++;
    }
    *d = 0;
    return name;
}

extern char __heap_base;
static char *heap_top = &__heap_base;
static char *last_block;

static int heap_reserve(char *end)
{
    unsigned have = (unsigned)__builtin_wasm_memory_size(0) * PAGE;
    if ((unsigned)end <= have) {
        return 1;
    }
    unsigned pages = ((unsigned)end - have + PAGE - 1) / PAGE;
    return __builtin_wasm_memory_grow(0, pages) != -1ul;
}

/* ed allocates one block, zero[], and grows it: a bump allocator suffices */
char *malloc(unsigned n)
{
    if (!heap_reserve(heap_top + n)) {
        return 0;
    }
    last_block = heap_top;
    heap_top += n;
    return last_block;
}

char *realloc(char *p, unsigned n)
{
    if (p != last_block || !heap_reserve(p + n)) {
        return 0;
    }
    heap_top = p + n;
    return p;
}

/* After an unwind __stack_pointer is where the trap left it. */
__asm__(".globaltype __stack_pointer, i32\n");
extern char __stack_high;

__attribute__((noinline)) static void reset_stack(void)
{
    __asm__("local.get %0\n"
            "global.set __stack_pointer\n" ::"r"(&__stack_high));
}

int ed_main(int argc, char **argv);
int ed_commands(void);

EXPORT(app_start) void app_start(void)
{
    static char args[128];
    static char *argv[8];
    char *p = args;
    int argc = 0;

    reset_stack();
    while (argc < 7) {
        int len = sys_arg(argc, p, (int)(args + sizeof args - p));
        if (len < 0) {
            break;
        }
        argv[argc++] = p;
        p += len + 1;
    }
    ed_main(argc, argv);
}

EXPORT(app_loop) void app_loop(void)
{
    reset_stack();
    ed_commands();
}
