#include "database.h"
#include "file.h"
#include "xmalloc.h"

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
