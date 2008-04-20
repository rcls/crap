#ifndef CHANGESET_H
#define CHANGESET_H

#include <time.h>

struct database;

typedef struct changeset {
    struct version * versions;          /** List of versions. */
    size_t unready_versions;            /** Number of blocking versions.  */
    size_t ready_index;                 /** Index into emission heap.  */
} changeset_t;

void create_changesets (struct database * db);

#endif
