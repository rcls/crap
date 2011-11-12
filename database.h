#ifndef DATABASE_H
#define DATABASE_H

#include "heap.h"

#include <stdint.h>

typedef struct database {
    struct file * files;
    struct file * files_end;

    struct tag * tags;
    struct tag * tags_end;

    struct changeset ** changesets;
    struct changeset ** changesets_end;

    heap_t ready_changesets;
} database_t;

/// Initialise a database_t object.
void database_init (database_t * db);

/// Free memory owned by a database_t object.
void database_destroy (database_t * db);

/// Create a new file object for the database.
struct file * database_new_file (database_t * db);

/// Find a file object by path name.
struct file * database_find_file (const database_t * db, const char * path);

/// Create a new changeset object for the database.
struct changeset * database_new_changeset (database_t * db);

/// Find a tag by name.
struct tag * database_find_tag (const database_t * db, const char * name);

#endif
