#ifndef COMPILE_H_INCLUDED
#define COMPILE_H_INCLUDED

#include <stdint.h>

#define WASMC_ERR_MALFORMED_WASM_MODULE -1
#define WASMC_ERR_OUT_OF_HEAP_MEMORY    -2
#define WASMC_ERR_AOT_BUFFER_TOO_SMALL  -3

int wasmc_compile(uint8_t *wasm_buf, unsigned int wasm_len,
                  uint8_t *aot_buf, unsigned int aot_len);

#endif //COMPILE_H_INCLUDED
