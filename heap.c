#include "heap.h"
#include "utils.h"

#include <assert.h>
#include <stdint.h>

#define INDEX(P) *((size_t *) (heap->index_offset + (char *) (P)))
#define LESS(P,Q) (heap->compare (Q, P) > 0)

void heap_init (heap_t * heap, size_t offset,
                int (*compare) (const void *, const void *))
{
    heap->entries = NULL;
    heap->entries_end = NULL;
    heap->entries_max = NULL;
    heap->index_offset = offset;
    heap->compare = compare;
}


/**
 * The heap has a bubble at @c position; shuffle the bubble downwards to an
 * appropriate point, and place @c item in it.  */
static void shuffle_down (heap_t * heap, size_t position, void * item)
{
    size_t num_entries = heap->entries_end - heap->entries;
    while (1) {
        size_t child = position * 2 + 1;
        if (child + 1 > num_entries)
            break;

        if (child + 1 < num_entries
            && LESS (heap->entries[child + 1], heap->entries[child]))
            ++child;
        
        if (LESS (item, heap->entries[child]))
            break;

        heap->entries[position] = heap->entries[child];
        INDEX (heap->entries[position]) = position;
        position = child;
    }

    heap->entries[position] = item;
    INDEX (item) = position;
}


/**
 * The heap has a bubble at @c position; shuffle the bubble upwards as far as
 * might be needed to insert @c item, and then call @c shuffle_down.  */
static void shuffle_up (heap_t * heap, size_t position, void * item)
{
    while (position > 0) {
        size_t parent = (position - 1) >> 1;
        if (!LESS (item, heap->entries[parent]))
            break;

        heap->entries[position] = heap->entries[parent];
        INDEX (heap->entries[position]) = position;
        position = parent;
    }

    shuffle_down (heap, position, item);
}


void heap_insert (heap_t * heap, void * item)
{
    assert (INDEX (item) == SIZE_MAX);

    /* Create a bubble at the end.  */
    ARRAY_EXTEND (heap->entries, heap->entries_end, heap->entries_max);

    shuffle_up (heap, heap->entries_end - heap->entries - 1, item);
}


void heap_replace (heap_t * heap, void * old, void * new)
{
    assert (INDEX (old) != SIZE_MAX);
    assert (INDEX (new) == SIZE_MAX);

    shuffle_up (heap, INDEX (old), new);

    INDEX (old) = SIZE_MAX;
}


void heap_remove (heap_t * heap, void * item)
{
    assert (INDEX (item) != SIZE_MAX);

    --heap->entries_end;
    if (item != *heap->entries_end)
        /* Shuffle the item from the end into the bubble.  */
        shuffle_up (heap, INDEX (item), *heap->entries_end);

    INDEX (item) = SIZE_MAX;
}


void * heap_front (heap_t * heap)
{
    assert (heap->entries != heap->entries_end);
    return heap->entries[0];
}


void * heap_pop (heap_t * heap)
{
    assert (heap->entries != heap->entries_end);
    void * result = heap->entries[0];
    if (--heap->entries_end != heap->entries)
        shuffle_down (heap, 0, *heap->entries_end);
    return result;
}
