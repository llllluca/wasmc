#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "wasm.h"
#include "libqbe.h"

void panic(void) {
    printf("PANIC!\n");
    exit(EXIT_FAILURE);
}

void *xcalloc(size_t nmemb, size_t size) {
    void *result = calloc(nmemb, size);
    if (result == NULL) {
        panic();
    }
    return result;
}

void *xmalloc(size_t size) {
    void *result = malloc(size);
    if (result == NULL) {
        panic();
    }
    return result;
}

