/// @file
/// Handling of tag/branch fixups.  A tag (or start of a branch) may contain
/// differences from the state of the point we're placed it in the parent
/// branch.
///
/// Detect these, and insert fixup-commits as required.

#include "database.h"
#include "errno.h"
#include "file.h"
#include "fixup.h"
#include "log.h"
#include "utils.h"

#include <limits.h>
#include <stdlib.h>

// FIXME - assumes signed time_t!
#define TIME_MIN (sizeof(time_t) == sizeof(int) ? INT_MIN : LONG_MIN)
#define TIME_MAX (sizeof(time_t) == sizeof(int) ? INT_MAX : LONG_MAX)


static int compare_fixup_by_time (const void * AA, const void * BB)
{
    const fixup_ver_t * A = AA;
    const fixup_ver_t * B = BB;
    if (A->time < B->time)
        return -1;

    if (A->time > B->time)
        return 1;

    return 0;
}


void create_fixups(const database_t * db,
                   version_t * const * branch_versions, tag_t * tag)
{
    // Go through the current versions on the branch and note any version
    // fix-ups required.
    assert (tag->fixups == NULL);
    assert (tag->fixups_end == NULL);

    assert (TIME_MIN < 0);
    assert (TIME_MAX > 0);
    assert (TIME_MIN == (time_t) ((unsigned long long) TIME_MAX + 1));

    version_t ** tf = tag->tag_files;
    for (file_t * i = db->files; i != db->files_end; ++i) {
        version_t * bv = branch_versions ? version_normalise (
            branch_versions[i - db->files]) : NULL;
        version_t * tv = NULL;
        if (tf != tag->tag_files_end && (*tf)->file == i)
            tv = version_normalise (*tf++);

        version_t * bvl = bv == NULL || bv->dead ? NULL : bv;
        version_t * tvl = tv == NULL || tv->dead ? NULL : tv;

        if (bvl == tvl)
            continue;

        // The only fixups we defer are files that spontaneously appear on
        // the tag.  Everything else we assume was there from the start.
        time_t fix_time;
        if (tv != NULL && branch_versions
            && branch_versions[i - db->files] == NULL)
            fix_time = tv->time;
        else
            fix_time = TIME_MIN;

        ARRAY_APPEND (tag->fixups, ((fixup_ver_t) {
                    .file = i, .version = tvl, .time = fix_time }));
    }

    tag->fixups_curr = tag->fixups;

    // Sort fix-ups by date.
    ARRAY_SORT (tag->fixups, compare_fixup_by_time);
}


static int compare_fixup_by_file (const void * AA, const void * BB)
{
    const fixup_ver_t * A = AA;
    const fixup_ver_t * B = BB;
    if (A->file < B->file)
        return -1;

    if (A->file > B->file)
        return 1;

    return 0;
}


static int compare_file_version (const void * KK, const void * VV)
{
    const file_t * K = KK;
    const version_t * const * V = VV;
    if (K < (*V)->file)
        return -1;
    if (K > (*V)->file)
        return 1;
    return 0;
}


static version_t * changeset_find_file (const changeset_t * cs,
                                        const file_t * file)
{
    return bsearch (file, cs->versions, cs->versions_end - cs->versions,
                    sizeof (*cs->versions), compare_file_version);
}


void fixup_list (fixup_ver_t ** fixups, fixup_ver_t ** fixups_end,
                 tag_t * tag, const changeset_t * cs)
{
    *fixups = NULL;
    *fixups_end = NULL;

    time_t time = TIME_MAX;
    if (cs != NULL)
        time = cs->time;

    for (; tag->fixups_curr != tag->fixups_end
             && tag->fixups_curr->time <= time;
         ++tag->fixups_curr)
        if (tag->fixups_curr->file != NULL) {
            ARRAY_APPEND (*fixups, *tag->fixups_curr);
            tag->fixups_curr->file = NULL;
        }

    int remaining = 0;
    for (fixup_ver_t * i = tag->fixups_curr; i != tag->fixups_end; ++i)
        if (i->file == NULL)
            ;
        else if (changeset_find_file (cs, i->file)) {
            ARRAY_APPEND (*fixups, *i);
            i->file = NULL;
        }
        else
            ++remaining;

    // Sort the fixups by file...
    ARRAY_SORT (*fixups, compare_fixup_by_file);

    if (remaining > (tag->fixups_end - tag->fixups) / 2)
        return;

    if (remaining == 0) {
        if (tag->fixups != NULL) {
            xfree (tag->fixups);
            tag->fixups = NULL;
            tag->fixups_end = NULL;
            tag->fixups_curr = NULL;
        }
        return;
    }

    // Repack the array.
    fixup_ver_t * j = tag->fixups;
    for (fixup_ver_t * i = tag->fixups_curr; i != tag->fixups_end; ++i)
        if (i->file != NULL)
            *j++ = *i;

    tag->fixups_end = j;
    ARRAY_TRIM(tag->fixups);
    tag->fixups_curr = tag->fixups;
}


char * fixup_commit_comment (const database_t * db,
                             version_t * const * base_versions, tag_t * tag,
                             fixup_ver_t * fixups,
                             fixup_ver_t * fixups_end)
{
    // Generate stats.
    size_t keep = 0;
    size_t added = 0;
    size_t deleted = 0;
    size_t modified = 0;

    fixup_ver_t * ffv = fixups;
    for (file_t * i = db->files; i != db->files_end; ++i) {
        version_t * bv = base_versions ?
            version_live (base_versions[i - db->files]) : NULL;
        version_t * tv;
        if (ffv != fixups_end && ffv->file == i)
            tv = ffv++->version;
        else
            tv = bv;

        if (bv == tv) {
            if (bv != NULL)
                ++keep;
            continue;
        }

        if (tv == NULL) {
            ++deleted;
            continue;
        }

        if (bv == NULL)
            ++added;
        else
            ++modified;
    }

    assert (ffv == fixups_end);

    // Generate the commit comment.
    char * result;
    size_t res_size;

    FILE * f = open_memstream(&result, &res_size);
    if (f == NULL)
        fatal ("open_memstream failed: %s\n", strerror (errno));

    fprintf (f, "Fix-up commit generated by crap-clone.  "
             "(~%zu +%zu -%zu =%zu)\n", modified, added, deleted, keep);

    ffv = fixups;
    for (file_t * i = db->files; i != db->files_end; ++i) {
        version_t * bv = base_versions ?
            version_live (base_versions[i - db->files]) : NULL;
        version_t * tv = NULL;
        if (ffv != fixups_end && ffv->file == i)
            tv = ffv++->version;
        else
            tv = bv;

        if (bv == tv) {
            if (bv != NULL && keep <= deleted)
                fprintf (f, "%s KEEP %s\n", bv->file->path, bv->version);
            continue;
        }

        if (tv != NULL || deleted <= keep)
            fprintf (f, "%s %s->%s\n", i->path,
                     bv ? bv->version : "ADD", tv ? tv->version : "DELETE");
    }

    if (ferror (f))
        fatal ("memstream: error creating log message\n");

    fclose (f);

    return result;
}
