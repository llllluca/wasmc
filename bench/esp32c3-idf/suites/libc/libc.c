/* A small freestanding libc for the benchmark modules.

   The modules import nothing, so there is no output (printf formats
   nothing and returns 0) and exit/abort trap. malloc takes its memory from
   a static arena (ARENA_SIZE bytes, in the module's BSS) and never grows
   the linear memory: a module without memory.grow can then have its memory
   shrunk to __heap_base by the loader (wamrc) or by wasmc.

   Compiled with -ffreestanding, so the compiler doesn't turn these loops
   back into calls to themselves. */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef ARENA_SIZE
#define ARENA_SIZE (32 * 1024)
#endif

/* --- memory and strings -------------------------------------------- */

void *memcpy(void *restrict d, const void *restrict s, size_t n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;
    if ((((uintptr_t)dp | (uintptr_t)sp) & 3) == 0) {
        for (; n >= 4; n -= 4, dp += 4, sp += 4)
            *(uint32_t *)dp = *(const uint32_t *)sp;
    }
    while (n--)
        *dp++ = *sp++;
    return d;
}

void *memmove(void *d, const void *s, size_t n)
{
    unsigned char *dp = d;
    const unsigned char *sp = s;
    if (dp == sp || n == 0)
        return d;
    if (dp < sp || dp >= sp + n)
        return memcpy(d, s, n);
    while (n--)
        dp[n] = sp[n];
    return d;
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = d;
    uint32_t w = (unsigned char)c * 0x01010101U;
    while (n && ((uintptr_t)p & 3)) {
        *p++ = (unsigned char)c;
        n--;
    }
    for (; n >= 4; n -= 4, p += 4)
        *(uint32_t *)p = w;
    while (n--)
        *p++ = (unsigned char)c;
    return d;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    for (; n; n--, x++, y++)
        if (*x != *y)
            return *x - *y;
    return 0;
}

void *memchr(const void *s, int c, size_t n)
{
    const unsigned char *p = s;
    for (; n; n--, p++)
        if (*p == (unsigned char)c)
            return (void *)p;
    return NULL;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return (size_t)(p - s);
}

size_t strnlen(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i])
        i++;
    return i;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        if (!*a)
            break;
    }
    return 0;
}

char *strcpy(char *restrict d, const char *restrict s)
{
    char *r = d;
    while ((*d++ = *s++))
        ;
    return r;
}

char *strncpy(char *restrict d, const char *restrict s, size_t n)
{
    char *r = d;
    for (; n && *s; n--)
        *d++ = *s++;
    for (; n; n--)
        *d++ = 0;
    return r;
}

char *strcat(char *restrict d, const char *restrict s)
{
    strcpy(d + strlen(d), s);
    return d;
}

char *strncat(char *restrict d, const char *restrict s, size_t n)
{
    char *p = d + strlen(d);
    while (n-- && *s)
        *p++ = *s++;
    *p = 0;
    return d;
}

char *strchr(const char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c)
            return (char *)s;
        if (!*s)
            return NULL;
    }
}

char *strrchr(const char *s, int c)
{
    const char *r = NULL;
    for (;; s++) {
        if (*s == (char)c)
            r = s;
        if (!*s)
            return (char *)r;
    }
}

char *strstr(const char *h, const char *n)
{
    size_t len = strlen(n);
    for (; *h; h++)
        if (!strncmp(h, n, len))
            return (char *)h;
    return len ? NULL : (char *)h;
}

size_t strspn(const char *s, const char *a)
{
    size_t i = 0;
    while (s[i] && strchr(a, s[i]))
        i++;
    return i;
}

size_t strcspn(const char *s, const char *r)
{
    size_t i = 0;
    while (s[i] && !strchr(r, s[i]))
        i++;
    return i;
}

char *strpbrk(const char *s, const char *a)
{
    s += strcspn(s, a);
    return *s ? (char *)s : NULL;
}

char *strtok_r(char *restrict s, const char *restrict d, char **restrict save)
{
    if (!s)
        s = *save;
    s += strspn(s, d);
    if (!*s) {
        *save = s;
        return NULL;
    }
    char *e = s + strcspn(s, d);
    if (*e)
        *e++ = 0;
    *save = e;
    return s;
}

char *strtok(char *restrict s, const char *restrict d)
{
    static char *save;
    return strtok_r(s, d, &save);
}

/* --- malloc: first fit on a static arena --------------------------- */

/* Every block starts with its size (header included, a multiple of 8);
   bit 0 set means in use. Free neighbours are merged while searching. */
static _Alignas(8) unsigned char arena[ARENA_SIZE];
static size_t arena_used; /* bytes of the arena ever handed out */

#define HDR 8U

