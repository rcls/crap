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
    db->num_files = 0;
    db->files = NULL;
    db->num_tags = 0;
    db->tags = NULL;
    db->num_changesets = 0;
    db->max_changesets = 0;
    db->changesets = NULL;

    heap_init (&db->ready_versions,
               offsetof (version_t, ready_index), compare_version);
    heap_init (&db->ready_changesets,
               offsetof (changeset_t, ready_index), compare_changset);
}


void database_destroy (database_t * db)
{
    for (size_t i = 0; i != db->num_files; ++i) {
        free (db->files[i].versions);
        free (db->files[i].file_tags);
        free (db->files[i].branches);
    }

    for (size_t i = 0; i != db->num_tags; ++i)
        free (db->tags[i].tag_files);

    for (size_t i = 0; i != db->num_changesets; ++i)
        free (db->changesets[i]);

    free (db->files);
    free (db->tags);
    free (db->changesets);
    free (db->ready_versions.entries);
    free (db->ready_changesets.entries);
}


file_t * database_new_file (database_t * db)
{
    db->files = xrealloc (db->files, ++db->num_files * sizeof (file_t));
    file_t * result = &db->files[db->num_files - 1];
    result->num_versions = 0;
    result->max_versions = 0;
    result->versions = NULL;
    result->num_file_tags = 0;
    result->max_file_tags = 0;
    result->file_tags = NULL;
    result->branches_end = 0;
    result->branches_max = 0;
    result->branches = NULL;
    return result;
}


changeset_t * database_new_changeset (database_t * db)
{
    changeset_t * result = xmalloc (sizeof (changeset_t));
    result->ready_index = SIZE_MAX;

    ARRAY_EXTEND (db->changesets, db->num_changesets, db->max_changesets);

    db->changesets[db->num_changesets - 1] = result;
    return result;
}


void version_release (database_t * db, version_t * version)
{
    heap_insert (&db->ready_versions, version);

    assert (version->changeset->unready_versions != 0);

    if (--version->changeset->unready_versions == 0)
        heap_insert (&db->ready_changesets, version->changeset);
}


void changeset_emitted (database_t * db, changeset_t * changeset)
{
    for (version_t * cs_v = changeset->versions;
         cs_v; cs_v = cs_v->cs_sibling) {
        heap_remove (&db->ready_versions, cs_v);
        for (version_t * v = cs_v->children; v; v = v->sibling)
            version_release (db, v);
    }
}
