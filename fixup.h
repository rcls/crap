#ifndef FIXUP_H
#define FIXUP_H

#include <time.h>

struct database;
struct tag;
struct version;

/// Record the data for a file-version in a fixup-commit.
typedef struct fixup_ver {
    const file_t * file;                ///< NULL if already done.
    struct version * version;           ///< Maybe NULL.
    time_t time;                        ///< Timestamp of fix-up.
} fixup_ver_t;

void create_fixups(const struct database * db,
                   struct version * const * branch_versions,
                   struct tag * tag);

char * fixup_commit_comment (const struct database * db,
                             struct version * const * base_versions,
                             struct tag * tag,
                             fixup_ver_t * fixups,
                             fixup_ver_t * fixups_end);

#endif
