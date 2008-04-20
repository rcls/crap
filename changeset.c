#include "changeset.h"
#include "database.h"
#include "file.h"
#include "string_cache.h"
#include "xmalloc.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* FIXME - should be configurable.  */
#define FUZZ_TIME 300


static bool strings_match (const version_t * A, const version_t * B)
{
    if (strcmp (A->author, B->author) != 0)
        return 0;

    if (strcmp (A->commitid, B->commitid) != 0)
        return 0;

    if (string_hash_get (A->log) != string_hash_get (B->log))
        return 0;

    if (strcmp (A->log, B->log) != 0)
        return 0;

    return 1;
}


static int version_compare (const void * AA, const void * BB)
{
    const version_t * A = * (version_t * const *) AA;
    const version_t * B = * (version_t * const *) BB;

    int r = strcmp (A->author, B->author);
    if (r != 0)
        return r;

    r = strcmp (A->commitid, B->commitid);
    if (r != 0)
        return r;

    unsigned long Alh = string_hash_get (A->log);
    unsigned long Blh = string_hash_get (B->log);
    if (Alh != Blh)
        return Alh < Blh ? -1 : 1;

    r = strcmp (A->log, B->log);
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
    const version_t * A = * (version_t * const *) AA;
    const version_t * B = * (version_t * const *) BB;

    if (A->time != B->time)
        return A->time < B->time ? -1 : 1;
    else
        return version_compare (AA, BB);
}        


void create_changesets (file_database_t * db)
{
    size_t total_versions = 0;

    for (size_t i = 0; i != db->num_files; ++i)
        total_versions += db->files[i].num_versions;

    if (total_versions == 0) {
        db->num_changesets = 0;
        db->changesets = NULL;
        return;
    }

    version_t ** version_list = xmalloc (total_versions * sizeof (version_t *));
    version_t ** vp = version_list;

    for (size_t i = 0; i != db->num_files; ++i)
        for (size_t j = 0; j != db->files[i].num_versions; ++j)
            *vp++ = &db->files[i].versions[j];

    assert (vp == version_list + total_versions);

    qsort (version_list, total_versions, sizeof (version_t *), version_compare);

    version_t * current = version_list[0];
    time_t time = current->time;
    file_database_new_changeset (db, current);
    for (size_t i = 1; i < total_versions; ++i) {
        version_t * next = version_list[i];
        if (strings_match (current, next) && next->time - time < FUZZ_TIME)
            current->cs_sibling = next;
        else {
            current->cs_sibling = NULL;
            file_database_new_changeset (db, next);
            time = next->time;
        }
        current = next;
    }

    current->cs_sibling = NULL;

    free (version_list);

    qsort (db->changesets, db->num_changesets,
           sizeof (version_t *), cs_compare);
}
