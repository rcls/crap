#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>
#include <stdio.h>

/// Call malloc and die on error.
void * xmalloc (size_t size)
    __attribute__ ((__malloc__, __warn_unused_result__));

/// Call realloc and die on error.
void * xrealloc (void * ptr, size_t size)
    __attribute__ ((__warn_unused_result__));

/// Call calloc and die on error.
void * xcalloc (size_t size)
    __attribute__ ((__malloc__, __warn_unused_result__));

/// Call free.
void xfree (const void * p);

/// Call getline and do some sanity checking.
size_t next_line (char ** line, size_t * len, FILE * stream);

#define ARRAY_ALLOC(T,N) ((T *) xmalloc (sizeof (T) * (N)))
#define ARRAY_CALLOC(T,N) ((T *) xcalloc (sizeof (T) * (N)))
#define ARRAY_REALLOC(P,N) ((__typeof__ (P)) xrealloc (P, sizeof (*P) * (N)))

#define ARRAY_EXTEND(P) do {                            \
        size_t ITEMS = P##_end - P;                     \
        if (ITEMS & (ITEMS - 1)) {                      \
            ++P##_end;                                  \
            break;                                      \
        }                                               \
        P = ARRAY_REALLOC (P, ITEMS * 2 + (ITEMS ==0)); \
        P##_end = P + ITEMS + 1;                        \
    } while (0)

#define ARRAY_APPEND(P,I) do {                                  \
        size_t ITEMS = P##_end - P;                             \
        if ((ITEMS & (ITEMS - 1)) == 0) {                       \
            P = ARRAY_REALLOC (P, ITEMS * 2 + (ITEMS ==0));     \
            P##_end = P + ITEMS;                                \
        }                                                       \
        *(P##_end)++ = I;                                       \
    } while (0)

#endif
