/* Entry of a Sightglass shootout kernel as a benchmark module.

   Built with -DBENCH=<kernel> -Dblack_box=bench_black_box: calls
   <kernel>_setup() (when the kernel has a public one) and <kernel>_body()
   once and returns a hash of what the
   kernel passed to BLACK_BOX() (its first 4 bytes, in call order), so every
   runtime must return the same value. */
#include <stdint.h>

#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)

__attribute__((weak)) void CAT(BENCH, _setup)(void *global_ctx, void **ctx_p);
void CAT(BENCH, _body)(void *ctx);

static uint32_t res;

void bench_black_box(void *x)
{
    uint32_t v;
    __builtin_memcpy(&v, x, sizeof v);
    res = res * 31U + v;
}

__attribute__((export_name("__main_void")))
uint32_t bench_main(void)
{
    void *ctx = 0;
    if (CAT(BENCH, _setup))
        CAT(BENCH, _setup)(0, &ctx);
    CAT(BENCH, _body)(ctx);
    return res;
}
