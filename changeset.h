#ifndef CHANGESET_H
#define CHANGESET_H

#include <assert.h>
#include <time.h>

struct database;

typedef struct implicit_merge implicit_merge_t;
typedef struct changeset changeset_t;
typedef struct commit commit_t;

/**
 * The possible types of changeset.  */
typedef enum changeset_type {
    ct_implicit_merge,        /* Implicit merge from vendor branch to trunk.  */
    ct_commit,                          /* A normal commit.  */
} changeset_type_t;


/**
 * Base information about a changeset.  */
struct changeset {
    time_t time;
    changeset_type_t type;
    size_t unready_count;
    /**
     * Number of reasons for not emitting this changeset.  Each version in
     * the changeset with an unemitted predecessor counts.  Also counted is
     * each version which is the first change to a file on a branch, where the
     * branch start point has not been detected.  */
    size_t ready_index;                 /** Index into emission heap.  */
};


/**
 * A changeset representing a commit.
 */
struct commit {
    changeset_t changeset;

    struct version * versions;          /** List of versions. */

    /**
     * If this commit is a vendor branch import, then we may need to follow the
     * commit with a merge to trunk.  */
    implicit_merge_t * implicit_merge;

    /**
     * We keep a list of all commits on a branch; a commit is not released until
     * the branch tag is emitted.
     */
//    commit_t * branch_sibling;
};


/**
 * A changeset representing an implicit merge.
 */
struct implicit_merge {
    changeset_t changeset;
    commit_t * commit;
};

void create_changesets (struct database * db);


// Type safe conversion from changeset to commit.
static inline commit_t * as_commit (const changeset_t * cs)
{
    assert (cs->type == ct_commit);
    return (commit_t *) cs;
}


// Type safe conversion from changeset to implicit merge.
static inline implicit_merge_t * as_imerge (const changeset_t * cs)
{
    assert (cs->type == ct_implicit_merge);
    return (implicit_merge_t *) cs;
}


#endif
