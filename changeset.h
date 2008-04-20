#ifndef CHANGESET_H
#define CHANGESET_H

#include <time.h>

struct database;

typedef struct changeset {
    size_t ready_index;                 /* Index into emission heap.  */

    struct version * versions;
} changeset_t;

void create_changesets (struct database * db);

#endif
