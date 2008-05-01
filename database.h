#ifndef DATABASE_H
#define DATABASE_H

#include "heap.h"

#include <stdint.h>

struct version;

typedef struct database {
    struct file * files;
    struct file * files_end;

    struct tag * tags;
    struct tag * tags_end;

    struct changeset ** changesets;
    struct changeset ** changesets_end;

    heap_t ready_versions;
    heap_t ready_changesets;
    heap_t ready_tags;

    struct tag ** tag_hash;
    size_t tag_hash_num_entries;
    size_t tag_hash_num_buckets;
} database_t;

/// Initialise a database_t object.
void database_init (database_t * db);

/// Free memory owned by a database_t object.
void database_destroy (database_t * db);

/// Create a new file object for the database.
struct file * database_new_file (database_t * db);

/// Create a new changeset object for the database.
struct changeset * database_new_changeset (database_t * db);

/// Insert a tag into the tag hash.
void database_tag_hash_insert (database_t * db, struct tag * tag);

/// Find the first tag matching a hash.
struct tag * database_tag_hash_find (database_t * db, const uint32_t hash[5]);

/// Find the next tag matching a hash.
struct tag * database_tag_hash_next (struct tag * tag);

#endif
