/* Freestanding libc for the benchmark modules: ASCII only. */
#ifndef BENCH_CTYPE_H
#define BENCH_CTYPE_H
static inline int isdigit(int c) { return (unsigned)c - '0' < 10; }
static inline int islower(int c) { return (unsigned)c - 'a' < 26; }
static inline int isupper(int c) { return (unsigned)c - 'A' < 26; }
static inline int isalpha(int c) { return islower(c) || isupper(c); }
static inline int isalnum(int c) { return isalpha(c) || isdigit(c); }
static inline int isxdigit(int c) { return isdigit(c) || (unsigned)(c | 32) - 'a' < 6; }
static inline int isspace(int c) { return c == ' ' || (unsigned)c - '\t' < 5; }
static inline int isprint(int c) { return (unsigned)c - 0x20 < 0x5f; }
static inline int isgraph(int c) { return (unsigned)c - 0x21 < 0x5e; }
static inline int iscntrl(int c) { return (unsigned)c < 0x20 || c == 0x7f; }
static inline int ispunct(int c) { return isgraph(c) && !isalnum(c); }
static inline int tolower(int c) { return isupper(c) ? c | 32 : c; }
static inline int toupper(int c) { return islower(c) ? c & ~32 : c; }
#endif
