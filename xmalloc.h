#ifndef XMALLOC_H
#define XMALLOC_H

#include <stddef.h>

void * xmalloc (size_t size)
    __attribute__ ((__malloc__, __warn_unused_result__));

void * xrealloc (void * ptr, size_t size)
    __attribute__ ((__warn_unused_result__));

#endif
