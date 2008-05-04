#include "changeset.h"
#include "database.h"
#include "file.h"
#include "string_cache.h"
#include "utils.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// FIXME - should be configurable.
#define FUZZ_TIME 3600


void changeset_init (changeset_t * cs)
{
    cs->ready_index = SIZE_MAX;
    cs->unready_count = 0;
    cs->parent = NULL;
    cs->children = NULL;
    cs->children_end = NULL;
}


static bool strings_match (const version_t * A, const version_t * B)
{
    tag_t * Abranch = A->branch ? A->branch->tag : NULL;
    tag_t * Bbranch = B->branch ? B->branch->tag : NULL;
    return A->author   == B->author
        && A->commitid == B->commitid
        && Abranch     == Bbranch
        && A->log      == B->log;
}


static int version_compare (const version_t * A, version_t * B)
{
    int r = cache_strcmp (A->commitid, B->commitid);
    if (r != 0)
        return r;

    r = cache_strcmp (A->author, B->author);
    if (r != 0)
        return r;

    if (A->branch != NULL && B->branch == NULL)
        return 1;

    if (A->branch == NULL && B->branch != NULL)
        return -1;

    if (A->branch != NULL) {
        r = cache_strcmp (A->branch->tag->tag, B->branch->tag->tag);
        if (r != 0)
            return r;
    }

    unsigned long Alh = string_hash_get (A->log);
    unsigned long Blh = string_hash_get (B->log);
    if (Alh != Blh)
        return Alh < Blh ? -1 : 1;

    r = cache_strcmp (A->log, B->log);
    if (r != 0)
        return r;

    if (A->time != B->time)
        return A->time < B->time ? -1 : 1;

    if (A->file != B->file)
        return A->file < B->file ? -1 : 1; // Files are sorted by now.

    if (A == B)
        return 0;

    return A < B ? -1 : 1;              // Versions are sorted by now.
}


static int version_compare_qsort (const void * AA, const void * BB)
{
    return version_compare (* (version_t * const *) AA,
                            * (version_t * const *) BB);
}


static int cs_compare (const void * AA, const void * BB)
{
    const changeset_t * A = * (changeset_t * const *) AA;
    const changeset_t * B = * (changeset_t * const *) BB;

    if (A->time != B->time)
        return A->time < B->time ? -1 : 1;

    if (A->type != B->type)
        return A->type < B->type ? -1 : 1;

    if (A->type == ct_commit)
        return version_compare (A->versions, B->versions);

    if (A->type == ct_implicit_merge)
        return version_compare (A->parent->versions, B->parent->versions);

    abort();
}


static void create_implicit_merge (database_t * db, changeset_t * cs)
{
    changeset_t * merge = database_new_changeset (db);
    merge->type = ct_implicit_merge;
    merge->time = cs->time;
    changeset_add_child (cs, merge);
}


void changeset_add_child (changeset_t * parent, changeset_t * child)
{
    assert (child);
    assert (child->parent == NULL);
    child->parent = parent;
    ARRAY_APPEND (parent->children, child);
}


void create_changesets (database_t * db)
{
    size_t total_versions = 0;

    for (file_t * i = db->files; i != db->files_end; ++i)
        total_versions += i->versions_end - i->versions;

    if (total_versions == 0)
        return;

    version_t ** version_list = ARRAY_ALLOC (version_t *, total_versions);
    version_t ** vp = version_list;

    for (file_t * i = db->files; i != db->files_end; ++i)
        for (version_t * j = i->versions; j != i->versions_end; ++j)
            *vp++ = j;

    assert (vp == version_list + total_versions);

    qsort (version_list, total_versions, sizeof (version_t *),
           version_compare_qsort);

    changeset_t * current = database_new_changeset (db);
    version_t * tail = version_list[0];
    tail->commit = current;
    current->time = tail->time;
    current->type = ct_commit;
    current->versions = tail;
    for (size_t i = 1; i < total_versions; ++i) {
        version_t * next = version_list[i];
        if (strings_match (tail, next)
            && next->time - current->time < FUZZ_TIME) {
            tail->cs_sibling = next;
        }
        else {
            tail->cs_sibling = NULL;
            current = database_new_changeset (db);
            current->time = next->time;
            current->type = ct_commit;
            current->versions = next;
        }
        next->commit = current;
        tail = next;
    }

    tail->cs_sibling = NULL;

    free (version_list);

    // Now walk through the commit changesets and process the implicit merges.
    // We create an implicit_merge changeset for each one that needs it.
    size_t num_commits = db->changesets_end - db->changesets;
    for (size_t i = 0; i != num_commits; ++i)
        for (version_t * v = db->changesets[i]->versions; v; v = v->cs_sibling)
            if (v->implicit_merge) {
                create_implicit_merge (db, db->changesets[i]);
                break;
            }

    qsort (db->changesets, db->changesets_end - db->changesets,
           sizeof (changeset_t *), cs_compare);
}
