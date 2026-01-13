/* stb-style single header file libray (https://github.com/nothings/stb/)
 * from a subset of these Linux Kernel source files:
 * "lib/bitmap.c" GPL-2.0 and "include/linux/bitmap.h" GPL-2.0 */
#ifndef BITMAP_H_INCLUDED
#define BITMAP_H_INCLUDED

#include <string.h>
#include <stdbool.h>

/**
 * DOC: bitmap introduction
 *
 * bitmaps provide an array of bits, implemented using an
 * array of unsigned longs.  The number of valid bits in a
 * given bitmap does _not_ need to be an exact multiple of
 * __BITMAP_BITS_PER_LONG.
 *
 * The possible unused bits in the last, partially used word
 * of a bitmap are 'don't care'.  The implementation makes
 * no particular effort to keep them zero.  It ensures that
 * their value will not affect the results of any operation.
 * The bitmap operations that return Boolean (bitmap_empty,
 * for example) results carefully filter out these unused
 * bits from impacting their results.
 */

#define __BITMAP_BITS_PER_BYTE 8
#define __BITMAP_BYTES_PER_LONG sizeof(unsigned long)
#define __BITMAP_BITS_PER_LONG (__BITMAP_BITS_PER_BYTE * __BITMAP_BYTES_PER_LONG)
#define __BITMAP_MEM_ALIGNMENT __BITMAP_BYTES_PER_LONG
#define __BITMAP_MEM_MASK (__BITMAP_MEM_ALIGNMENT - 1)

# define __BITMAP_LIKELY(x) __builtin_expect(!!(x), 1)
# define __BITMAP_UNLIKELY(x) __builtin_expect(!!(x), 0)

/*
 * Generate a bitmask with bits set to 1 between the
 * specified high (h) and low (l) positions, inclusive.
 * Assumption: h >= l and 0 <= l, h < __BITMAP_BITS_PER_LONG
 */
#define __BITMAP_GENMASK(h, l) \
    (((~0UL) << (l)) & (~0UL) >> (__BITMAP_BITS_PER_LONG - 1 - (h)))

#define __BITMAP_ALIGN(x, a) \
    (((x) + ((a) - 1)) & ~((a) - 1))

#define __BITMAP_IS_ALIGNED(x, a) \
    (((x) & ((typeof(x))(a) - 1)) == 0)

#define __BITMAP_DIV_ROUND_UP(n, d) \
    (((n) + (d) - 1) / (d))

#define __BITMAP_BITS_TO_LONGS(nr) \
    __BITMAP_DIV_ROUND_UP(nr, __BITMAP_BITS_PER_LONG)

#define __BITMAP_FIRST_WORD_MASK(start) \
    (~0UL << ((start) & (__BITMAP_BITS_PER_LONG - 1)))

#define __BITMAP_LAST_WORD_MASK(nbits) \
    (~0UL >> (-(nbits) & (__BITMAP_BITS_PER_LONG - 1)))
/*
 * __BITMAP_SMALL_CONST_NBITS(n) is true precisely when it is known at compile-time
 * that bitmap_size(n) is BYTES_PER_LONG, i.e. 1 <= n <= __BITMAP_BITS_PER_LONG.
 * This allows various bit/bitmap APIs to provide a fast implementation. Bitmaps
 * of size 0 are very rare, and a compile-time-known-size 0 is most likely
 * a sign of error. They will be handled correctly by the bit/bitmap APIs,
 * but using the out-of-line functions, so that the inline implementations
 * can unconditionally dereference the pointer(s).
 */
#define __BITMAP_SMALL_CONST_NBITS(nbits) \
    (__builtin_constant_p(nbits) && 0 < (nbits) && (nbits) <= __BITMAP_BITS_PER_LONG)

/* The DECLARE_BITMAP(name,bits) macro can be used to declare
 * an array named 'name' of just enough unsigned longs to
 * contain all bit positions from 0 to 'bits' - 1. */
