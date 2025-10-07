#ifndef DALIST_H_INCLUDED
#define DALIST_H_INCLUDED

#include <stdlib.h>

#ifndef DALIST_INITIAL_CAPACITY
#define DALIST_INITIAL_CAPACITY 2
#endif

typedef struct dalist {
    void **items;
    size_t size;
    size_t capacity;
    void (*free)(void *);
} dalist_t;


void da_push(dalist_t *da, void *item);
void *da_pop(dalist_t *da);
void *da_peak_last(dalist_t *da);
unsigned int da_is_empty(dalist_t *da);
void da_free(dalist_t *da);

#endif
