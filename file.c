#include "file.h"
#include "utils.h"

#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

version_t * file_new_version (file_t * f)
{
    ARRAY_EXTEND (f->versions, f->versions_end, f->versions_max);
    f->versions_end[-1].implicit_merge = false;
    f->versions_end[-1].ready_index = SIZE_MAX;
    return &f->versions_end[-1];
}


void file_new_branch (file_t * f, file_tag_t * tag)
{
    ARRAY_EXTEND (f->branches, f->branches_end, f->branches_max);
    f->branches_end[-1] = tag;
}


version_t * file_find_version (const file_t * f, const char * s)
{
    version_t * base = f->versions;
    ssize_t count = f->versions_end - f->versions;

    while (count > 0) {
        size_t mid = count >> 1;

        int c = strcmp (base[mid].version, s);
        if (c < 0) {
            base += mid + 1;
            count -= mid + 1;
        }
        else if (c > 0)
            count = mid;
        else
            return base + mid;
    }

    return NULL;
}


file_tag_t * file_find_branch (const file_t * f, const char * s)
{
    char vers[strlen (s) + 1];
    strcpy (vers, s);
    char * dot = strrchr (vers, '.');
    assert (dot != NULL);
    if (memchr (vers, '.', dot - vers) == NULL)
        dot = vers;                     /* On trunk.  */

    /* Truncate the last component.  */
    *dot = 0;

    /* Now bsearch for the branch.  */
    file_tag_t ** base = f->branches;
    ssize_t count = f->branches_end - f->branches;

    while (count > 0) {
        size_t mid = count >> 1;

        int c = strcmp (base[mid]->vers, vers);
        if (c < 0) {
            base += mid + 1;
            count -= mid + 1;
        }
        else if (c > 0)
            count = mid;
        else
            return base[mid];
    }

    fprintf (stderr, "File %s version %s (%s) has no branch\n",
             f->rcs_path, s, vers);

    return NULL;
}


void tag_new_tag_file (tag_t * t, file_tag_t * ft)
{
    ARRAY_EXTEND (t->tag_files, t->tag_files_end, t->tag_files_max);
    t->tag_files_end[-1] = ft;
}
