/* Freestanding libc for the benchmark modules: see ../libc.c. The modules
   have no imports, so output goes nowhere: printf only formats nothing and
   returns 0. */
#ifndef BENCH_STDIO_H
#define BENCH_STDIO_H
#include <stddef.h>
#include <stdarg.h>
#define EOF (-1)
typedef struct bench_file FILE;
extern FILE *const stdin, *const stdout, *const stderr;
int printf(const char *restrict f, ...);
int fprintf(FILE *restrict s, const char *restrict f, ...);
int vprintf(const char *restrict f, va_list ap);
int puts(const char *s);
int putchar(int c);
int fflush(FILE *s);
#endif
