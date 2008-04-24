#include "changeset.h"
#include "database.h"
#include "file.h"
#include "utils.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int compare_version (const void * AA, const void * BB)
{
    const version_t * A = AA;
    const version_t * B = BB;
    if (A->time != B->time)
        return A->time > B->time;

    if (A->file != B->file)
        return A->file > B->file;

    return strcmp (A->version, B->version);
}


static int compare_changset (const void * AA, const void * BB)
{
    const version_t * A = ((const changeset_t *) AA)->versions;
    const version_t * B = ((const changeset_t *) BB)->versions;

    if (A->time != B->time)
        return A->time > B->time;

    if (A->author != B->author)
        return strcmp (A->author, B->author);

    if (A->commitid != B->commitid)
        return strcmp (A->commitid, B->commitid);

    if (A->log != B->log)
        return strcmp (A->log, B->log);

    // FIXME - should have branch names in here.

    // Last resort.
    if (A->file != B->file)
        return A->file > B->file;

    return A > B;
}


void database_init (database_t * db)
{
    db->files = NULL;
    db->files_end = NULL;
    db->files_max = NULL;
    db->tags = NULL;
    db->tags_end = NULL;
    db->changesets = NULL;
    db->changesets_end = NULL;
    db->changesets_max = NULL;

    heap_init (&db->ready_versions,
               offsetof (version_t, ready_index), compare_version);
    heap_init (&db->ready_changesets,
               offsetof (changeset_t, ready_index), compare_changset);
}


void database_destroy (database_t * db)
{
    for (file_t * i = db->files; i != db->files_end; ++i) {
        free (i->versions);
        free (i->file_tags);
        free (i->branches);
    }

    for (tag_t * i = db->tags; i != db->tags_end; ++i) {
        free (i->tag_files);
        free (i->branch_versions);
    }

    for (changeset_t ** i = db->changesets; i != db->changesets_end; ++i)
        free (*i);

    free (db->files);
    free (db->tags);
    free (db->changesets);
    free (db->ready_versions.entries);
    free (db->ready_changesets.entries);
    free (db->trunk_versions);
}


file_t * database_new_file (database_t * db)
{
    ARRAY_EXTEND (db->files, db->files_end, db->files_max);
    file_t * result = &db->files_end[-1];
    result->versions = NULL;
    result->versions_end = NULL;
    result->versions_max = NULL;
    result->file_tags = NULL;
    result->file_tags_end = NULL;
    result->file_tags_max = NULL;
    result->branches = NULL;
    result->branches_end = NULL;
    result->branches_max = NULL;
    return result;
}


changeset_t * database_new_changeset (database_t * db)
{
    changeset_t * result = xmalloc (sizeof (changeset_t));
    result->ready_index = SIZE_MAX;

    ARRAY_EXTEND (db->changesets, db->changesets_end, db->changesets_max);

    db->changesets_end[-1] = result;
    return result;
}