#define DECLARE_BITMAP(name,bits) \
    unsigned long name[__BITMAP_BITS_TO_LONGS(bits)]

#define for_each_set_bit(bit, addr, size) \
    for ((bit) = 0; (bit) = find_next_bit((addr), (size), (bit)), (bit) < (size); (bit)++)

/**
 * bitmap_size() - Calculate the size of the bitmap in bytes.
 * @nbits: The number of bits in the bitmap.
 */
#define bitmap_size(nbits) \
    (__BITMAP_ALIGN(nbits, __BITMAP_BITS_PER_LONG) / __BITMAP_BITS_PER_BYTE)

/**
 * bitmap_set_bit() - Set a single bit in the bitmap to 1.
 * @map: The bitmap.
 * @bit: The bit position to set.
 */
#define bitmap_set_bit(map, bit) bitmap_set(map, bit, 1)

/**
 * bitmap_clear_bit() - Clear a single bit in the bitmap to 0.
 * @map: Pointer to the bitmap.
 * @bit: The bit position to clear.
 */
#define bitmap_clear_bit(map, bit) bitmap_clear(map, bit, 1)

/* Foreward declarations of the implementation functions */
bool __bitmap_and(unsigned long *dst, const unsigned long *bitmap1,
                  const unsigned long *bitmap2, unsigned int nbits);
void __bitmap_or(unsigned long *dst, const unsigned long *bitmap1,
                 const unsigned long *bitmap2, unsigned int bits);
void __bitmap_xor(unsigned long *dst, const unsigned long *bitmap1,
                  const unsigned long *bitmap2, unsigned int bits);
void __bitmap_complement(unsigned long *dst,
                         const unsigned long *src, unsigned int bits);
bool __bitmap_equal(const unsigned long *bitmap1,
                    const unsigned long *bitmap2, unsigned int bits);
bool __bitmap_intersects(const unsigned long *bitmap1,
                         const unsigned long *bitmap2, unsigned int bits);
bool __bitmap_subset(const unsigned long *bitmap1,
                     const unsigned long *bitmap2, unsigned int bits);
void __bitmap_set(unsigned long *map, unsigned int start, int len);
void __bitmap_clear(unsigned long *map, unsigned int start, int len);
unsigned long __find_next_bit(const unsigned long *addr,
                              unsigned long nbits, unsigned long offset);

/**
 * bitmap_zero() - Initialize a bitmap with all bits set to zero.
 * @dst: The bitmap to initialize.
 * @nbits: Number of bits of which the bitmap is composed.
 *
 * bitmap_zero() operate over the region of unsigned longs, that is, bits
 * behind bitmap till the unsigned long boundary will be zeroed as well.
 * Consider to use bitmap_clear() to make explicit zeroing.
 */
static __always_inline void bitmap_zero(unsigned long *dst, unsigned int nbits)
{
    unsigned int len = bitmap_size(nbits);
    if (__BITMAP_SMALL_CONST_NBITS(nbits)) {
        *dst = 0;
    } else {
        memset(dst, 0, len);
    }
}

/**
 * bitmap_fill() - Initialize a bitmap with all bits set to one.
 * @dst: The bitmap to initialize.
 * @nbits: Number of bits of which the bitmap is composed.
 *
 * bitmap_fill() operate over the region of unsigned longs, that is, bits
 * behind bitmap till the unsigned long boundary will be filled as well.
 * Consider to use bitmap_set() to make explicit filling.
 */
static __always_inline void bitmap_fill(unsigned long *dst, unsigned int nbits)
{
    unsigned int len = bitmap_size(nbits);
    if (__BITMAP_SMALL_CONST_NBITS(nbits)) {
        *dst = ~0UL;
    } else {
        memset(dst, 0xff, len);
    }
}

/**
 * bitmap_copy() - Copy source bitmap to destination bitmap.
 * @dst: The destination bitmap.
 * @src: The source bitmap.
 * @nbits: Number of bits of the source and destination bitmaps.
 */
