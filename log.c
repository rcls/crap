
#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void bugger (const char * f, ...)
{
    va_list l;
    va_start (l, f);
    vfprintf (stderr, f, l);
    va_end (l);

    fflush (NULL);
    exit (EXIT_FAILURE);
}
