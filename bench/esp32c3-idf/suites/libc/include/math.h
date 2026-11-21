/* Freestanding libc for the benchmark modules. The operations WebAssembly
   has as instructions are builtins; the rest are in ../libc.c. */
#ifndef BENCH_MATH_H
#define BENCH_MATH_H
#define HUGE_VAL __builtin_huge_val()
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")
#define M_PI 3.14159265358979323846
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
static inline double fabs(double x) { return __builtin_fabs(x); }
static inline float fabsf(float x) { return __builtin_fabsf(x); }
static inline double sqrt(double x) { return __builtin_sqrt(x); }
static inline float sqrtf(float x) { return __builtin_sqrtf(x); }
static inline double floor(double x) { return __builtin_floor(x); }
static inline float floorf(float x) { return __builtin_floorf(x); }
static inline double ceil(double x) { return __builtin_ceil(x); }
static inline float ceilf(float x) { return __builtin_ceilf(x); }
static inline double trunc(double x) { return __builtin_trunc(x); }
static inline double fmin(double a, double b) { return __builtin_fmin(a, b); }
static inline double fmax(double a, double b) { return __builtin_fmax(a, b); }
double exp(double x);
float expf(float x);
double log(double x);
double pow(double x, double y);
double sin(double x);
double cos(double x);
double atan(double x);
double acos(double x);
#endif
