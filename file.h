#ifndef FILE_H
#define FILE_H

#include <stdbool.h>
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
    version_t * versions_max;

    file_tag_t * file_tags;
    file_tag_t * file_tags_end;
    file_tag_t * file_tags_max;

    file_tag_t ** branches;
    file_tag_t ** branches_end;
    file_tag_t ** branches_max;
};

version_t * file_new_version (file_t * f);

void file_new_branch (file_t *f, file_tag_t * tag);

/**
 * Find a file version object by the version string.  The version string @c s
 * need not be cached.  */
version_t * file_find_version (const file_t * f, const char * s);

/** Find a branch on which version @c s of file @c lies.  */
file_tag_t * file_find_branch (const file_t * f, const char * s);

struct version {
    file_t * file;
    const char * version;
    bool dead;

    /**
     * Indicate that this revision is part of a vendor branch import that should
     * be implicitly merged to the trunk.
     */
    bool implicit_merge;

    version_t * parent;
    version_t * children;               /* A child, or NULL.  */
    version_t * sibling;                /* A sibling, or NULL.  */

    const char * author;
    const char * commitid;
    time_t time;
    time_t offset;
    const char * log;
    file_tag_t * branch;

    struct commit * commit;
    version_t * cs_sibling;             /**< Sibling in changeset.  */

    size_t ready_index;               /**< Heap index for emitting versions.  */
};


struct file_tag {
    file_t * file;
    tag_t * tag;
    /* vers is the version information stored in cvs.  For a branch, version is
     * the version to use as the branch point.  Version may be null.  */
    const char * vers;
    version_t * version;
    bool is_branch;
};


struct tag {
    const char * tag;                   /**< The tag name.  */

    file_tag_t ** tag_files;
    file_tag_t ** tag_files_end;
    file_tag_t ** tag_files_max;

    /**
     * This is non-NULL for branches, where a tag is considered a branch if the
     * tag is a branch tag on any file.  It points to an array of versions, the
     * same size as the database file array.  Each item in the slot is current
     * version, in the emission of the branch, of the corresponding file.  */
    version_t ** branch_versions;

    /**
     * A sha-1 hash of the version information; this is used to identify when
     * a set of versions exactly matching this tag has been emitted.
     */
    uint32_t hash[5];
    tag_t * hash_next;
    bool is_emitted;
};

void tag_new_tag_file (tag_t * tag, file_tag_t * file_tag);

#endif