void *malloc(size_t n)
{
    if (n > ARENA_SIZE)
        return NULL;
    size_t need = (n + HDR + 7U) & ~(size_t)7U;
    for (size_t at = 0; at < arena_used;) {
        size_t *h = (size_t *)(arena + at), sz = *h & ~(size_t)1;
        if (!(*h & 1)) {
            /* merge the free blocks after this one */
            while (at + sz < arena_used && !(*(size_t *)(arena + at + sz) & 1))
                sz += *(size_t *)(arena + at + sz);
            if (at + sz == arena_used) { /* last block: give it back */
                arena_used = at;
                break;
            }
            *h = sz;
            if (sz >= need) {
                if (sz - need >= HDR + 8U) {
                    *(size_t *)(arena + at + need) = sz - need;
                    sz = need;
                }
                *h = sz | 1;
                return arena + at + HDR;
            }
        }
        at += sz;
    }
    if (ARENA_SIZE - arena_used < need)
        return NULL;
    size_t *h = (size_t *)(arena + arena_used);
    *h = need | 1;
    arena_used += need;
    return (unsigned char *)h + HDR;
}

void free(void *p)
{
    if (p)
        *(size_t *)((unsigned char *)p - HDR) &= ~(size_t)1;
}

void *calloc(size_t m, size_t n)
{
    if (n && m > ARENA_SIZE / n)
        return NULL;
    void *p = malloc(m * n);
    if (p)
        memset(p, 0, m * n);
    return p;
}

void *realloc(void *p, size_t n)
{
    if (!p)
        return malloc(n);
    size_t old = (*(size_t *)((unsigned char *)p - HDR) & ~(size_t)1) - HDR;
    if (n <= old)
        return p;
    void *q = malloc(n);
    if (q) {
        memcpy(q, p, old);
        free(p);
    }
    return q;
}

/* --- the rest of stdlib -------------------------------------------- */

_Noreturn void abort(void) { __builtin_trap(); }
_Noreturn void exit(int code) { (void)code; __builtin_trap(); }

static uint32_t rand_state = 1;
void srand(unsigned seed) { rand_state = seed; }
int rand(void)
{
    rand_state = rand_state * 1103515245U + 12345U;
    return (int)((rand_state >> 1) & RAND_MAX);
}

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }

unsigned long strtoul(const char *restrict s, char **restrict end, int base)
{
    unsigned long v = 0;
    int neg = 0;
    while (*s == ' ' || (unsigned)*s - '\t' < 5)
        s++;
    if (*s == '-' || *s == '+')
        neg = *s++ == '-';
    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] | 32) == 'x')
        s += 2, base = 16;
    else if (base == 0)
        base = *s == '0' ? 8 : 10;
    for (;; s++) {
        int d = (unsigned)*s - '0' < 10 ? *s - '0'
              : (unsigned)(*s | 32) - 'a' < 26 ? (*s | 32) - 'a' + 10 : 99;
        if (d >= base)
            break;
        v = v * (unsigned)base + (unsigned)d;
    }
    if (end)
        *end = (char *)s;
    return neg ? -v : v;
}

long strtol(const char *restrict s, char **restrict end, int base)
{
    return (long)strtoul(s, end, base);
}

int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }

static void swap_bytes(unsigned char *a, unsigned char *b, size_t n)
{
    while (n--) {
        unsigned char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

/* Heapsort: no recursion, no extra memory. */
static void sift(unsigned char *b, size_t root, size_t n, size_t sz,
                 int (*cmp)(const void *, const void *))
{
    for (;;) {
        size_t c = 2 * root + 1;
        if (c >= n)
            return;
        if (c + 1 < n && cmp(b + c * sz, b + (c + 1) * sz) < 0)
            c++;
        if (cmp(b + root * sz, b + c * sz) >= 0)
            return;
        swap_bytes(b + root * sz, b + c * sz, sz);
        root = c;
    }
}

void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *))
{
    unsigned char *b = base;
    for (size_t i = n / 2; i-- > 0;)
        sift(b, i, n, sz, cmp);
    for (size_t i = n; i-- > 1;) {
        swap_bytes(b, b + i * sz, sz);
        sift(b, 0, i, sz, cmp);
    }
}

/* --- compiler runtime -------------------------------------------- */

/* 128-bit multiply (unsigned __int128 a * b): wasm32 has 64-bit
   multiplies only. Built from 32x32->64 products so it doesn't call
   itself. */
static void mul64x64(uint64_t a, uint64_t b, uint64_t *lo, uint64_t *hi)
{
    uint64_t a0 = (uint32_t)a, a1 = a >> 32, b0 = (uint32_t)b, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (uint32_t)p01 + (uint32_t)p10;
    *lo = (mid << 32) | (uint32_t)p00;
    *hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

typedef unsigned __int128 u128;
u128 __multi3(u128 a, u128 b)
{
    uint64_t al = (uint64_t)a, ah = (uint64_t)(a >> 64);
    uint64_t bl = (uint64_t)b, bh = (uint64_t)(b >> 64);
    uint64_t lo, hi;
    mul64x64(al, bl, &lo, &hi);
    hi += al * bh + ah * bl;
    return ((u128)hi << 64) | lo;
}

/* --- stdio: nowhere to write --------------------------------------- */

FILE *const stdin, *const stdout, *const stderr;
int printf(const char *restrict f, ...) { (void)f; return 0; }
int fprintf(FILE *restrict s, const char *restrict f, ...) { (void)s; (void)f; return 0; }
int vprintf(const char *restrict f, va_list ap) { (void)f; (void)ap; return 0; }
int puts(const char *s) { (void)s; return 0; }
int putchar(int c) { return (unsigned char)c; }
int fflush(FILE *s) { (void)s; return 0; }
