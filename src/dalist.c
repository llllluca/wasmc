#include "dalist.h" 
#include <stdlib.h>

static void da_grow(dalist_t *da, size_t expected_capacity) {
    if (expected_capacity < da->capacity) return;
    if (da->capacity == 0) {
        da->capacity = DALIST_INITIAL_CAPACITY;
    }
    while (expected_capacity > da->capacity) {
        da->capacity *= 2;
    }
    da->items = realloc(da->items, da->capacity * sizeof(void *));
    if (da->items == NULL) {
        exit(EXIT_FAILURE);
    }
}

void da_push(dalist_t *da, void *item) {
    da_grow(da, da->size + 1);
    da->items[da->size++] = item;
}

void *da_pop(dalist_t *da) {
    if (da->size == 0) return NULL;
    return da->items[--da->size];
}

void *da_peak_last(dalist_t *da) {
    if (da->size == 0) return NULL;
    return da->items[da->size - 1];
}

unsigned int da_is_empty(dalist_t *da) {
    return da->size == 0;
}

void da_free(dalist_t *da) {
    if (da->free != NULL) {
        for (unsigned int i = 0; i < da->size; i++) {
            da->free(da->items[i]);
        }
    }
    free(da->items);
}

