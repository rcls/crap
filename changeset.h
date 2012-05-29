#ifndef CHANGESET_H
#define CHANGESET_H

#include <time.h>

struct database;

typedef struct changeset changeset_t;

/// The possible types of changeset.
typedef enum changeset_type {
    ct_tag,                             ///< Tag / branch.
    ct_commit,                          ///< A normal commit.
} changeset_type_t;


/// Base information about a changeset.
struct changeset {
    time_t time;                        ///< Timestamp of changeset.
    changeset_type_t type;              ///< Type of changeset.

    /// Number of reasons for not emitting this changeset. Each version in
    /// the changeset with an unemitted predecessor counts.  Also counted is
    /// each version which is the first change to a file on a branch, where the
    /// branch start point has not been detected.
    size_t unready_count;

    size_t ready_index;                 ///< Index into emission heap.
    long mark;                          ///< Mark number for fast-import.

    struct version ** versions;         ///< List of versions for a commit.
    struct version ** versions_end;

    /// Child changeset list.  Children cannot be emitted until the parent is.
    /// Possible reasons for being a child:
    ///  - implicit merge of a vendor branch import.
    ///  - commit on a branch.
    ///  - tag/branch off a branch.
    changeset_t ** children;
    changeset_t ** children_end;

    /// Merge list.  These changesets are recorded as ancestors of the
    /// changeset.
    changeset_t ** merge;
    changeset_t ** merge_end;
};


/// Initialise changeset members.
void changeset_init (changeset_t * changeset);

/// Create the commit and vendor-merge changesets.
void create_changesets (struct database * db);

/// The maximum difference between the timestamps of any two commits in a
/// changeset.
extern int fuzz_span;

/// The maximum difference between the timestamps of two consecutive commits in
/// a changeset.
extern int fuzz_gap;

#endif
