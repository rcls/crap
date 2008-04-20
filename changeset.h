#ifndef CHANGESET_H
#define CHANGESET_H

#include <time.h>

struct database;

typedef struct changeset {
    time_t time;                        /* Earliest version in changeset.  */
    const char * log;
    const char * author;
    const char * commitid;

    size_t num_versions;
    struct version ** versions;
} changeset_t;

void create_changesets (struct database * db);

#endif
