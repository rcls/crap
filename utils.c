#include "log.h"
#include "utils.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

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
    if (vasprintf (&result, format, args) < 0)
        fatal ("Failed to format a string: %s\n", strerror (errno));

    va_end (args);

    return result;
}


void * find_string (const void * array, size_t count, size_t size,
                    size_t position, const char * needle)
{
    const char * base = array;

    while (count) {
        size_t mid = count >> 1;
        const char * midp = base + mid * size;
        int c = strcmp (* (const char * const *) (midp + position), needle);
        if (c < 0) {
            base = midp + size;
            count -= mid + 1;
        }
        else if (c > 0)
            count = mid;
        else
            return (void *) midp;
    }

    return NULL;
}

