#ifndef FILE_H
#define FILE_H

#include "changeset.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

typedef struct file file_t;
typedef struct version version_t;
typedef struct file_tag file_tag_t;
typedef struct tag tag_t;

struct file {
    const char * path;
    const char * rcs_path;

    version_t * versions;
    version_t * versions_end;

    file_tag_t * file_tags;
    file_tag_t * file_tags_end;

    file_tag_t ** branches;
    file_tag_t ** branches_end;
};

version_t * file_new_version (file_t * f);

void file_new_branch (file_t *f, file_tag_t * tag);

/// Find a file version object by the version string.  The version string @c s
/// need not be cached.
version_t * file_find_version (const file_t * f, const char * s);

/// Find a branch on which version @c s of file @c lies.
file_tag_t * file_find_branch (const file_t * f, const char * s);

struct version {
    file_t * file;
    const char * version;
    bool dead;

    /// Indicate that this revision is part of a vendor branch import that
    /// should be implicitly merged to the trunk.
    bool implicit_merge;

    version_t * parent;
    version_t * children;               /// A child, or NULL.
    version_t * sibling;                /// A sibling, or NULL.

    const char * author;
    const char * commitid;
    time_t time;
    time_t offset;
    const char * log;
    file_tag_t * branch;

    struct changeset * commit;
    version_t * cs_sibling;             ///< Sibling in changeset.

    size_t ready_index;                 ///< Heap index for emitting versions.
};


struct file_tag {
    file_t * file;
    tag_t * tag;
    /// vers is the version information stored in cvs.  For a branch, version is
    /// the version to use as the branch point.  Version may be null.
    const char * vers;
    version_t * version;
    bool is_branch;
};


struct tag {
    const char * tag;                   ///< The tag name.

    file_tag_t ** tag_files;
    file_tag_t ** tag_files_end;

    /// This is non-NULL for branches, where a tag is considered a branch if the
    /// tag is a branch tag on any file.  It points to an array of versions, the
    /// same size as the database file array.  Each item in the slot is current
    /// version, in the emission of the branch, of the corresponding file.
    version_t ** branch_versions;

    /// The array of parent branches to this tag.  The emission process will
    /// choose one of these as the branch to put the tag on.
    struct parent_branch * parents;
    struct parent_branch * parents_end;

    /// Tags on this branch (if it's a branch).
    struct branch_tag * tags;
    struct branch_tag * tags_end;

    /// Have we been released for emission?  A tag may be released for one
    /// of two reasons; either all it's parents have been released, or we had
    /// an exact match in the tag hash.
    bool is_released;

    /// Have we had an exact match from the tag hash?
    bool exact_match;

    changeset_t changeset;              ///< Tag emission changeset.

    tag_t * hash_next;                  ///< Next in tag hash table.

    /// A sha-1 hash of the version information; this is used to identify when
    /// a set of versions exactly matching this tag has been emitted.
    uint32_t hash[5];
};


/// Initialise a @c tag with @c name.
void tag_init (tag_t * tag, const char * name);

/// Add a new @c file_tag to a @c tag.
void tag_new_tag_file (tag_t * tag, file_tag_t * file_tag);

/// Find a @c file_tag for the given @c file and @c tag.
file_tag_t * find_file_tag (file_t * file, tag_t * tag);

static inline tag_t * as_tag (const changeset_t * cs)
{
    assert (cs->type == ct_tag);
    return (tag_t *) (((char *) cs) - offsetof (tag_t, changeset));
}

#endif
