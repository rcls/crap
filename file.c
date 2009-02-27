#include "file.h"
#include "utils.h"

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

version_t * file_new_version (file_t * f)
{
    ARRAY_EXTEND (f->versions);
    f->versions_end[-1].file = f;
    f->versions_end[-1].implicit_merge = false;
    f->versions_end[-1].used = true;
    f->versions_end[-1].ready_index = SIZE_MAX;
    return &f->versions_end[-1];
}


version_t * file_find_version (const file_t * f, const char * s)
{
    return version_normalise (
        find_string (f->versions, f->versions_end - f->versions,
                     sizeof (version_t), offsetof (version_t, version), s));
}


void tag_init (tag_t * tag, const char * name)
{
    changeset_init (&tag->changeset);
    tag->changeset.type = ct_tag;
    tag->changeset.time = -1l << (sizeof (time_t) * 8 - 1);
    assert (tag->changeset.time < 0);
    assert ((tag->changeset.time & (tag->changeset.time - 1)) == 0);

    tag->tag = name;
    tag->tag_files = NULL;
    tag->tag_files_end = NULL;
    tag->branch_versions = NULL;

    tag->parents = NULL;
    tag->parents_end = NULL;

    tag->tags = NULL;
    tag->tags_end = NULL;
    tag->fixup = false;
    tag->parent = NULL;
}


version_t * find_file_tag (file_t * file, tag_t * tag)
{
    version_t ** base = tag->tag_files;
    size_t count = tag->tag_files_end - tag->tag_files;

    while (count > 0) {
        size_t mid = count >> 1;
        version_t ** midp = base + mid;
        if (file < (*midp)->file)
            count = mid;
        else if (file > (*midp)->file) {
            base += mid + 1;
            count -= mid + 1;
        }
        else
            return *midp;
    }

    return NULL;
}
