/* Entry of an Embench-IoT benchmark as a benchmark module, in place of
   Embench's support/main.c and board files.

   Built with -DGLOBAL_SCALE_FACTOR=1 (one pass of the benchmark's loop:
   Embench sizes it so one pass takes about 4 s on a 1 MHz core) and no
   cache warm-up. Returns benchmark()'s result when verify_benchmark()
   accepts it, otherwise 0xbad00000 ^ result, so a wrong run shows as a
   result that differs from the other modes. */
#include "support.h"

void initialise_board(void) {}
void start_trigger(void) {}
void stop_trigger(void) {}

__attribute__((export_name("__main_void")))
unsigned bench_main(void)
{
    initialise_board();
    initialise_benchmark();
    warm_caches(0);
    start_trigger();
    int r = benchmark();
    stop_trigger();
    return verify_benchmark(r) ? (unsigned)r : 0xbad00000U ^ (unsigned)r;
}
