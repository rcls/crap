#ifndef UTILS_H
#define UTILS_H

#include <stddef.h>

void * xmalloc (size_t size)
    __attribute__ ((__malloc__, __warn_unused_result__));

void * xrealloc (void * ptr, size_t size)
    __attribute__ ((__warn_unused_result__));

void * xcalloc (size_t size)
    __attribute__ ((__malloc__, __warn_unused_result__));

#define ARRAY_EXTEND(P,S,M) do { if (++S > M) { \
            M = M ? M + M/2 : 8;                \
            P = xrealloc (P, M * sizeof (*P));  \
        } } while (0);

#endif
