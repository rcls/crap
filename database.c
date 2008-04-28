#include "changeset.h"
#include "database.h"
#include "file.h"
#include "utils.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int compare_version (const void * AA, const void * BB)
{
    const version_t * A = AA;
    const version_t * B = BB;
    if (A->time != B->time)
        return A->time > B->time;

    if (A->file != B->file)
        return A->file > B->file;

    return strcmp (A->version, B->version);
}


static int compare_changeset (const void * AA, const void * BB)
{
    const changeset_t * A = AA;
    const changeset_t * B = BB;

    // Implicit merges go before anything else, no matter what the timestamps.
    if (A->type == ct_implicit_merge && B->type != ct_implicit_merge)
        return -1;

    if (A->type != ct_implicit_merge && B->type == ct_implicit_merge)
        return 1;

    if (A->time != B->time)
        return A->time > B->time;

    // That's all the ordering we really *need* to do, but we try and make
    // things as deterministic as possible.

    if (A->type != B->type)
        return A->type > B->type ? 1 : -1;

    if (A->type == ct_implicit_merge) {
        A = A->parent;
        B = B->parent;
    }

    if (A->versions->author != B->versions->author)
        return strcmp (A->versions->author, B->versions->author);

    if (A->versions->commitid != B->versions->commitid)
        return strcmp (A->versions->commitid, B->versions->commitid);

    if (A->versions->log != B->versions->log)
        return strcmp (A->versions->log, B->versions->log);

    if (A->versions->branch == NULL && B->versions->branch != NULL)
        return -1;

    if (A->versions->branch != NULL && B->versions->branch == NULL)
        return 1;

    if (A->versions->branch->tag != B->versions->branch->tag)
        return A->versions->branch->tag < B->versions->branch->tag ? -1 : 1;

    if (A->versions->file != B->versions->file)
        return A->versions->file > B->versions->file;

    return A->versions > B->versions;
}


void database_init (database_t * db)
{
    db->files = NULL;
    db->files_end = NULL;
    db->tags = NULL;
    db->tags_end = NULL;
    db->changesets = NULL;
    db->changesets_end = NULL;
    db->tag_hash = NULL;
    db->tag_hash_num_entries = 0;
    db->tag_hash_num_buckets = 0;

    heap_init (&db->ready_versions,
               offsetof (version_t, ready_index), compare_version);
    heap_init (&db->ready_changesets,
               offsetof (changeset_t, ready_index), compare_changeset);
}


void database_destroy (database_t * db)
{
    for (file_t * i = db->files; i != db->files_end; ++i) {
        free (i->versions);
        free (i->file_tags);
        free (i->branches);
    }

    for (tag_t * i = db->tags; i != db->tags_end; ++i) {
        free (i->tag_files);
        free (i->branch_versions);
        free (i->tags);
        free (i->parents);
    }

    for (changeset_t ** i = db->changesets; i != db->changesets_end; ++i)
        free (*i);

    free (db->files);
    free (db->tags);
    free (db->changesets);
    heap_destroy (&db->ready_versions);
    heap_destroy (&db->ready_changesets);
    free (db->tag_hash);
}


file_t * database_new_file (database_t * db)
{
    ARRAY_EXTEND (db->files, db->files_end);
    file_t * result = &db->files_end[-1];
    result->versions = NULL;
    result->versions_end = NULL;
    result->file_tags = NULL;
    result->file_tags_end = NULL;
    result->branches = NULL;
    result->branches_end = NULL;
    return result;
}


changeset_t * database_new_changeset (database_t * db)
{
    changeset_t * result = xmalloc (sizeof (changeset_t));
    changeset_init (result);

    ARRAY_EXTEND (db->changesets, db->changesets_end);

    db->changesets_end[-1] = result;
    return result;
}


void database_tag_hash_insert (database_t * db, tag_t * tag)
{
    if (db->tag_hash_num_buckets == 0) {
        db->tag_hash_num_buckets = 8;
        db->tag_hash = ARRAY_CALLOC (tag_t *, 8);
    }
    else if (db->tag_hash_num_entries >= db->tag_hash_num_buckets) {
        db->tag_hash = ARRAY_REALLOC (db->tag_hash,
                                      2 * db->tag_hash_num_buckets);

        for (size_t i = 0; i != db->tag_hash_num_buckets; ++i) {
            tag_t ** old = db->tag_hash + i;
            tag_t ** new = old + db->tag_hash_num_buckets;
            for (tag_t * v = db->tag_hash[i]; v != 0; v = v->hash_next)
                if (v->hash[0] & db->tag_hash_num_buckets) {
                    *new = v;
                    new = &v->hash_next;
                }
                else {
                    *old = v;
                    old = &v->hash_next;
                }
            *old = NULL;
            *new = NULL;
        }
        db->tag_hash_num_buckets *= 2;
    }

    tag_t ** bucket = &db->tag_hash[tag->hash[0]
                                    & (db->tag_hash_num_buckets - 1)];
    tag->hash_next = *bucket;
    *bucket = tag;

    ++db->tag_hash_num_entries;
}


tag_t * database_tag_hash_find (database_t * db, const uint32_t hash[5])
{
    for (tag_t * i = db->tag_hash[hash[0] & (db->tag_hash_num_buckets - 1)];
         i; i = i->hash_next)
        if (memcmp (hash, i->hash, sizeof (i->hash)) == 0)
            return i;
    return NULL;
}


tag_t * database_tag_hash_next (tag_t * tag)
{
    for (tag_t * i = tag->hash_next; i; i = i->hash_next)
        if (memcmp (tag->hash, i->hash, sizeof (i->hash)) == 0)
            return i;
    return NULL;
}
