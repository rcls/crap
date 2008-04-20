#include "changeset.h"
#include "database.h"
#include "file.h"
#include "xmalloc.h"


void file_database_init (file_database_t * db)
{
    db->num_files = 0;
    db->files = NULL;
    db->num_tags = 0;
    db->tags = NULL;
    db->num_changesets = 0;
    db->changesets = NULL;
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
