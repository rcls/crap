#include "xmalloc.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>

void * xmalloc (size_t size)
{
    void * r = malloc (size);
    if (r == NULL)
        bugger ("Failed to malloc %zu bytes.\n", size);

    return r;
}


void * xrealloc (void * old, size_t size)
{
    void * r = realloc (old, size);
    if (r == NULL)
        bugger ("Failed to realloc %zu bytes.\n", size);

    return r;
}

