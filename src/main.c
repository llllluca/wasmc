#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "compile.h"
#include "wasm.h"
#include "aot.h"

extern Target rv32;

int main(int argc, char **argv) {
    if (argc < 2) {
        return 0;
    }
    FILE *input = fopen(argv[1], "r");
    if (input == NULL) {
        return 1;
    }
    fseek(input, 0L, SEEK_END);
    unsigned int wasm_len = ftell(input);
    fseek(input, 0L, SEEK_SET);
    unsigned char *wasm_buf = calloc(wasm_len, sizeof(char));
    if (wasm_buf == NULL) {
        return 1;
    }
    fread(wasm_buf, sizeof(char), wasm_len, input);
    fclose(input);

    WASMModule m;
    wasm_decode(&m, wasm_buf, wasm_len);
    uint8_t *buf;
    uint32_t len;
    if (compile(&m, &rv32, &buf, &len)) return 1;

    FILE *output = fopen("a.aot", "w");
    fwrite(buf, 1, len, output);
    fclose(output);

    wasm_free(&m);
    free(wasm_buf);
    free(buf);
    return EXIT_SUCCESS;
}
