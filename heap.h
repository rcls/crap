#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>

typedef struct heap {
    void ** entries;
    size_t num_entries;
    size_t max_entries;
    size_t index_offset;
    /**
     * Compare should return >0 if first arg is greater than second, and <=0
     * otherwise.  Thus either a strcmp or a '<' like predicate can be used.  */
    int (*compare) (void *, void *);
} heap_t;


void heap_init (heap_t * heap, size_t offset, int (*compare) (void *, void *));
void heap_destroy (heap_t * heap);

void heap_insert (heap_t * heap, void * item);
void heap_remove (heap_t * heap, void * item);
void * heap_front (heap_t * heap);
void * heap_pop (heap_t * heap);

#endif
