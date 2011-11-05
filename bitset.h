#ifndef BITSET_H
#define BITSET_H

#include "utils.h"

#include <limits.h>

/// A bitset that maintains a number of set bits.
typedef struct bitset {
    unsigned long * bits;
    size_t count;
} bitset_t;


static inline void bitset_init (bitset_t * bs, size_t size)
{
    bs->bits = ARRAY_CALLOC (unsigned long,
                             (size + LONG_BIT - 1) / LONG_BIT);
    bs->count = 0;
}


static inline void bitset_destroy (bitset_t * bs)
{
    xfree (bs->bits);
}


static inline void bitset_set (bitset_t * bs, size_t bit)
{
    unsigned long * word = bs->bits + bit / LONG_BIT;
    unsigned long old = *word;
    *word |= 1ul << bit % LONG_BIT;
    if (old != *word)
        ++bs->count;
}


static inline void bitset_reset (bitset_t * bs, size_t bit)
{
    unsigned long * word = bs->bits + bit / LONG_BIT;
    unsigned long old = *word;
    *word &= ~(1ul << bit % LONG_BIT);
    if (old != *word)
        --bs->count;
}

#endif
