#include <stdio.h>
#include <stdlib.h>
#include "wasmc.h"

#define AOT_LEN (16 * 1024)
uint8_t aot_buf[AOT_LEN];

int main(int argc, char **argv) {

    if (argc < 2) {
        fprintf(stderr, "Usage: %s path/to/file.wasm\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *input_path = argv[1];
    FILE *wasm_file = fopen(input_path, "r");
    if (wasm_file == NULL) {
        fprintf(stderr, "Error: cannot open %s\n", input_path);
        return EXIT_FAILURE;
    }

    unsigned int wasm_len;
    uint8_t *wasm_buf;
    fseek(wasm_file, 0L, SEEK_END);
    wasm_len = ftell(wasm_file);
    fseek(wasm_file, 0L, SEEK_SET);
    wasm_buf = calloc(wasm_len, sizeof(uint8_t));
    if (wasm_buf == NULL) {
        fprintf(stderr, "Error: calloc failure\n");
        return EXIT_FAILURE;
    }
    fread(wasm_buf, sizeof(uint8_t), wasm_len, wasm_file);
    fclose(wasm_file);

    int len;
    len = compile(wasm_buf, wasm_len, aot_buf, AOT_LEN);
    if (len < 0) {
        fprintf(stderr, "Error: compilation failure\n");
        return EXIT_FAILURE;
    }
    free(wasm_buf);

    FILE *output = fopen("a.aot", "w");
    fwrite(aot_buf, 1, len, output);
    fclose(output);

    return EXIT_SUCCESS;
}

