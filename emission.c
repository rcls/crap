#include "changeset.h"
#include "database.h"
#include "emission.h"
#include "file.h"

#include <assert.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>

void version_release (database_t * db, version_t * version)
{
    heap_insert (&db->ready_versions, version);

    assert (version->changeset->unready_count != 0);

    if (--version->changeset->unready_count == 0)
        heap_insert (&db->ready_changesets, version->changeset);
}


void changeset_emitted (database_t * db, changeset_t * changeset)
{
    if (changeset->type == ct_commit && changeset->implicit_merge != NULL) {
        assert (db->pending_implicit_merge == NULL);
        db->pending_implicit_merge = changeset->implicit_merge;
    }
    if (changeset->type == ct_implicit_merge)
        return;

    for (version_t * cs_v = changeset->versions;
         cs_v; cs_v = cs_v->cs_sibling) {
        heap_remove (&db->ready_versions, cs_v);
        for (version_t * v = cs_v->children; v; v = v->sibling)
            version_release (db, v);
    }
}

#include <string.h>
size_t changeset_update_branch (struct database * db,
                                struct changeset * changeset)
{
    version_t ** branch;
    bool implicit_merge = false;
    if (changeset->type == ct_implicit_merge) {
        assert (db->tags[0].tag[0] == 0);
        branch = db->tags[0].branch_versions;
        assert (branch);
        changeset = changeset->implicit_merge;
        implicit_merge = true;
    }
    else if (changeset->versions->branch == NULL)
        /* FIXME - what should we do about changesets on anonymous branches?
         * Stringing them together into branches is probably more bother than
         * it's worth, so we should probably really just never actually create
         * those changesets.  */
        return 0;                       /* Changeset on unknown branch.  */
    else
        branch = changeset->versions->branch->tag->branch_versions;

    size_t changes = 0;
    for (version_t * i = changeset->versions; i; i = i->cs_sibling) {
        if (implicit_merge && !i->implicit_merge)
            continue;
        version_t * v = i->dead ? NULL : i;
        if (branch[i->file - db->files] != v) {
            branch[i->file - db->files] = v;
            ++changes;
        }
    }

    if (changes == 0)
        return 0;

    /* Compute the SHA1 hash of the current branch state.  */
    SHA_CTX sha;
    SHA1_Init (&sha);
    version_t ** branch_end = branch + (db->files_end - db->files);
    printf ("Versions:\n");
    for (version_t ** i = branch; i != branch_end; ++i)
        if (*i != NULL && !(*i)->dead) {
            printf (" %s %s", (*i)->file->rcs_path + 17, (*i)->version);
            SHA1_Update (&sha, i, sizeof (version_t *));
        }
    printf ("\n");

    uint32_t hash[5];
    SHA1_Final ((unsigned char *) hash, &sha);

    /* Iterate over all the tags that match.  */
    for (tag_t * i = database_tag_hash_find (db, hash); i;
         i = database_tag_hash_next (i)) {
        printf ("*** HIT %s %s%s ***\n",
                i->branch_versions ? "BRANCH" : "TAG", i->tag,
                i->is_emitted ? " (DUPLICATE)" : "");
        i->is_emitted = true;
    }

    return changes;
}


static const version_t * preceed (const version_t * v)
{
    /* If cs is not ready to emit, then some version in cs is blocked.  The
     * earliest un-emitted ancestor of that version will be ready to emit.
     * Search for it.  We could be a bit smarter by seraching harder for the
     * oldest possible version.  But most cycles are trivial (length 1) so it's
     * probably not worth the effort.  */
    for (version_t * csv = v->changeset->versions; csv; csv = csv->cs_sibling) {
        if (csv->ready_index != SIZE_MAX)
            continue;                   /* Not blocked.  */
        for (version_t * v = csv->parent; v; v = v->parent)
            if (v->ready_index != SIZE_MAX)
                return v;
    }
    abort();
}


static void cycle_split (database_t * db, changeset_t * cs)
{
    /* FIXME - the changeset may have an implicit merge; we should then split
     * the implicit merge also.  */
    fflush (NULL);
    fprintf (stderr, "*********** CYCLE **********\n");
    /* We split the changeset into to.  We leave all the blocked versions
     * in cs, and put the ready-to-emit into nw.  */

    /* FIXME - we should split implicit merges also.  */
    assert (cs->implicit_merge == NULL);
    changeset_t * new = database_new_changeset (db);
    new->type = ct_commit;
    new->time = cs->time;
    new->unready_count = 0;
    version_t ** cs_v = &cs->versions;
    version_t ** new_v = &new->versions;
    for (version_t * v = cs->versions; v; v = v->cs_sibling) {
        if (v->ready_index == SIZE_MAX) {
            /* Blocked; stays in cs.  */
            *cs_v = v;
            cs_v = &v->cs_sibling;
        }
        else {
            /* Ready-to-emit; goes into new.  */
            v->changeset = new;
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
        fprintf (stderr, "    %s:%s\n", v->file->rcs_path, v->version);
        
    fprintf (stderr, "Deferring:\n");

    for (const version_t * v = cs->versions; v; v = v->cs_sibling)
        fprintf (stderr, "    %s:%s\n", v->file->rcs_path, v->version);
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


changeset_t * next_changeset (database_t * db)
{
    /* A pending implicit merge is always done before anything else.  */
    if (db->pending_implicit_merge) {
        changeset_t * result = db->pending_implicit_merge;
        db->pending_implicit_merge = NULL;
        return result;
    }

    if (db->ready_versions.entries == db->ready_versions.entries_end)
        return NULL;

    if (db->ready_changesets.entries == db->ready_changesets.entries_end) {
        /* Find a cycle.  */
        const version_t * slow = heap_front (&db->ready_versions);
        const version_t * fast = slow;
        do {
            slow = preceed (slow);
            fast = preceed (preceed (fast));
        }
        while (slow != fast);

        /* And split it.  */
        cycle_split (db,
                     cycle_find (heap_front (&db->ready_versions))->changeset);

        assert (db->ready_changesets.entries
                != db->ready_changesets.entries_end);
    }

    return heap_pop (&db->ready_changesets);
}