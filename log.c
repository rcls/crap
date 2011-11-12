
#include "log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void warning (const char * f, ...)
{
    va_list l;
    va_start (l, f);
    vfprintf (stderr, f, l);
    va_end (l);
}


void fatal (const char * f, ...)
{
    va_list l;
    va_start (l, f);
    vfprintf (stderr, f, l);
    va_end (l);

    exit (EXIT_FAILURE);
}


ssize_t check (ssize_t val, const char * f, ...)
{
    if (val >= 0)
        return val;

    const char * e = strerror(errno);

    va_list l;
    va_start (l, f);
    vfprintf (stderr, f, l);
    va_end (l);
    fprintf (stderr, " failed: %s\n", e);

    exit (EXIT_FAILURE);
}
