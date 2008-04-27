// Computation of the branch tree.  This is done independently of the main
// commit & branch emission.
//
// Each file imposes some branch/sub-branch dependencies.
//
// We create a weighted graph on the set of branches by counting
// the dependencies from each file.
//
// This weighted graph may have cycles (in theory).  We produce a digraph by
// breaking cycles, removing links from cycles with the least weight.  [We could
// do transitive reduction at this stage also?]
//
// Later, we want to further reduce the digraph to a tree.

#include "branch.h"
#include "database.h"
#include "file.h"
#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static int tag_compare (const void * AA, const void * BB)
{
    const tag_t * A = AA;
    const tag_t * B = BB;
    return A > B;
}


static int compare_pb (const void * AA, const void * BB)
{
    const parent_branch_t * A = AA;
    const parent_branch_t * B = BB;

    // Put biggest weights first.
    if (A->weight > B->weight)
        return -1;

    if (A->weight < B->weight)
        return 1;

    if (A->branch < B->branch)
        return -1;

    if (A->branch > B->branch)
        return 1;

    return 0;
}


static parent_branch_t * unemitted_parent (tag_t * t)
{
    parent_branch_t * i = t->parents_end;
    while (i != t->parents)
        if (!(--i)->branch->is_emitted)
            return i;
    abort();
}


static void split_cycle (heap_t * heap, tag_t * t)
{
    tag_t * slow = t;
    tag_t * fast = t;
    do {
        slow = unemitted_parent (slow)->branch;
        fast = unemitted_parent (unemitted_parent (fast)->branch)->branch;
    }
    while (slow != fast);

    tag_t * best = fast;
    parent_branch_t * best_parent = unemitted_parent (best);
    tag_t * i = best_parent->branch;
    while (i != fast) {
        parent_branch_t * i_parent = unemitted_parent (i);
        if (compare_pb (i_parent, best_parent) > 0) {
            best = i;
            best_parent = i_parent;
        }
        i = i_parent->branch;
    }

    tag_t * parent = best_parent->branch;

    fprintf (stderr, "Break branch cycle link %s child of %s weight %u\n",
             best->tag, parent->tag, best_parent->weight);

    // Remove the parent from the child.
    memmove (best_parent, best_parent + 1,
             sizeof (parent_branch_t) * (best->parents_end - best_parent - 1));
    --best->parents_end;
    if (--best->changeset.unready_count == 0)
        heap_insert (heap, best);

    // Remove the child from the parent.
    for (branch_tag_t * i = parent->tags; i != parent->tags_end; ++i)
        if (i->tag == best) {
            memmove (i, i + 1,
                     sizeof (branch_tag_t) * (parent->tags_end - i - 1));
            return;
        }

    abort();                            // Loop should hit.
}


static bool emit_tag (heap_t * heap)
{
    if (heap->entries == heap->entries_end)
        return false;

    tag_t * tag = heap_pop (heap);
    tag->is_emitted = true;

    fprintf (stderr, "Tag '%s' with %u parents\n",
             tag->tag, tag->parents_end - tag->parents);

    for (parent_branch_t * i = tag->parents; i != tag->parents_end; ++i)
        fprintf (stderr, "\t%s\n", i->branch->tag);

    for (branch_tag_t * i = tag->tags; i != tag->tags_end; ++i) {
        assert (i->tag->changeset.unready_count != 0);
        if (--i->tag->changeset.unready_count == 0)
            heap_insert (heap, i->tag);
    }

    return true;
}


void branch_analyse (database_t * db)
{
    // First, go through each tag, and put it on all the branches.
    for (tag_t * i = db->tags; i != db->tags_end; ++i) {
        i->changeset.unready_count = 0;
        for (file_tag_t ** j = i->tag_files; j != i->tag_files_end; ++j) {
            if ((*j)->version == NULL || (*j)->version->branch == NULL)
                continue;
            tag_t * b = (*j)->version->branch->tag;
            if (b->tags_end != b->tags && b->tags_end[-1].tag == i) {
                ++b->tags_end[-1].weight;
                continue;
            }

            ARRAY_EXTEND (b->tags, b->tags_end);
            b->tags_end[-1].tag = i;
            b->tags_end[-1].weight = 1;
        }
    }

    // Now go through each branch and put it onto each tag.
    for (tag_t * i = db->tags; i != db->tags_end; ++i)
        for (branch_tag_t * j = i->tags; j != i->tags_end; ++j) {
            ARRAY_EXTEND (j->tag->parents, j->tag->parents_end);
            j->tag->parents_end[-1].branch = i;
            j->tag->parents_end[-1].weight = j->weight;
            ++j->tag->changeset.unready_count;
        }

    heap_t heap;
    heap_init (&heap, offsetof (tag_t, changeset.ready_index), tag_compare);

    // Put the tags that are ready right now on to the heap.  Also sort the
    // parents.
    for (tag_t * i = db->tags; i != db->tags_end; ++i) {
        if (i->changeset.unready_count == 0)
            heap_insert (&heap, i);
        qsort (i->parents, i->parents_end - i->parents,
               sizeof (parent_branch_t), compare_pb);
    }

    while (emit_tag (&heap));

    for (tag_t * i = db->tags; i != db->tags_end; ++i)
        while (!i->is_emitted) {
            split_cycle (&heap, i);
            while (emit_tag (&heap));
        }

    free (heap.entries);
}