static __always_inline
void bitmap_copy(unsigned long *dst, const unsigned long *src, unsigned int nbits)
{
    unsigned int len = bitmap_size(nbits);
    if (__BITMAP_SMALL_CONST_NBITS(nbits)) {
        *dst = *src;
    } else {
        memcpy(dst, src, len);
    }
}

/**
 * bitmap_and() - bitwise and.
 * @dst: The bitmap in which to store the result of the bitwise and.
 * @src1: The first operand of the bitwise and.
 * @src2: The second operand of the bitwise and.
 * @nbits: Number of bits of the src1, src2 and dst bitmaps.
 *
 * Return: true if dst has at least one bit on, otherwise returns false.
 */
static __always_inline
bool bitmap_and(unsigned long *dst, const unsigned long *src1,
                const unsigned long *src2, unsigned int nbits)
{
    if (__BITMAP_SMALL_CONST_NBITS(nbits)) {
        return (*dst = *src1 & *src2 & __BITMAP_LAST_WORD_MASK(nbits)) != 0;
    }
    return __bitmap_and(dst, src1, src2, nbits);
}

/**
 * bitmap_or() - bitwise or.
 * @dst: The bitmap in which to store the result of the bitwise or.
 * @src1: The first operand of the bitwise or.
 * @src2: The second operand of the bitwise or.
 * @nbits: Number of bits of the src1, src2 and dst bitmaps.
 */
static __always_inline
void bitmap_or(unsigned long *dst, const unsigned long *src1,
               const unsigned long *src2, unsigned int nbits)
{
    if (__BITMAP_SMALL_CONST_NBITS(nbits)) {
        *dst = *src1 | *src2;
    } else {
        __bitmap_or(dst, src1, src2, nbits);
    }
}

/**
 * bitmap_xor() - bitwise xor.
 * @dst: The bitmap in which to store the result of the bitwise xor.
 * @src1: The first operand of the bitwise xor.
 * @src2: The second operand of the bitwise xor.
 * @nbits: Number of bits of the src1, src2 and dst bitmaps.
 */
static __always_inline
void bitmap_xor(unsigned long *dst, const unsigned long *src1,
                const unsigned long *src2, unsigned int nbits)
{
    if (__BITMAP_SMALL_CONST_NBITS(nbits)) {
        *dst = *src1 ^ *src2;
    } else {
        __bitmap_xor(dst, src1, src2, nbits);
    }
}

/**
 * bitmap_complement() - bitwise not.
 * @dst: The bitmap in which to store the result of the bitwise not.
 * @src1: The operand of the bitwise not.
 * @nbits: Number of bits of the src1 and dst bitmaps.
 */
static __always_inline
void bitmap_complement(unsigned long *dst, const unsigned long *src, unsigned int nbits)
{
    if (__BITMAP_SMALL_CONST_NBITS(nbits)) {
        *dst = ~(*src);
    } else {
        __bitmap_complement(dst, src, nbits);
    }
}

/**
 * bitmap_equal() - Check if two bitmaps are equal.
 * @src1: The first operand.
 * @src2: The second operand.
 * @nbits: Number of bits of the src1 and src2 bitmaps.
 *
 * Return: true if src1 and src2 are equal, otherwise returns false.
 */
static __always_inline
bool bitmap_equal(const unsigned long *src1, const unsigned long *src2, unsigned int nbits)
{
    if (__BITMAP_SMALL_CONST_NBITS(nbits)) {
        return !((*src1 ^ *src2) & __BITMAP_LAST_WORD_MASK(nbits));
    }
    if (__builtin_constant_p(nbits & __BITMAP_MEM_MASK) &&
        __BITMAP_IS_ALIGNED(nbits, __BITMAP_MEM_ALIGNMENT)) {
        return !memcmp(src1, src2, nbits / 8);
    }
    return __bitmap_equal(src1, src2, nbits);
}

