#ifndef CHANGESET_H
#define CHANGESET_H

#include <assert.h>
#include <time.h>

struct database;

typedef struct implicit_merge implicit_merge_t;
typedef struct changeset changeset_t;
typedef struct commit commit_t;

/// The possible types of changeset.
typedef enum changeset_type {
    ct_implicit_merge,        ///< Implicit merge from vendor branch to trunk.
    ct_commit,                ///< A normal commit.
} changeset_type_t;


/// Base information about a changeset.
struct changeset {
    time_t time;
    changeset_type_t type;
    size_t unready_count;
    /// Number of reasons for not emitting this changeset. Each version in
    /// the changeset with an unemitted predecessor counts.  Also counted is
    /// each version which is the first change to a file on a branch, where the
    /// branch start point has not been detected.
    size_t ready_index;                 ///< Index into emission heap.

    /// Child changeset list.  Children cannot be emitted until the parent is.
    /// Possible reasons for being a child:
    ///  - implicit merge of a vendor branch import.
    ///  - commit on a branch.  (NYI).
    ///  - branch off a branch.  (NYI).
    changeset_t * children;
    changeset_t * sibling;              ///< Sibling in parent/child relation.
};


/// A changeset representing a commit.
struct commit {
    changeset_t changeset;

    struct version * versions;          ///< List of versions.
};


/// A changeset representing an implicit merge.  Note that we don't maintain
/// strict dependency information on implicit merges; a changeset may depend
/// on the implict merge despite not having the dependency explicitly
/// maintained.  Instead, we simply prioritise the emission of the implicit
/// merge.
struct implicit_merge {
    changeset_t changeset;
    commit_t * commit;
};

void create_changesets (struct database * db);


/// Type safe conversion from changeset to commit.
static inline commit_t * as_commit (const changeset_t * cs)
{
    assert (cs->type == ct_commit);
    return (commit_t *) cs;
}


/// Type safe conversion from changeset to implicit merge.
static inline implicit_merge_t * as_imerge (const changeset_t * cs)
{
    assert (cs->type == ct_implicit_merge);
    return (implicit_merge_t *) cs;
}


/// Give @c parent a @c child.
void changeset_add_child (changeset_t * parent, changeset_t * child);


#endif
