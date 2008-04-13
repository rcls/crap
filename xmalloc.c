#include "xmalloc.h"

#include <stdio.h>
#include <stdlib.h>

void * xmalloc (size_t size)
{
    void * r = malloc (size);
    if (r)
        return r;

    fprintf (stderr, "Failed to malloc %zu bytes.\n", size);
    fflush (NULL);
    abort();
}
