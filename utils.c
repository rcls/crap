#include "log.h"
#include "utils.h"

#include <stdarg.h>
#include <stdlib.h>

void * xmalloc (size_t size)
{
    // Round up to keep valgrind v. strlen happy.
    void * r = malloc ((size + 3) & ~3);
    if (r == NULL && size != 0)
        fatal ("Failed to malloc %zu bytes.\n", size);

    return r;
}


void * xrealloc (void * old, size_t size)
{
    void * r = realloc (old, size);
    if (r == NULL && size != 0)
        fatal ("Failed to realloc %zu bytes.\n", size);

    return r;
}


void * xcalloc (size_t size)
{
    void * r = calloc (size, 1);
    if (r == NULL && size != 0)
        fatal ("Failed to malloc %zu bytes.\n", size);

    return r;
}


void xfree (const void * p)
{
    free ((void *) p);
}


char * xstrdup (const char * s)
{
    char * r = strdup (s);
    if (r == NULL)
        fatal ("Failed to strdup %zu bytes.\n", strlen (s));

    return r;
}


char * xasprintf (const char * format, ...)
{
    va_list args;
    va_start (args, format);

    char * result;
    check (vasprintf (&result, format, args), "Formatting a string");

    va_end (args);

    return result;
}


int compare_paths (const char * A, const char * B)
{
    for (; *A && *A == *B; ++A, ++B);
    if (*A == *B)
        return 0;                       // Equal.
    if (*A == '/')
        return -1;
    if (*B == '/')
        return 1;
    return *A < *B ? -1 : 1;
}


void * find_string (const void * array, size_t count, size_t size,
                    size_t position, const char * needle)
{
    const char * base = array + position;

    while (count) {
        size_t mid = count >> 1;
        const char * midp = base + mid * size;
        int c = strcmp (* (const char * const *) midp, needle);
        if (c < 0) {
            base = midp + size;
            count -= mid + 1;
        }
        else if (c > 0)
            count = mid;
        else
            return (void *) (midp - position);
    }

    return NULL;
}

