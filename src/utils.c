#include "all.h"
#include <stdio.h>
#include <stdlib.h>

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

void read_u32(read_struct_t *r, uint32_t *out) {
    unsigned char *new_offset = r->offset + sizeof(uint32_t);
    if (new_offset > r->end) {
        panic();
    }
    *out = *((uint32_t *) r->offset);
    r->offset = new_offset;
}

void read_u8(read_struct_t *r, unsigned char *out) {
    unsigned char *new_offset = r->offset + sizeof(unsigned char);
    if (new_offset > r->end) {
        panic();
    }
    *out = *(r->offset);
    r->offset = new_offset;
}

// Unsigned Litte Endian Base 128: https://en.wikipedia.org/wiki/LEB128
/* read a uint32_t number encoded in the ULED128 format. ULED128 is a
 * variable length encoding format, so the actual number of readed bytes
 * may be less that 4. */
unsigned int readULEB128_u32(read_struct_t *r, uint32_t *out) {
    uint32_t result = 0;
    uint32_t shift = 0;
    unsigned char byte;
    unsigned int count = 0;
    do {
        read_u8(r, &byte);
        result |= (byte & 0x7f) << shift; /* low-order 7 bits of value */
        shift += 7;
        count++;
    } while ((byte & 0x80) != 0); /* get high-order bit of byte */

    *out = result;
    return count;
}

#pragma GCC diagnostic ignored "-Wshift-negative-value"
unsigned int readILEB128_i32(read_struct_t *r, int32_t *out) {
    int32_t result = 0;
    uint32_t shift = 0;
    /* the size in bits of the result variable, e.g., 32 if result's type is int32_t */
    uint32_t size = 32;
    unsigned char byte;
    unsigned int count = 0;
    do {
        read_u8(r, &byte);
        result |= (byte & 0x7f) << shift; /* low-order 7 bits of value */
        shift += 7;
        count++;
    } while ((byte & 0x80) != 0); /* get high-order bit of byte */

    /* sign bit of byte is second high-order bit (0x40) */
    if ((shift < size) && ((byte & 0x40) != 0))
      /* sign extend */
    result |= (~0 << shift);
    *out = result;
    return count;
}

