#include "all.h"
#include "msort.h"


void app_main(void)
{
    wasm_module *m = parse(msort_wasm, msort_wasm_len);
    compile(m);
    free_wasm_module(m);
}
