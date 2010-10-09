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
typedef struct tag tag_t;
typedef struct fixup_ver fixup_ver_t;

struct file {
    const char * path;
    const char * rcs_path;

    version_t * versions;
    version_t * versions_end;
};

version_t * file_new_version (file_t * f);

/// Find a file version object by the version string.  The version string @c s
/// need not be cached.
version_t * file_find_version (const file_t * f, const char * s);

struct version {
    file_t * file;                      ///< File this is a version of.
    const char * version;               ///< Version string.
    bool dead;                          ///< A dead revision marking a delete.

    /// Indicate that this revision is the implicit merge of a vendor branch
    /// import to the trunk.
    bool implicit_merge;

    /// An implicit merge might not actually get used; this flag is set to
    /// indicate if the revision was actually used.
    bool used;

    /// Should this version be mode 755 instead of 644?
    bool exec;

    version_t * parent;                 ///< Previous version.
    version_t * children;               ///< A child, or NULL.
    version_t * sibling;                ///< A sibling, or NULL.

    const char * author;
    const char * commitid;
    time_t time;
    time_t offset;
    const char * log;
    tag_t * branch;

    /// The principal commit for this version; note that there may be other
    /// commits (branch fix-ups).
    struct changeset * commit;

    union {
        size_t ready_index;             ///< Heap index for emitting versions.
        size_t mark;                    ///< Mark during emission.
    };
};


static inline version_t * version_normalise (version_t * v)
{
    return v ? v - v->implicit_merge : v;
}


static inline version_t * version_live (version_t * v)
{
    return v && !v->dead ? v - v->implicit_merge : NULL;
}


struct tag {
    const char * tag;                   ///< The tag name.

    version_t ** tag_files;
    version_t ** tag_files_end;

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

    bool fixup;                         ///< Did we need a fix-up changeset?

    unsigned rank;

    changeset_t * parent;               ///< Changeset we leach off.

    changeset_t changeset;              ///< Tag emission changeset.

    changeset_t * last;           ///< The last changeset output on this branch.

    fixup_ver_t * fixups;               ///< Array of required fixups.
    fixup_ver_t * fixups_end;
    fixup_ver_t * fixups_curr;          ///< Current position in fixups.
};


/// Initialise a @c tag with @c name.
void tag_init (tag_t * tag, const char * name);

/// Find a @c file_tag for the given @c file and @c tag.
version_t * find_file_tag (file_t * file, tag_t * tag);

static inline tag_t * as_tag (const changeset_t * cs)
{
    assert (cs->type == ct_tag);
    return (tag_t *) (((char *) cs) - offsetof (tag_t, changeset));
}

#endif
