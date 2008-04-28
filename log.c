
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

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

    fflush (NULL);
    exit (EXIT_FAILURE);
}
