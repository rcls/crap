#ifndef FILTER_H
#define FILTER_H

struct changeset;
struct database;

void filter_changesets (struct database * db,
                        struct changeset ** serial,
                        struct changeset ** serial_end,
                        const char * filter_command);

#endif
