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
    // FIXME - we should be more deterministic - i.e., the choice of parent
    // should be more obviously related to external observables.
    parent_branch_t * i = t->parents_end;
    while (i != t->parents)
        if (!(--i)->branch->is_released)
            return i;
    abort();
}


static void break_cycle (heap_t * heap, tag_t * t)
{
    // Find a cycle.
    tag_t * slow = t;
    tag_t * fast = t;
    do {
        slow = unemitted_parent (slow)->branch;
        fast = unemitted_parent (unemitted_parent (fast)->branch)->branch;
    }
    while (slow != fast);

    // Walk through the cycle again, finding the parent with the lowest weight.
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


// FIXME - we don't cope optimally with the situation where a branch is
// created, files deleted, and then the branch tagged (without rtag).  We'll
// never know that the tag was placed on the branch; instead we'll place the tag
// on the trunk.
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

            ARRAY_EXTEND (b->tags);
            b->tags_end[-1].tag = i;
            b->tags_end[-1].weight = 1;
        }
    }

    // Now go through each branch and put it onto each tag.
    for (tag_t * i = db->tags; i != db->tags_end; ++i)
        for (branch_tag_t * j = i->tags; j != i->tags_end; ++j) {
            ARRAY_EXTEND (j->tag->parents);
            j->tag->parents_end[-1].branch = i;
            j->tag->parents_end[-1].weight = j->weight;
            ++j->tag->changeset.unready_count;
        }

    // Sort the parent lists.
    for (tag_t * i = db->tags; i != db->tags_end; ++i)
        qsort (i->parents, i->parents_end - i->parents,
               sizeof (parent_branch_t), compare_pb);

    // Now do a cycle breaking pass of the branches.
    heap_t heap;

    heap_init (&heap, offsetof (tag_t, changeset.ready_index), tag_compare);

    // Put the tags that are ready right now on to the heap.  Also sort the
    // parents.
    for (tag_t * i = db->tags; i != db->tags_end; ++i) {
        i->changeset.unready_count = i->parents_end - i->parents;
        if (i->changeset.unready_count == 0) {
            i->is_released = true;
            heap_insert (&heap, i);
        }
    }

    while (branch_heap_next (&heap));

    for (tag_t * i = db->tags; i != db->tags_end; ++i)
        while (!i->is_released) {
            break_cycle (&heap, i);
            while (branch_heap_next (&heap));
        }

    heap_destroy (&heap);
}


tag_t * branch_heap_next (heap_t * heap)
{
    if (heap_empty (heap))
        return NULL;

    tag_t * tag = heap_pop (heap);

    for (branch_tag_t * i = tag->tags; i != tag->tags_end; ++i) {
        assert (i->tag->changeset.unready_count != 0);
        if (--i->tag->changeset.unready_count == 0 && !i->tag->is_released) {
            i->tag->is_released = true;
            heap_insert (heap, i->tag);
        }
    }

    return tag;
}


static bool better_than (tag_t * new, tag_t * old)
{
    // FIXME - the actual test should be something like lower-rank-branches win;
    // for equal rank, deterministicly order tags.
    return false;
}


void assign_tag_point (database_t * db, tag_t * tag)
{
    const char * bt = tag->branch_versions ? "Branch" : "Tag";

    // Exact matches have already assigned tag points.
    if (tag->exact_match) {
        fprintf (stderr, "%s '%s' already exactly matched\n", bt, tag->tag);
        return;
    }

    // Some branches have no parents.  I think this should only be the trunk.
    if (tag->parents == tag->parents_end) {
        fprintf (stderr, "%s '%s' has no parents\n", bt, tag->tag);
        return;
    }

    // We're going to do this the hard way.
    size_t best_weight = 0;
    tag_t * best_branch = NULL;

    // First of all, check to see which parent branches contain the most
    // versions from the tag.
    // FIXME - no need to do this if only one parent.
    for (parent_branch_t * i = tag->parents; i != tag->parents_end; ++i) {
        size_t weight = 1;
        file_tag_t ** j = tag->tag_files;
        file_tag_t ** jj = i->branch->tag_files;
        while (j != tag->tag_files_end && jj != i->branch->tag_files_end) {
            if ((*j)->file < (*jj)->file) {
                ++j;
                continue;
            }
            if ((*j)->file > (*jj)->file) {
                ++jj;
                continue;
            }
            // FIXME - misses vendor imports that get merged.
            if (((*j)->version && (*j)->version->branch
                 && (*j)->version->branch->tag == i->branch)
                || (*j)->version == (*jj)->version)
                ++weight;
            ++j;
            ++jj;
        }
        if (weight > best_weight ||
            (weight == best_weight && better_than (i->branch, best_branch))) {
            best_weight = weight;
            best_branch = i->branch;
        }
    }

    // We should now have a branch to use.  Now we need to find at what point
    // on the branch to place the tag.  We walk through the branch changesets,
    // keeping tabs on how many file versions match.  The one with the most
    // matches wins.
    assert (best_branch != NULL);

    fprintf (stderr, "%s '%s' placed on branch '%s'\n",
             bt, tag->tag, best_branch->tag);

    ssize_t current = 0;
    ssize_t best = 0;
    changeset_t * best_cs = &best_branch->changeset;

    for (changeset_t ** i = best_branch->changeset.children;
         i != best_branch->changeset.children_end; ++i) {
        // Go through the changeset versions; if it matches the tag version,
        // then increment current; if the previous version matches the tag
        // version, then decrement current.  Just to make life fun, the
        // changeset versions are not sorted by file, so we have to search for
        // them.  FIXME - again, this misses vendor imports.
        version_t * v;
        if ((*i)->type == ct_commit)
            v = (*i)->versions;
        else if ((*i)->type == ct_implicit_merge)
            v = (*i)->parent->versions;
        else
            continue;                   // Tags play no role here.
        for (version_t * j = v; j; j = j->cs_sibling) {
            if ((*i)->type == ct_implicit_merge && !j->implicit_merge)
                continue;
            file_tag_t * ft = find_file_tag (j->file, tag);
            // FIXME - we should process ft->version==NULL.
            if (ft == NULL || ft->version == NULL)
                continue;
            if (ft->version == j)
                ++current;
            else if (ft->version == j->parent)
                --current;
        }
        if (current > best) {
            best = current;
            best_cs = *i;
        }
    }

    // Set the tag as a child of the changeset.
    changeset_add_child (best_cs, &tag->changeset);
}