/**
 * bitmap_intersects() - Check if two bitmaps intersect.
 * @src1: The first operand.
 * @src2: The second operand.
 * @nbits: Number of bits of the src1 and src2 bitmaps.
 *
 * Return: true if src1 and src2 intersect, otherwise returns false.
 */
static __always_inline
bool bitmap_intersects(const unsigned long *src1,
                       const unsigned long *src2, unsigned int nbits)
{
	if (__BITMAP_SMALL_CONST_NBITS(nbits)) { 
		return ((*src1 & *src2) & __BITMAP_LAST_WORD_MASK(nbits)) != 0;
    }
	return __bitmap_intersects(src1, src2, nbits);
}

/**
 * bitmap_intersects() - Check if the first bitmap is a subset of the second.
 * @src1: The first operand.
 * @src2: The second operand.
 * @nbits: Number of bits of the src1 and src2 bitmaps.
 *
 * Return: true if src1 is a subset of src2, otherwise returns false.
 */
static __always_inline
bool bitmap_subset(const unsigned long *src1,
                   const unsigned long *src2, unsigned int nbits)
{
    if (__BITMAP_SMALL_CONST_NBITS(nbits)) {
        return !((*src1 & ~(*src2)) & __BITMAP_LAST_WORD_MASK(nbits));
    }
    return __bitmap_subset(src1, src2, nbits);
}

static __always_inline
bool bitmap_empty(const unsigned long *src, unsigned nbits)
{
    if (__BITMAP_SMALL_CONST_NBITS(nbits)) {
        return ! (*src & __BITMAP_LAST_WORD_MASK(nbits));
    }

    return __find_next_bit(src, nbits, 0) == nbits;
}

/**
 * bitmap_set() - Set a range of bits in the bitmap to 1.
 * @map: The bitmap.
 * @start: The starting bit position to set.
 * @nbits: The number of bits to set beginning from the start position.
 */
static __always_inline
void bitmap_set(unsigned long *map, unsigned int start, unsigned int nbits)
{
    if (__BITMAP_SMALL_CONST_NBITS(start + nbits)) {
        *map |= __BITMAP_GENMASK(start + nbits - 1, start);
    } else if (__builtin_constant_p(start & __BITMAP_MEM_MASK) &&
         __BITMAP_IS_ALIGNED(start, __BITMAP_MEM_ALIGNMENT) &&
         __builtin_constant_p(nbits & __BITMAP_MEM_MASK) &&
         __BITMAP_IS_ALIGNED(nbits, __BITMAP_MEM_ALIGNMENT)) {
        memset((char *)map + start / 8, 0xff, nbits / 8);
    } else {
        __bitmap_set(map, start, nbits);
    }
}

/**
 * bitmap_clear() - Clear a range of bits in the bitmap to 0.
 * @map: The bitmap.
 * @start: The starting bit position to clear.
 * @nbits: The number of bits to clear beginning from the start position.
 */
static __always_inline
void bitmap_clear(unsigned long *map, unsigned int start, unsigned int nbits)
{
    if (__BITMAP_SMALL_CONST_NBITS(start + nbits)) {
        *map &= ~__BITMAP_GENMASK(start + nbits - 1, start);
    } else if (__builtin_constant_p(start & __BITMAP_MEM_MASK) &&
         __BITMAP_IS_ALIGNED(start, __BITMAP_MEM_ALIGNMENT) &&
         __builtin_constant_p(nbits & __BITMAP_MEM_MASK) &&
         __BITMAP_IS_ALIGNED(nbits, __BITMAP_MEM_ALIGNMENT)) {
        memset((char *)map + start / 8, 0, nbits / 8);
    } else {
        __bitmap_clear(map, start, nbits);
    }
}

/**
 * bitmap_alloc() - Allocate memory for a bitmap on the heap.
 * @nbits: The number of bits in the bitmap.
 *
 * Return: a pointer to the allocated memory, returns NULL on error.
 */
