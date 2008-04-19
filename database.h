#ifndef DATABASE_H
#define DATABASE_H

#include "file.h"

#include <sys/types.h>

typedef struct file_database {
    size_t num_files;
    file_t * files;

    size_t num_tags;
    tag_t * tags;
} file_database_t;

file_t * file_database_new_file (file_database_t * db);
tag_t * file_database_new_tag (file_database_t * db);

#endif
