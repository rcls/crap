#ifndef EMISSION_H
#define EMISSION_H

struct changeset;
struct database;
struct version;

/// Mark a version as ready to be emitted.
void version_release (struct database * db, struct version * version);

/// Record a changeset as ready to be emitted.
void changeset_emitted (struct database * db, struct changeset * changeset);

/// Record the new changeset versions on the corresponding branch.  Return the
/// number of files that actually changed.  This may be zero if the changeset
/// consisted entirely of dead trunk 1.1 revisions corresponding to branch
/// additions.
size_t changeset_update_branch (struct database * db,
                                struct changeset * changeset);

/// Find the next changeset to emit.
changeset_t * next_changeset (database_t * db);

#endif
