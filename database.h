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

/** Initialise a database_t object.  */
void database_init (database_t * db);

/** Free memory owned by a database_t object.  */
void database_destroy (database_t * db);

/** Create a new file object for the database.  */
struct file * database_new_file (database_t * db);

/** Create a new changeset object for the database.  */
struct changeset * database_new_changeset (database_t * db);

/** Mark a version as ready to be emitted.  */
void version_release (database_t * db, struct version * version);

/** Record a changeset as ready to be emitted.  */
void changeset_emitted (database_t * db, struct changeset * changeset);


#endif
