/* Freestanding libc for the benchmark modules: see ../libc.c. */
#ifndef BENCH_STDLIB_H
#define BENCH_STDLIB_H
#include <stddef.h>
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 0x7fffffff
void *malloc(size_t n);
void *calloc(size_t m, size_t n);
void *realloc(void *p, size_t n);
void free(void *p);
_Noreturn void abort(void);
_Noreturn void exit(int code);
int rand(void);
void srand(unsigned seed);
int abs(int x);
long labs(long x);
long long llabs(long long x);
int atoi(const char *s);
long atol(const char *s);
long strtol(const char *restrict s, char **restrict end, int base);
unsigned long strtoul(const char *restrict s, char **restrict end, int base);
void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *));
#endif
