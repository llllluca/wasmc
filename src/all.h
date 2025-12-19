#ifndef AOT_H_INCLUDED
#define AOT_H_INCLUDED

#include "wasm.h"
#include "libqbe.h"

/* parse.c */
wasm_func_body *parse_next_func_body(wasm_module *m);
wasm_module* parse(unsigned char *start, unsigned int len);
void free_wasm_module(wasm_module *m);

/* compile.c */
void compile(wasm_module *m, uint8_t **out_buf, uint32_t *out_len);

/* utils.c */
void read_u8(uint8_t **buf, uint8_t *buf_end, uint8_t *out);
void read_u32(uint8_t **buf, uint8_t *buf_end, uint32_t *out);
unsigned int readULEB128_u32(uint8_t **buf, uint8_t *buf_end, uint32_t *out);
unsigned int readILEB128_i32(uint8_t **buf, uint8_t *buf_end, int32_t *out);
wasm_valtype local_type(func_compile_ctx_t *ctx, uint32_t index);
simple_type cast(wasm_valtype t);
void panic(void);
void *xcalloc(size_t nmemb, size_t size);
void *xmalloc(size_t size);

/* rega.c */
void linear_scan(Fn *f, rv32_reg_pool *gpr, rv32_reg_pool *arg);

#endif
