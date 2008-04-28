#include "log.h"
#include "utils.h"

#include <stdlib.h>

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
