#ifndef FILE_H
#define FILE_H

#include <stdbool.h>
#include <time.h>

typedef struct file file_t;
typedef struct version version_t;
typedef struct file_tag file_tag_t;
typedef struct tag tag_t;

struct file {
    const char * path;
    const char * rcs_path;

    size_t num_versions;
    size_t max_versions;
    version_t * versions;

    size_t num_file_tags;
    size_t max_file_tags;
    file_tag_t * file_tags;

    file_tag_t ** branches;
    file_tag_t ** branches_end;
    file_tag_t ** branches_max;
};

version_t * file_new_version (file_t * f);

file_tag_t * file_new_file_tag (file_t * f);
void file_new_branch (file_t *f, file_tag_t * tag);

version_t * file_find_version (const file_t * f, const char * s);

/** Find a branch on which version @c s of file @c lies.  */
file_tag_t * file_find_branch (const file_t * f, const char * s);

struct version {
    file_t * file;
    const char * version;
    bool dead;

    version_t * parent;
    version_t * children;               /* A child, or NULL.  */
    version_t * sibling;                /* A sibling, or NULL.  */

    const char * author;
    const char * commitid;
    time_t time;
    time_t offset;
    const char * log;
    file_tag_t * branch;

    struct changeset * changeset;
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
};


struct tag {
    const char * tag;                   /**< The tag name.  */

    size_t num_tag_files;
    size_t max_tag_files;
    file_tag_t ** tag_files;

#if 0
    /**
     * This is non-NULL for branches, where a tag is considered a branch if the
     * tag is a branch tag on any file.  It points to an array of versions, the
     * same size as the database file array.  Each item in the slot is current
     * version, in the emission of the branch, of the corresponding file.  */
    version_t ** branch_versions;
#endif

    /**
     * A sha-1 hash of the version information; this is used to identify when
     * a set of versions exactly matching this tag has been emitted.
     */
    unsigned char hash[20];
};

void tag_new_tag_file (tag_t * tag, file_tag_t * file_tag);

#endif
