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
    version_t * versions;

    size_t num_file_tags;
    file_tag_t * file_tags;
};

version_t * file_new_version (file_t * f);
version_t * file_find_version (const file_t * f, const char * s);

file_tag_t * file_new_file_tag (file_t * f);

struct version {
    const char * version;
    bool dead;

    version_t * parent;

    const char * author;
    time_t time;
    time_t offset;
    const char * log;
};


struct file_tag {
    tag_t * tag;
    /* vers is the version information stored in cvs.  For a branch, version is
     * the version to use as the branch point.  Version may be null.  */
    const char * vers;
    version_t * version;
};


typedef struct tag_and_file {
    file_t * file;
    file_tag_t * file_tag;
} tag_and_file_t;


struct tag {
    const char * tag;
    
    size_t num_tag_and_files;
    tag_and_file_t * tag_and_files;
};


#endif
