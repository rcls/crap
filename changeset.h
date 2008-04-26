#ifndef CHANGESET_H
#define CHANGESET_H

#include <time.h>

struct database;

typedef enum changeset_type {
    ct_implicit_merge,        /* Implicit merge from vendor branch to trunk.  */
    ct_commit,                          /* A normal commit.  */
} changeset_type_t;


typedef struct changeset {
    time_t time;
    changeset_type_t type;
    size_t unready_count;
    /**
     * Number of reasons for not emitting this changeset.  Each version in
     * the changeset with an unemitted predecessor counts.  Also counted is
     * each version which is the first change to a file on a branch, where the
     * branch start point has not been detected.  */
    size_t ready_index;                 /** Index into emission heap.  */

    struct version * versions;          /** List of versions. */

    /**
     * If this commit is a vendor branch import, then we may need to follow the
     * commit with a merge to trunk.  */
    struct changeset * implicit_merge;
} changeset_t;


void create_changesets (struct database * db);

#endif
