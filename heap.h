#ifndef HEAP_H
#define HEAP_H

#include <stddef.h>

/// Type used to store a heap.
typedef struct heap {
    void ** entries;
    void ** entries_end;
    size_t index_offset;
    /// @c compare should return >0 if first arg is greater than second, and <=0
    /// otherwise.  Thus either a strcmp or a '>' like predicate can be used.
    int (*compare) (const void *, const void *);
} heap_t;


/// Initialise a new heap.
void heap_init (heap_t * heap, size_t offset,
                int (*compare) (const void *, const void *));

/// Insert an item.
void heap_insert (heap_t * heap, void * item);

/// Remove an item.
void heap_remove (heap_t * heap, void * item);

/// Return least item from a heap.
void * heap_front (heap_t * heap);

/// Return least item from a heap, after removing it.
void * heap_pop (heap_t * heap);

#endif
