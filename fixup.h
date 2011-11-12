#ifndef FIXUP_H
#define FIXUP_H

#include <time.h>

struct changeset;
struct database;
struct tag;
struct version;

/// Record the data for a file-version in a fixup-commit.
typedef struct fixup_ver {
    const file_t * file;                ///< NULL if already done.
    struct version * version;           ///< Maybe NULL.
    time_t time;                        ///< Timestamp of fix-up.
} fixup_ver_t;

/// Create the fixups for a tag (or branch).  @p branch_versions is a list of
/// versions, and versions that differ on the @p tag are noted in the @p
/// tag->fixup list.
void create_fixups (const struct database * db,
                    struct version * const * branch_versions,
                    struct tag * tag);

/// Select from the @p tag->fixups the list of @p fixups to be done before the
/// @p changeset (or all if NULL).
void fixup_list (fixup_ver_t ** fixups, fixup_ver_t ** fixups_end,
                 tag_t * tag, const struct changeset * changeset);

/// Generate the commit message for a fixup list.
char * fixup_commit_comment (const struct database * db,
                             struct version * const * base_versions,
                             struct tag * tag,
                             fixup_ver_t * fixups,
                             fixup_ver_t * fixups_end);

#endif
