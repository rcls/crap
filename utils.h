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
#define ARRAY_CALLOC(T,N) ((T *) xcalloc (sizeof (T) * (N)))
#define ARRAY_REALLOC(P,N) ((__typeof__ (P)) xrealloc (P, sizeof (*P) * (N)))

#define ARRAY_EXTEND(P,E) do {                          \
        size_t ITEMS = E - P;                           \
        if (ITEMS & (ITEMS - 1)) {                      \
            ++E;                                        \
            break;                                      \
        }                                               \
        P = ARRAY_REALLOC (P, ITEMS * 2 + (ITEMS ==0)); \
        E = P + ITEMS + 1;                              \
    } while (0)

#endif
