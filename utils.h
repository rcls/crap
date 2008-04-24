#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

void * xmalloc (size_t size)
    __attribute__ ((__malloc__, __warn_unused_result__));

void * xrealloc (void * ptr, size_t size)
    __attribute__ ((__warn_unused_result__));

void * xcalloc (size_t size)
    __attribute__ ((__malloc__, __warn_unused_result__));

#define ARRAY_ALLOC(T,N) ((T *) xmalloc (sizeof (T) * (N)))
#define ARRAY_REALLOC(P,N) ((__typeof__ (P)) xrealloc (P, sizeof (*P) * (N)))

#define ARRAY_EXTEND(P,E,M) do { if (E != M) { ++E; break; }    \
        size_t ITEMS = E - P + 1;                               \
        P = ARRAY_REALLOC (P, 2 * ITEMS);                       \
        E = P + ITEMS;                                          \
        M = E + ITEMS;                                          \
    } while (0);

#endif
