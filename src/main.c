#include "all.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    wasm_module *m = parse(wasm_buf, wasm_len);
    uint8_t *buf;
    uint32_t len;
    compile(m, &buf, &len);
    fwrite(buf, len, 1, stdout);

    free_wasm_module(m);
    free(wasm_buf);
    return EXIT_SUCCESS;
}
