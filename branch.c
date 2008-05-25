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
#include "emission.h"
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


// Release all the child tags of a branch.
static void tag_released (heap_t * heap, tag_t * tag,
                          tag_t *** tree_order, tag_t *** tree_order_end)
{
    ARRAY_APPEND (*tree_order, tag);
    for (branch_tag_t * i = tag->tags; i != tag->tags_end; ++i) {
        assert (i->tag->changeset.unready_count != 0);
        if (--i->tag->changeset.unready_count == 0) {
            i->tag->is_released = true;
            heap_insert (heap, i->tag);
        }
    }
}


static void record_branch_tag (tag_t * branch, tag_t * tag)
{
    if (branch->tags != branch->tags_end && branch->tags_end[-1].tag == tag) {
        ++branch->tags_end[-1].weight;
        return;
    }

    ARRAY_EXTEND (branch->tags);
    branch->tags_end[-1].tag = tag;
    branch->tags_end[-1].weight = 1;
}



// FIXME - we don't cope optimally with the situation where a branch is
// created, files deleted, and then the branch tagged (without rtag).  We'll
// never know that the tag was placed on the branch; instead we'll place the tag
// on the trunk.
static void branch_graph (database_t * db,
                          tag_t *** tree_order, tag_t *** tree_order_end)
{
    // First, go through each tag, and put it on all the branches.
    for (tag_t * i = db->tags; i != db->tags_end; ++i) {
        i->changeset.unready_count = 0;
        for (file_tag_t ** j = i->tag_files; j != i->tag_files_end; ++j) {
            if ((*j)->version == NULL)
                continue;

            if ((*j)->version->branch)
                record_branch_tag ((*j)->version->branch->tag, i);

            if ((*j)->version != (*j)->file->versions &&
                (*j)->version[-1].implicit_merge &&
                (*j)->version[-1].used &&
                (*j)->version[-1].branch)
                record_branch_tag ((*j)->version[-1].branch->tag, i);
        }
    }

    // Go through each branch and put record it on the tags.
    for (tag_t * i = db->tags; i != db->tags_end; ++i)
        for (branch_tag_t * j = i->tags; j != i->tags_end; ++j) {
            ARRAY_EXTEND (j->tag->parents);
            j->tag->parents_end[-1].branch = i;
            j->tag->parents_end[-1].weight = j->weight;
            ++j->tag->changeset.unready_count;
        }

    // Do a cycle breaking pass of the branches.
    heap_t heap;

    heap_init (&heap, offsetof (tag_t, changeset.ready_index), tag_compare);

    // Release all the tags that are ready right now; also sort the parent
    // lists.
    for (tag_t * i = db->tags; i != db->tags_end; ++i) {
        qsort (i->parents, i->parents_end - i->parents,
               sizeof (parent_branch_t), compare_pb);
        if (i->changeset.unready_count == 0) {
            i->is_released = true;
            heap_insert (&heap, i);
        }
    }

    while (!heap_empty (&heap))
        tag_released (&heap, heap_pop (&heap), tree_order, tree_order_end);

    for (tag_t * i = db->tags; i != db->tags_end; ++i)
        while (!i->is_released) {
            break_cycle (&heap, i);
            while (!heap_empty (&heap))
                tag_released (&heap, heap_pop (&heap),
                              tree_order, tree_order_end);
        }

    heap_destroy (&heap);
}


static bool better_than (tag_t * new, tag_t * old)
{
    // FIXME - the actual test should be something like lower-rank-branches win;
    // for equal rank, deterministicly order tags.
    return false;
}


static bool is_on_branch (database_t * db, tag_t * branch, version_t * version)
{
    if (version == NULL || version->dead)
        return false;

    if (version->branch && version->branch->tag == branch)
        return true;

    file_tag_t * bt = find_file_tag (version->file, branch);
    return bt != NULL && bt->version == version;
}


