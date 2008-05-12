#ifndef UTILS_H
#define UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

/// Format a string into a malloc'd buffer.
char * xasprintf (const char * format, ...)
    __attribute__ ((malloc, warn_unused_result, format (printf, 1, 2)));

/// Does @c haystack start with @c needle?
static inline bool starts_with (const char * haystack, const char * needle)
{
    return strncmp (haystack, needle, strlen (needle)) == 0;
}


/// Allocate an array with malloc().
#define ARRAY_ALLOC(T,N) ((T *) xmalloc (sizeof (T) * (N)))

/// Allocate an array with calloc().
#define ARRAY_CALLOC(T,N) ((T *) xcalloc (sizeof (T) * (N)))

/// Re-size an array with realloc().
#define ARRAY_REALLOC(P,N) ((__typeof__ (P)) xrealloc (P, sizeof (*P) * (N)))

/// Extend an array by one item.  P_end should be the end pointer.
#define ARRAY_EXTEND(P) do {                            \
        size_t ITEMS = P##_end - P;                     \
        if (ITEMS & (ITEMS - 1)) {                      \
            ++P##_end;                                  \
            break;                                      \
        }                                               \
        P = ARRAY_REALLOC (P, ITEMS * 2 + (ITEMS ==0)); \
        P##_end = P + ITEMS + 1;                        \
    } while (0)

/// Extend an array by one item.  P_end should be the end pointer.
#define ARRAY_APPEND(P,I) do {                                  \
        size_t ITEMS = P##_end - P;                             \
        if ((ITEMS & (ITEMS - 1)) == 0) {                       \
            P = ARRAY_REALLOC (P, ITEMS * 2 + (ITEMS ==0));     \
            P##_end = P + ITEMS;                                \
        }                                                       \
        *(P##_end)++ = I;                                       \
    } while (0)

#endif
