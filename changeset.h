#ifndef CHANGESET_H
#define CHANGESET_H

#include <assert.h>
#include <time.h>

struct database;

typedef struct changeset changeset_t;

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

    struct version * versions;          ///< List of versions for a commit.

    /// Child changeset list.  Children cannot be emitted until the parent is.
    /// Possible reasons for being a child:
    ///  - implicit merge of a vendor branch import.
    ///  - commit on a branch.  (NYI).
    ///  - branch off a branch.  (NYI).
    changeset_t * children;
    changeset_t * sibling;              ///< Sibling in parent/child relation.
    changeset_t * parent;               ///< Parent in parent/child relation.
};


/// Give @c parent a @c child.
void changeset_add_child (changeset_t * parent, changeset_t * child);


#endif
