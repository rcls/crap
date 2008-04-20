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
};

version_t * file_new_version (file_t * f);
version_t * file_find_version (const file_t * f, const char * s);

file_tag_t * file_new_file_tag (file_t * f);

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

    version_t * cs_sibling;             /* Sibling in changeset.  */
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
    const char * tag;

    size_t num_tag_files;
    size_t max_tag_files;
    file_tag_t ** tag_files;
};

void tag_new_tag_file (tag_t * tag, file_tag_t * file_tag);

#endif
