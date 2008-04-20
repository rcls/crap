#include "changeset.h"
#include "database.h"
#include "file.h"
#include "utils.h"

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
    const version_t * A = AA;
    const version_t * B = BB;

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
    db->changesets = NULL;

    heap_init (&db->ready_versions,
               offsetof (version_t, v_ready_index), compare_version);
    heap_init (&db->ready_changesets,
               offsetof (version_t, cs_ready_index), compare_changset);
}


void database_destroy (database_t * db)
{
    for (size_t i = 0; i != db->num_files; ++i) {
        free (db->files[i].versions);
        free (db->files[i].file_tags);
    }

    for (size_t i = 0; i != db->num_tags; ++i)
        free (db->tags[i].tag_files);

    free (db->files);
    free (db->tags);
    free (db->changesets);
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
    result->file_tags = 0;
    return result;
}


void database_new_changeset (database_t * db, version_t * v)
{
    db->changesets = xrealloc (db->changesets,
                               ++db->num_changesets * sizeof (version_t *));
    db->changesets[db->num_changesets - 1] = v;
}
