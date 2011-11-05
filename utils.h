#ifndef UTILS_H
#define UTILS_H

#include <stdarg.h>
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

/// Call strdup and die on error.
char * xstrdup (const char * s)
    __attribute__ ((__malloc__, __warn_unused_result__));

/// Format a string into a malloc'd buffer.
char * xasprintf (const char * format, ...)
    __attribute__ ((malloc, warn_unused_result, format (printf, 1, 2)));

/// Binary search for the string @c needle in an @c array of @c count items of
/// @c size bytes each, with a string pointer at offset @c position.
void * find_string (const void * array, size_t count, size_t size,
                    size_t position, const char * needle);

/// Does @c haystack start with @c needle?
static inline bool starts_with (const char * haystack, const char * needle)
{
    return strncmp (haystack, needle, strlen (needle)) == 0;
}


static inline bool ends_with (const char * haystack, const char * needle)
{
    size_t h_len = strlen (haystack);
    size_t n_len = strlen (needle);
    return h_len >= n_len
        && memcmp (haystack + h_len - n_len, needle, n_len) == 0;
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
            P = ARRAY_REALLOC (P, ITEMS * 2 + (ITEMS == 0));    \
            P##_end = P + ITEMS;                                \
        }                                                       \
        *(P##_end)++ = I;                                       \
    } while (0)

/// Resize an array to it's exact size.
#define ARRAY_TRIM(P) do {                      \
        if (P != P##_end) {                     \
            size_t ITEMS = P##_end - P;         \
            P = ARRAY_REALLOC (P, ITEMS);       \
            P##_end = P + ITEMS;                \
        } } while (0)

/// Sort an array using qsort.
#define ARRAY_SORT(P, F) qsort (P, P##_end - P, sizeof(*(P)), F)

#endif
