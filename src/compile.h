#ifndef COMPILE_H_INCLUDED
#define COMPILE_H_INCLUDED

#include "libqbe.h"
#include "wasm.h"
#include "aot.h"

typedef enum CompileErr_t {
    COMPILE_OK = 0,
    COMPILE_ERR,
} CompileErr_t ;

CompileErr_t compile(WASMModule *wasm_mod, Target *T, uint8_t **out_buf, uint32_t *out_len);

#endif