unsigned long *bitmap_alloc(unsigned int nbits);

/**
 * bitmap_free() - Deallocate memory for a bitmap.
 * @bitmap: Pointer to the bitmap to free.
 */
void bitmap_free(const unsigned long *bitmap);

/**
 * bitmap_test_bit() - Test if a specific bit is set.
 * @map: The bitmap.
 * @bit: The bit position to check.
 *
 * Return: true if the bit is set, otherwise returns false.
 */
static __always_inline bool bitmap_test_bit(const unsigned long *map, unsigned int bit)
{
    return (map[bit / __BITMAP_BITS_PER_LONG] &
        (1UL << (bit % __BITMAP_BITS_PER_LONG))) != 0;
}

/**
 * find_next_bit - find the next set bit in a memory region
 * @addr: The address to base the search on
 * @nbits: The bitmap size in bits
 * @offset: The bitnumber to start searching at
 *
 * Returns the bit number for the next set bit
 * If no bits are set, returns @size.
 */
static __always_inline
unsigned long find_next_bit(const unsigned long *addr, unsigned long nbits,
                unsigned long offset)
{
    if (__BITMAP_SMALL_CONST_NBITS(nbits)) {
        unsigned long val;

        if (__BITMAP_UNLIKELY(offset >= nbits))
            return nbits;

        val = *addr & __BITMAP_GENMASK(nbits - 1, offset);
        return val ? (unsigned long)__builtin_ctzl(val) : nbits;
    }

    return __find_next_bit(addr, nbits, offset);
}

#endif //BITMAP_H_INCLUDED

#ifdef BITMAP_IMPLEMENTATION

#include <stdlib.h>

bool __bitmap_and(unsigned long *dst, const unsigned long *bitmap1,
                  const unsigned long *bitmap2, unsigned int bits)
{
    unsigned int k;
    unsigned int lim = bits / __BITMAP_BITS_PER_LONG;
    unsigned long result = 0;

    for (k = 0; k < lim; k++) {
        result |= (dst[k] = bitmap1[k] & bitmap2[k]);
    }
    if (bits % __BITMAP_BITS_PER_LONG) {
        result |= (dst[k] = bitmap1[k] & bitmap2[k] &
               __BITMAP_LAST_WORD_MASK(bits));
    }
    return result != 0;
}

void __bitmap_or(unsigned long *dst, const unsigned long *bitmap1,
                 const unsigned long *bitmap2, unsigned int bits)
{
    unsigned int k;
    unsigned int nr = __BITMAP_BITS_TO_LONGS(bits);

    for (k = 0; k < nr; k++) {
        dst[k] = bitmap1[k] | bitmap2[k];
    }
}

void __bitmap_xor(unsigned long *dst, const unsigned long *bitmap1,
                  const unsigned long *bitmap2, unsigned int bits)
{
    unsigned int k;
    unsigned int nr = __BITMAP_BITS_TO_LONGS(bits);

    for (k = 0; k < nr; k++) {
        dst[k] = bitmap1[k] ^ bitmap2[k];
    }
}

void __bitmap_complement(unsigned long *dst, const unsigned long *src, unsigned int bits)
{
    unsigned int k, lim = __BITMAP_BITS_TO_LONGS(bits);
    for (k = 0; k < lim; ++k) {
        dst[k] = ~src[k];
    }
}

bool __bitmap_equal(const unsigned long *bitmap1,
                    const unsigned long *bitmap2, unsigned int bits)
{
    unsigned int k, lim = bits / __BITMAP_BITS_PER_LONG;
    for (k = 0; k < lim; ++k) {
        if (bitmap1[k] != bitmap2[k]) {
            return false;
         }
    }

    if (bits % __BITMAP_BITS_PER_LONG) {
        if ((bitmap1[k] ^ bitmap2[k]) & __BITMAP_LAST_WORD_MASK(bits)) {
            return false;
        }
    }

    return true;
}

