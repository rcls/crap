#include "log.h"
#include "utils.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

void * xmalloc (size_t size)
{
    void * r = malloc (size);
    if (r == NULL)
        fatal ("Failed to malloc %zu bytes.\n", size);

    return r;
}


void * xrealloc (void * old, size_t size)
{
    void * r = realloc (old, size);
    if (r == NULL)
        fatal ("Failed to realloc %zu bytes.\n", size);

    return r;
}


void * xcalloc (size_t size)
{
    void * r = calloc (size, 1);
    if (r == NULL)
        fatal ("Failed to malloc %zu bytes.\n", size);

    return r;
}


void xfree (const void * p)
{
    free ((void *) p);
}


size_t next_line (char ** line, size_t * len, FILE * stream)
{
    ssize_t s = getline (line, len, stream);
    if (s < 0)
        fatal ("Unexpected EOF from server.\n");

    if (strlen (*line) < s)
        fatal ("Got line containing ASCII NUL from server.\n");

    if (s > 0 && (*line)[s - 1] == '\n') {
        --s;
        (*line)[s] = 0;
    }

    return s;
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
