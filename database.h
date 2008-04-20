#ifndef DATABASE_H
#define DATABASE_H

#include <sys/types.h>

typedef struct file_database {
    size_t num_files;
    struct file * files;

    size_t num_tags;
    struct tag * tags;

    size_t num_changesets;
    struct version ** changesets;

} file_database_t;

void file_database_init (file_database_t * db);
void file_database_destroy (file_database_t * db);

struct file * file_database_new_file (file_database_t * db);
void file_database_new_changeset (file_database_t * db, struct version * v);

#endif
