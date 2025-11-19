#include "all.h"
//#include "qsort.h"
//#include "msort.h"
//#include "base64.h"
//#include "matrix.h"
#include "heapsort.h"

void app_main(void)
{
    //unsigned char *start = test_qsort_wasm;
    //unsigned int len = test_qsort_wasm_len;

    //unsigned char *start = test_msort_wasm;
    //unsigned int len = test_msort_wasm_len;

    //unsigned char *start = test_base64_wasm;
    //unsigned int len = test_base64_wasm_len;

    //unsigned char *start = test_matrix_wasm;
    //unsigned int len = test_matrix_wasm_len;

    unsigned char *start = test_heapsort_wasm;
    unsigned int len = test_heapsort_wasm_len;

    wasm_module *m = parse(start, len);
    compile(m);
    free_wasm_module(m);
}
