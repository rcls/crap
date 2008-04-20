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
    return &db->files[db->num_files - 1];
}


tag_t * file_database_new_tag (file_database_t * db)
{
    db->tags = xrealloc (db->tags, ++db->num_tags * sizeof (tag_t));
    return &db->tags[db->num_tags - 1];
}


void file_database_new_changeset (file_database_t * db, version_t * v)
{
    db->changesets = xrealloc (db->changesets,
                               ++db->num_changesets * sizeof (changeset_t));
    db->changesets[db->num_changesets - 1] = v;
}