static void branch_tag_point (database_t * db, tag_t * branch, tag_t * tag)
{
    changeset_t * best_cs = &branch->changeset;
    ssize_t hit = 0;
    ssize_t best_hit = 0;
    ssize_t extra = 0;
    ssize_t best_extra = 0;

    for (changeset_t ** i = branch->changeset.children;
         i != branch->changeset.children_end; ++i) {
        changeset_t * cs = *i;
        if (cs->type == ct_tag)
            continue;               // Ignore child tags.
        for (version_t ** j = cs->versions; j != cs->versions_end; ++j) {
            if (!(*j)->used)
                continue;
            file_tag_t * ft = find_file_tag ((*j)->file, tag);
            if (ft == NULL || ft->version == NULL) {
                extra -= is_on_branch (db, branch, (*j)->parent);
                extra += is_on_branch (db, branch, *j);
                continue;
            }
            assert (!ft->version->implicit_merge);
            if (ft->version == version_normalise (*j))
                ++hit;
            else if (ft->version == version_normalise ((*j)->parent))
                // FIXME check parent actually on branch.
                --hit;
        }
        if (hit > best_hit || (hit == best_hit && extra < best_extra)) {
            hit = best_hit;
            extra = best_extra;
            best_cs = cs;
        }
    }

    tag->parent = best_cs;
    ARRAY_APPEND (best_cs->children, &tag->changeset);

    // FIXME Now walk through the changesets a second time; each time we
    // come to a child tag, generate any needed fix-up.
}


/// Choose which branch to put a tag.  We choose the branch with the largest
/// number of tag versions.
static void branch_choose (tag_t * tag)
{
    size_t best_weight = 0;
    tag_t * best_branch = NULL;
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
            version_t * tv = (*j++)->version;
            version_t * bv = (*jj++)->version;
            if (!tv)
                continue;
            // We cound the branch if (a) the tag version is on the branch for
            // this file, (b) the tag version is the branch point, (c) the
            // tag version is an implicit merge and the branch we are
            // considering is the trunk.
            if ((tv->branch && tv->branch->tag == i->branch) || tv == bv
                || (i->branch->tag[0] == 0 && tv != tv->file->versions_end
                    && tv[1].implicit_merge && tv[1].used))                 
                ++weight;
        }
        if (weight > best_weight
            || (weight == best_weight
                && better_than (i->branch, best_branch))) {
            best_weight = weight;
            best_branch = i->branch;
        }
    }
    if (best_branch) {
        fprintf (stderr, "Tag '%s' placing on branch '%s'\n",
                 tag->tag, best_branch->tag);
        tag->parent = &best_branch->changeset;
    }
    else
        tag->parent = NULL;
    xfree (tag->parents);
    tag->parents = NULL;
    tag->parents_end = NULL;
    xfree (tag->tags);
    tag->tags = NULL;
    tag->tags_end = NULL;
}


static void branch_changesets (database_t * db)
{
    // Do a pass through the changesets, assigning changesets to their branches.
    // This will place the changesets in emission order.
    prepare_for_emission (db, NULL);

    changeset_t * cs;
    while ((cs = next_changeset (db))) {
        assert (cs->type == ct_commit);
        changeset_emitted (db, NULL, cs);
        changeset_update_branch_versions (db, cs);
        if (cs->versions[0]->branch == NULL)
            continue;               // Anonymous branch: skip.
        tag_t * branch = cs->versions[0]->branch->tag;
        ARRAY_APPEND (branch->changeset.children, cs);
    }
}


void branch_analyse (database_t * db)
{
    branch_changesets (db);

    tag_t ** tree_order = NULL;
    tag_t ** tree_order_end = NULL;

    branch_graph (db, &tree_order, &tree_order_end);

    // Choose the branch on which to place each tag.
    for (tag_t * i = db->tags; i != db->tags_end; ++i)
        branch_choose (i);

    // Choose the changeset on which to place each tag.
    for (tag_t * i = db->tags; i != db->tags_end; ++i)
        if (i->parent)
            branch_tag_point (db, as_tag (i->parent), i);

    // Set the timestamps on the tags.
    for (tag_t ** i = tree_order; i != tree_order_end; ++i)
        if ((*i)->parent)
            (*i)->changeset.time = (*i)->parent->time;
        else
            (*i)->changeset.time = 0;

    xfree (tree_order);
}
