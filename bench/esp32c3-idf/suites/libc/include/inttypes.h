/* Freestanding libc for the benchmark modules (wasm32: long is 32 bits). */
#ifndef BENCH_INTTYPES_H
#define BENCH_INTTYPES_H
#include <stdint.h>
#define PRId32 "d"
#define PRIu32 "u"
#define PRIx32 "x"
#define PRId64 "lld"
#define PRIu64 "llu"
#define PRIx64 "llx"
#endif
