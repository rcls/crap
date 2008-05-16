#include "changeset.h"
#include "database.h"
#include "emission.h"
#include "file.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void changeset_release (database_t * db, changeset_t * cs)
{
    assert (cs->unready_count != 0);

    if (--cs->unready_count == 0)
        heap_insert (&db->ready_changesets, cs);
}

void version_release (database_t * db, heap_t * version_heap,
                      version_t * version)
{
    if (version_heap)
        heap_insert (version_heap, version);

    changeset_release (db, version->commit);
}


void changeset_emitted (database_t * db, heap_t * ready_versions,
                        changeset_t * changeset)
{
    /* FIXME - this could just as well be merged into next_changeset. */

    if (changeset->type == ct_commit)
        for (version_t * i = changeset->versions; i; i = i->cs_sibling) {
            if (ready_versions)
                heap_remove (ready_versions, i);
            for (version_t * v = i->children; v; v = v->sibling)
                version_release (db, ready_versions, v);
        }

    for (changeset_t ** i = changeset->children;
         i != changeset->children_end; ++i)
        changeset_release (db, *i);
}


static bool can_replace_with_implicit_merge (const version_t * v)
{
    if (v == NULL || v->implicit_merge)
        return true;

    return strcmp (v->version, "1.1") == 0 && !v->dead
        && strcmp (v->log, "Initial revision\n") == 0;
}


size_t changeset_update_branch_versions (struct database * db,
                                         struct changeset * changeset)
{
    if (changeset->versions->branch == NULL)
        // FIXME - what should we do about changesets on anonymous branches?
        // Stringing them together into branches is probably more bother
        // than it's worth, so we should probably really just never actually
        // create those changesets.
        return 0;                   // Changeset on unknown branch.

    version_t ** branch = changeset->versions->branch->tag->branch_versions;
    version_t * versions = changeset->versions;
    size_t changes = 0;

    for (version_t * i = versions; i; i = i->cs_sibling) {
        version_t ** bv = &branch[i->file - db->files];
        i->used = !i->implicit_merge || can_replace_with_implicit_merge (*bv);

        if (i->used && *bv != i) {
            // We need to keep dead versions here, because dead versions block
            // implicit merges of vendor imports.
            *bv = i;
            ++changes;
        }
    }

    return changes;
}


static const version_t * preceed (const version_t * v)
{
    // If cs is not ready to emit, then some version in cs is blocked.  The
    // earliest un-emitted ancestor of that version will be ready to emit.
    // Search for it.  FIXME We should be a bit smarter by searching harder for
    // the oldest possible version.
    for (version_t * csv = v->commit->versions; csv; csv = csv->cs_sibling)
        if (csv->ready_index == SIZE_MAX)
            for (version_t * v = csv->parent; v; v = v->parent)
                if (v->ready_index != SIZE_MAX)
                    return v;

    abort();
}


static void cycle_split (database_t * db, changeset_t * cs)
{
    // FIXME - the changeset may have an implicit merge; we should then split
    // the implicit merge also.
    fflush (NULL);
    fprintf (stderr, "*********** CYCLE **********\n");
    // We split the changeset into to.  We leave all the blocked versions
    // in cs, and put the ready-to-emit into nw.

    changeset_t * new = database_new_changeset (db);
    new->type = ct_commit;
    new->time = cs->time;
    version_t ** cs_v = &cs->versions;
    version_t ** new_v = &new->versions;
    for (version_t * v = cs->versions; v; v = v->cs_sibling) {
        if (v->ready_index == SIZE_MAX) {
            // Blocked; stays in cs.
            *cs_v = v;
            cs_v = &v->cs_sibling;
        }
        else {
            // Ready-to-emit; goes into new.
            v->commit = new;
            *new_v = v;
            new_v = &v->cs_sibling;
        }
    }

    *cs_v = NULL;
    *new_v = NULL;
    assert (cs->versions);
    assert (new->versions);

    heap_insert (&db->ready_changesets, new);

    fprintf (stderr, "Changeset %s %s\n%s\n",
             cs->versions->branch ? cs->versions->branch->tag->tag : "",
             cs->versions->author, cs->versions->log);
    for (const version_t * v = new->versions; v; v = v->cs_sibling)
        fprintf (stderr, "    %s:%s\n", v->file->path, v->version);

    fprintf (stderr, "Deferring:\n");

    for (const version_t * v = cs->versions; v; v = v->cs_sibling)
        fprintf (stderr, "    %s:%s\n", v->file->path, v->version);
}


static const version_t * cycle_find (const version_t * v)
{
    const version_t * slow = v;
    const version_t * fast = v;
    do {
        slow = preceed (slow);
        fast = preceed (preceed (fast));
    }
    while (slow != fast);
    return slow;
}


changeset_t * next_changeset_split (database_t * db, heap_t * ready_versions)
{
    if (heap_empty (ready_versions))
        return NULL;

    if (heap_empty (&db->ready_changesets)) {
        // Find a cycle.
        const version_t * slow = heap_front (ready_versions);
        const version_t * fast = slow;
        do {
            slow = preceed (slow);
            fast = preceed (preceed (fast));
        }
        while (slow != fast);

        // And split it.
        cycle_split (db, cycle_find (heap_front (ready_versions))->commit);

        assert (db->ready_changesets.entries
                != db->ready_changesets.entries_end);
    }

    return heap_pop (&db->ready_changesets);
}


changeset_t * next_changeset (database_t * db)
{
    if (heap_empty (&db->ready_changesets))
        return NULL;
    else
        return heap_pop (&db->ready_changesets);
}


void prepare_for_emission (database_t * db, heap_t * ready_versions)
{
    // Re-do the changeset unready counts.
    for (changeset_t ** i = db->changesets; i != db->changesets_end; ++i) {
        if ((*i)->type == ct_commit)
            for (version_t * j = (*i)->versions; j; j = j->cs_sibling)
                ++(*i)->unready_count;

        for (changeset_t ** j = (*i)->children; j != (*i)->children_end; ++j)
            ++(*j)->unready_count;
    }

    // Mark the initial versions as ready to emit.
    for (file_t * f = db->files; f != db->files_end; ++f)
        for (version_t * j = f->versions; j != f->versions_end; ++j)
            if (j->parent == NULL)
                version_release (db, ready_versions, j);
}
