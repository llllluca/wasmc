#ifndef COMPILE_H_INCLUDED
#define COMPILE_H_INCLUDED

#include <stdint.h>

int compile(uint8_t *wasm_buf, unsigned int wasm_len,
                     uint8_t *aot_buf, unsigned int aot_len);

#endif //COMPILE_H_INCLUDED
