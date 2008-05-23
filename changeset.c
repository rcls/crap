#include "changeset.h"
#include "database.h"
#include "emission.h"
#include "file.h"
#include "string_cache.h"
#include "utils.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// FIXME - should be configurable.
#define FUZZ_TIME 300


void changeset_init (changeset_t * cs)
{
    cs->ready_index = SIZE_MAX;
    cs->unready_count = 0;
    cs->children = NULL;
    cs->children_end = NULL;
    cs->versions = NULL;
    cs->versions_end = NULL;
}


static bool strings_match (const version_t * A, const version_t * B)
{
    tag_t * Abranch = A->branch ? A->branch->tag : NULL;
    tag_t * Bbranch = B->branch ? B->branch->tag : NULL;
    return A->author   == B->author
        && A->commitid == B->commitid
        && Abranch     == Bbranch
        && A->log      == B->log
        && A->implicit_merge == B->implicit_merge;
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

    if (A->implicit_merge != B->implicit_merge)
        return B->implicit_merge - A->implicit_merge;

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


static int version_compare_heap (const void * AA, const void * BB)
{
    const version_t * A = AA;
    const version_t * B = BB;
    if (A->time != B->time)
        return A->time > B->time;

    if (A->file != B->file)
        return A->file > B->file;

    return strcmp (A->version, B->version);
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
    ARRAY_APPEND (current->versions, version_list[0]);
    version_list[0]->commit = current;
    current->time = version_list[0]->time;
    current->type = ct_commit;
    for (size_t i = 1; i < total_versions; ++i) {
        version_t * next = version_list[i];
        if (!strings_match (*current->versions, next)
            || next->time - current->time > FUZZ_TIME) {
            ARRAY_TRIM (current->versions);
            current = database_new_changeset (db);
            current->time = next->time;
            current->type = ct_commit;
        }
        ARRAY_APPEND (current->versions, version_list[i]);
        version_list[i]->commit = current;
    }

    ARRAY_TRIM (current->versions);
    free (version_list);

    // Do a pass through the changesets; this breaks any cycles.
    heap_t ready_versions;
    heap_init (&ready_versions,
               offsetof (version_t, ready_index), version_compare_heap);

    prepare_for_emission (db, &ready_versions);
    size_t emitted_changesets = 0;
    changeset_t * changeset;
    while ((changeset = next_changeset_split (db, &ready_versions))) {
        changeset_emitted (db, &ready_versions, changeset);
        ++emitted_changesets;
    }

    assert (heap_empty (&ready_versions));
    assert (heap_empty (&db->ready_changesets));
    assert (emitted_changesets == db->changesets_end - db->changesets);

    heap_destroy (&ready_versions);
}
