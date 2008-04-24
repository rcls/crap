#ifndef DATABASE_H
#define DATABASE_H

#include "heap.h"

struct version;

typedef struct database {
    struct file * files;
    struct file * files_end;
    struct file * files_max;

    struct tag * tags;
    struct tag * tags_end;

    struct changeset ** changesets;
    struct changeset ** changesets_end;
    struct changeset ** changesets_max;

    heap_t ready_versions;
    heap_t ready_changesets;

    struct version ** trunk_versions;

} database_t;

/** Initialise a database_t object.  */
void database_init (database_t * db);

/** Free memory owned by a database_t object.  */
void database_destroy (database_t * db);

/** Create a new file object for the database.  */
struct file * database_new_file (database_t * db);

/** Create a new changeset object for the database.  */
struct changeset * database_new_changeset (database_t * db);

#endif
