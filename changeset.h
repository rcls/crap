#ifndef CHANGESET_H
#define CHANGESET_H

#include <time.h>

struct database;

typedef struct changeset {
    struct version * versions;          /** List of versions. */
    /**
     * Number of reasons for not emitting this changeset.  Each version in
     * the changeset with an unemitted predecessor counts.  Also counted is
     * each version which is the first change to a file on a branch, where the
     * branch start point has not been detected.  */
    size_t unready_count;
    size_t ready_index;                 /** Index into emission heap.  */
} changeset_t;

void create_changesets (struct database * db);

#endif
