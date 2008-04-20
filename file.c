#include "file.h"
#include "utils.h"
#include "xmalloc.h"

#include <string.h>
#include <sys/types.h>

version_t * file_new_version (file_t * f)
{
    ARRAY_EXTEND (f->versions, f->num_versions, f->max_versions);
    return f->versions + f->num_versions - 1;
}


file_tag_t * file_new_file_tag (file_t * f)
{
    ARRAY_EXTEND (f->file_tags, f->num_file_tags, f->max_file_tags);
    return f->file_tags + f->num_file_tags - 1;
}


version_t * file_find_version (const file_t * f, const char * s)
{
    version_t * base = f->versions;
    ssize_t count = f->num_versions;

    while (count > 0) {
        size_t mid = count >> 1;

        int c = strcmp (base[mid].version, s);
        if (c == 0)
            return base + mid;

        if (c < 0) {
            base += mid + 1;
            count -= mid + 1;
        }
        else {
            count = mid;
        }
    }

    return NULL;
}


void tag_new_tag_file (tag_t * t, file_tag_t * ft)
{
    ARRAY_EXTEND (t->tag_files, t->num_tag_files, t->max_tag_files);
    t->tag_files [t->num_tag_files - 1] = ft;
}
