#ifndef DATABASE_H
#define DATABASE_H

#include "heap.h"

#include <sys/types.h>

struct version;

typedef struct database {
    size_t num_files;
    struct file * files;

    size_t num_tags;
    struct tag * tags;

    size_t num_changesets;
    size_t max_changesets;
    struct changeset * changesets;

    heap_t ready_versions;
    heap_t ready_changesets;

} database_t;

void database_init (database_t * db);
void database_destroy (database_t * db);

struct file * database_new_file (database_t * db);
struct changeset * database_new_changeset (database_t * db);

#endif
