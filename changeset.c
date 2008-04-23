#include "changeset.h"
#include "database.h"
#include "file.h"
#include "string_cache.h"
#include "utils.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* FIXME - should be configurable.  */
#define FUZZ_TIME 300


static bool strings_match (const version_t * A, const version_t * B)
{
    if (cache_strcmp (A->author, B->author) != 0)
        return 0;

    if (cache_strcmp (A->commitid, B->commitid) != 0)
        return 0;

    if (string_hash_get (A->log) != string_hash_get (B->log))
        return 0;

    if (cache_strcmp (A->log, B->log) != 0)
        return 0;

    return 1;
}


static int version_compare (const void * AA, const void * BB)
{
    const version_t * A = * (version_t * const *) AA;
    const version_t * B = * (version_t * const *) BB;

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
        return A->file < B->file ? -1 : 1; /* Files are sorted by now.  */
    if (A == B)
        return 0;
    return A < B ? -1 : 1;              /* Versions are sorted by now.  */
}


static int cs_compare (const void * AA, const void * BB)
{
    const version_t * A = (* (changeset_t * const *) AA)->versions;
    const version_t * B = (* (changeset_t * const *) BB)->versions;

    if (A->time != B->time)
        return A->time < B->time ? -1 : 1;
    else
        return version_compare (&A, &B);
}


void create_changesets (database_t * db)
{
    size_t total_versions = 0;

    for (file_t * i = db->files; i != db->files_end; ++i)
        total_versions += i->versions_end - i->versions;

    if (total_versions == 0)
        return;

    version_t ** version_list = xmalloc (total_versions * sizeof (version_t *));
    version_t ** vp = version_list;

    for (file_t * i = db->files; i != db->files_end; ++i)
        for (version_t * j = i->versions; j != i->versions_end; ++j)
            *vp++ = j;

    assert (vp == version_list + total_versions);

    qsort (version_list, total_versions, sizeof (version_t *), version_compare);

    changeset_t * current = database_new_changeset (db);
    version_t * tail = version_list[0];
    tail->changeset = current;
    current->versions = tail;
    current->unready_versions = 1;
    for (size_t i = 1; i < total_versions; ++i) {
        version_t * next = version_list[i];
        if (strings_match (tail, next)
            && next->time - current->versions->time < FUZZ_TIME) {
            tail->cs_sibling = next;
            ++current->unready_versions;
        }
        else {
            tail->cs_sibling = NULL;
            current = database_new_changeset (db);
            current->versions = next;
            current->unready_versions = 1;
        }
        next->changeset = current;
        tail = next;
    }

    tail->cs_sibling = NULL;

    free (version_list);

    qsort (db->changesets, db->changesets_end - db->changesets,
           sizeof (changeset_t *), cs_compare);
}
