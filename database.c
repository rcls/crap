#include "changeset.h"
#include "database.h"
#include "file.h"
#include "utils.h"

#include <stdlib.h>

void file_database_init (file_database_t * db)
{
    db->num_files = 0;
    db->files = NULL;
    db->num_tags = 0;
    db->tags = NULL;
    db->num_changesets = 0;
    db->changesets = NULL;
}


void file_database_destroy (file_database_t * db)
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


file_t * file_database_new_file (file_database_t * db)
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


void file_database_new_changeset (file_database_t * db, version_t * v)
{
    db->changesets = xrealloc (db->changesets,
                               ++db->num_changesets * sizeof (changeset_t));
    db->changesets[db->num_changesets - 1] = v;
}