bool __bitmap_intersects(const unsigned long *bitmap1,
                         const unsigned long *bitmap2, unsigned int bits)
{
    unsigned int k, lim = bits / __BITMAP_BITS_PER_LONG;
    for (k = 0; k < lim; ++k) {
        if (bitmap1[k] & bitmap2[k]) {
            return true;
        }
    }

    if (bits % __BITMAP_BITS_PER_LONG) {
        if ((bitmap1[k] & bitmap2[k]) & __BITMAP_LAST_WORD_MASK(bits)) {
            return true;
        }
    }

    return false;
}

bool __bitmap_subset(const unsigned long *bitmap1,
                     const unsigned long *bitmap2, unsigned int bits)
{
    unsigned int k, lim = bits / __BITMAP_BITS_PER_LONG;
    for (k = 0; k < lim; ++k) {
        if (bitmap1[k] & ~bitmap2[k]) {
            return false;
        }
    }

    if (bits % __BITMAP_BITS_PER_LONG) {
        if ((bitmap1[k] & ~bitmap2[k]) & __BITMAP_LAST_WORD_MASK(bits)) {
            return false;
        }
    }

    return true;
}

void __bitmap_set(unsigned long *map, unsigned int start, int len)
{
    unsigned long *p = map + start / __BITMAP_BITS_PER_LONG;
    const unsigned int size = start + len;
    int bits_to_set = __BITMAP_BITS_PER_LONG - (start % __BITMAP_BITS_PER_LONG);
    unsigned long mask_to_set = __BITMAP_FIRST_WORD_MASK(start);

    while (len - bits_to_set >= 0) {
        *p |= mask_to_set;
        len -= bits_to_set;
        bits_to_set = __BITMAP_BITS_PER_LONG;
        mask_to_set = ~0UL;
        p++;
    }
    if (len) {
        mask_to_set &= __BITMAP_LAST_WORD_MASK(size);
        *p |= mask_to_set;
    }
}

void __bitmap_clear(unsigned long *map, unsigned int start, int len)
{
    unsigned long *p = map + start/__BITMAP_BITS_PER_LONG;
    const unsigned int size = start + len;
    int bits_to_clear = __BITMAP_BITS_PER_LONG - (start % __BITMAP_BITS_PER_LONG);
    unsigned long mask_to_clear = __BITMAP_FIRST_WORD_MASK(start);

    while (len - bits_to_clear >= 0) {
        *p &= ~mask_to_clear;
        len -= bits_to_clear;
        bits_to_clear = __BITMAP_BITS_PER_LONG;
        mask_to_clear = ~0UL;
        p++;
    }
    if (len) {
        mask_to_clear &= __BITMAP_LAST_WORD_MASK(size);
        *p &= ~mask_to_clear;
    }
}

unsigned long __find_next_bit(const unsigned long *addr,
                              unsigned long nbits, unsigned long offset)
{
    if (offset >= nbits) return nbits;
    unsigned int start = offset / __BITMAP_BITS_PER_LONG;
    unsigned int lim = __BITMAP_BITS_TO_LONGS(nbits);

    unsigned long val, mask;
    for (unsigned int k = start; k < lim; k++) {
        mask = ~0UL;
        if (k == start) {
            mask &= __BITMAP_FIRST_WORD_MASK(offset);
        }
        if (k == lim) {
            mask &= __BITMAP_LAST_WORD_MASK(offset);
        }
        val = addr[k] & mask;
        if (val) {
            return (k * __BITMAP_BITS_PER_LONG) + __builtin_ctzl(val);
        }
    }

    return nbits;
}

unsigned long *bitmap_alloc(unsigned int nbits)
{
    return calloc(__BITMAP_BITS_TO_LONGS(nbits), sizeof(unsigned long));
}

void bitmap_free(const unsigned long *bitmap)
{
    //(void *) is only needed to remove the "discards ‘const’ qualifier" warning.
    free((void *)bitmap);
}

#endif //BITMAP_IMPLEMENTATION

