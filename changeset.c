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

    int r = cache_strcmp (A->author, B->author);
    if (r != 0)
        return r;

    r = cache_strcmp (A->commitid, B->commitid);
    if (r != 0)
        return r;

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

    for (size_t i = 0; i != db->num_files; ++i)
        total_versions += db->files[i].num_versions;

    if (total_versions == 0)
        return;

    version_t ** version_list = xmalloc (total_versions * sizeof (version_t *));
    version_t ** vp = version_list;

    for (size_t i = 0; i != db->num_files; ++i)
        for (size_t j = 0; j != db->files[i].num_versions; ++j)
            *vp++ = &db->files[i].versions[j];

    assert (vp == version_list + total_versions);

    qsort (version_list, total_versions, sizeof (version_t *), version_compare);

    changeset_t * current = database_new_changeset (db);
    version_t * tail = version_list[0];
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
        tail = next;
    }

    tail->cs_sibling = NULL;

    free (version_list);

    qsort (db->changesets, db->num_changesets,
           sizeof (changeset_t *), cs_compare);

    // FIXME - this is still not right; once we do change-set splitting, we'll
    // have to be a bit more careful about changeset pointers.
    for (size_t i = 0; i != db->num_changesets; ++i) {
        size_t count = 0;
        for (version_t * v = db->changesets[i]->versions;
             v; v = v->cs_sibling) {
            ++count;
            v->changeset = db->changesets[i];
        }
        assert (count == db->changesets[i]->unready_versions);
    }
}
