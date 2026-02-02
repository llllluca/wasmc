#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "wasmc.h"

#define AOT_LEN (16 * 1024)
uint8_t aot_buf[AOT_LEN];

void print_usage(FILE *f) {
    char *usage =
    "Usage: wasmc <option(s)> path/to/file.wasm\n"
    "Options are:\n\n"
    "  -h         Display this information.\n"
    "  -o <file>  Place the output into <file>.\n";
    fprintf(f, "%s\n", usage);
}

int main(int argc, char **argv) {

    if (argc < 2) {
        print_usage(stderr);
        return EXIT_FAILURE;
    }

    char *output_path = "a.aot";
    int c;
    char *optstring = "ho:";
    while ((c = getopt(argc, argv, optstring)) != -1) {
        switch (c) {
        case 'h':
            print_usage(stdout);
            return EXIT_SUCCESS;
        case 'o':
            output_path = optarg;
            break;
        default:
            return EXIT_FAILURE;
        }
    }

    if (optind == argc) {
        fprintf(stderr, "Error: provide an input file.\n");
        return EXIT_FAILURE;
    }

    if(optind != argc-1) {
        fprintf(stderr, "Error: invalid number of arguments,"
                " provide only one input file.\n");
        return EXIT_FAILURE;
    }

    char *input_path = argv[optind];
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
    len = wasmc_compile(wasm_buf, wasm_len, aot_buf, AOT_LEN);
    switch (len) {
    case WASMC_ERR_MALFORMED_WASM_MODULE:
        fprintf(stderr, "Error: compilation failure, malformed WebAssembly module\n");
        return EXIT_FAILURE;
    case WASMC_ERR_OUT_OF_HEAP_MEMORY:
        fprintf(stderr, "Error: compilation failure, out of heap memory\n");
        return EXIT_FAILURE;
    case WASMC_ERR_AOT_BUFFER_TOO_SMALL:
        fprintf(stderr, "Error: compilation failure, the aot buffer is too small\n");
        return EXIT_FAILURE;
    }
    free(wasm_buf);

    FILE *output = fopen(output_path, "w");
    fwrite(aot_buf, 1, len, output);
    fclose(output);

    return EXIT_SUCCESS;
}

