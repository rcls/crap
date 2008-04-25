#include "database.h"
#include "file.h"
#include "log.h"
#include "log_parse.h"
#include "string_cache.h"
#include "utils.h"

#include <assert.h>
#include <ctype.h>
#include <openssl/sha.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#define REV_BOUNDARY "M ----------------------------"
#define FILE_BOUNDARY "M ============================================================================="


typedef struct tag_hash_item {
    string_hash_head_t head;
    tag_t tag;
} tag_hash_item_t;


static size_t next_line (char ** line, size_t * len, FILE * stream)
{
    ssize_t s = getline (line, len, stream);
    if (s < 0)
        bugger ("Unexpected EOF from server.\n");

    if (strlen (*line) < s)
        bugger ("Got line containing ASCII NUL from server.\n");

    if (s > 0 && (*line)[s - 1] == '\n') {
        --s;
        (*line)[s] = 0;
    }

    return s;
}


static inline bool starts_with (const char * haystack, const char * needle)
{
    return strncmp (haystack, needle, strlen (needle)) == 0;
}


static inline bool ends_with (const char * haystack, const char * needle)
{
    size_t h_len = strlen (haystack);
    size_t n_len = strlen (needle);
    return h_len >= n_len
        && memcmp (haystack + h_len - n_len, needle, n_len) == 0;
}


/* Parse a date string into a time_t and an offset; the filled in time includes
 * the offset and hence is a real Unix time.  */
static bool parse_cvs_date (time_t * time, time_t * offset, const char * date)
{
    /* We parse (YY|YYYY)-MM-DD HH:MM(:SS)?( (+|-)HH(MM?))?  This is just like
     * cvsps.  We are a little looser about the digit sequences.  */
    if (!isdigit (date[0]) || !isdigit (date[1]))
        return false;

    struct tm dtm;

    dtm.tm_year = 0;
    if (date[2] != ':')
        dtm.tm_year = -1900;

    char * d;
    unsigned long year = strtoul (date, &d, 10);
    if (year >= 10000 || *d++ != '-')
        return false;

    dtm.tm_year += year;

    dtm.tm_mon = strtoul (d, &d, 10) - 1;
    if (dtm.tm_mon < 0 || dtm.tm_mon > 11 || *d++ != '-')
        return false;

    dtm.tm_mday = strtoul (d, &d, 10);
    if (dtm.tm_mday < 1 || dtm.tm_mday > 31 || *d++ != ' ')
        return false;

    dtm.tm_hour = strtoul (d, &d, 10);
    if (dtm.tm_hour < 0 || dtm.tm_hour > 24 || *d++ != ':')
        return false;

    dtm.tm_min = strtoul (d, &d, 10);
    if (dtm.tm_min < 0 || dtm.tm_min > 59)
        return false;

    if (*d == ':') {
        ++d;
        dtm.tm_sec = strtoul (d, &d, 10);
        if (dtm.tm_sec < 0 || dtm.tm_sec > 61)
            return false;
    }
    else
        dtm.tm_sec = 0;

    if (*d == 0) {
        *time = timegm (&dtm);
        *offset = 0;
        return true;
    }
    if (*d++ != ' ')
        return false;

    int sign;
    if (*d == '+')
        sign = 1;
    else if (*d == '-')
        sign = -1;
    else
        return false;

    if (!isdigit (d[1]) || !isdigit (d[2]))
        return false;

    time_t off = (d[1] - '0') * 36000 + (d[2] - '0') * 3600;
    d += 3;

    if (*d != 0) {
        if (!isdigit (d[0]) || !isdigit (d[1]))
            return false;
        off += (d[0] - '0') * 600 + (d[1] - '0') * 60;
    }
    if (d[3] != 0)
        return false;

    *time = timegm (&dtm) - sign * off;
    *offset = off;
    return true;
}


/* Is a version string value?  I.e., non-empty even-length '.' separated
 * numbers.  */
static bool valid_version (const char * s)
{
    do {
        if (*s < '1' || *s > '9')
            return false;               /* Bogus.  */

        for ( ; *s >= '0' && *s <= '9'; ++s);

        if (*s != '.')
            return false;               /* Bogus.  */

        ++s;

        if (*s < '1' || *s > '9')
            return false;               /* Bogus.  */

        for ( ; *s >= '0' && *s <= '9'; ++s);

        if (*s == 0)
            return true;                /* Done.  */
    }
    while (*s++ == '.');                /* Loop if OK.  */

    return false;                       /* Bogus.  */
}


static bool predecessor (char * s, bool is_branch)
{
    char * l = strrchr (s, '.');
    assert (l);

    if (is_branch) {
        /* Branch; just truncate the last component.  */
        *l = 0;
        return true;
    }

    if (l[1] == '1' && l[2] == 0) {
        /* .1 version; just remove the last two components.  */
        *l = 0;
        l = strrchr (s, '.');
        if (l == NULL)
            return false;
        *l = 0;
        return true;
    }

    /* Decrement the last component.  */
    char * end = s + strlen (s);
    char * p = end;
    while (*--p == '0')
        *p = '9';

    assert (isdigit (*p));
    assert (p != s);
    if (--*p == '0' && p[-1] == '.') {
        /* Rewrite 09999 to 9999 etc.  */
        *p = '9';
        end[-1] = 0;
    }
    return true;
}


/* Normalise a version string for a tag in place.  Rewrite the 'x.y.0.z' style
 * branch tags to 'x.y.z'.  Return -1 on a bogus string, 0 on a normal tag, 1 on
 * a branch tag.  */
static int normalise_tag_version (char * s)
{
    do {
        if (*s < '1' || *s > '9')
            return -1;                  /* Bogus.  */

        for ( ; *s >= '0' && *s <= '9'; ++s);

        if (*s == 0)
            return 1;                   /* x.y.z style branch.  */

        if (*s++ != '.' || *s < '1' || *s > '9')
            return -1;                  /* Bogus.  */

        for ( ; *s >= '0' && *s <= '9'; ++s);

        if (*s == 0)
            return 0;                   /* Done.  */

        if (*s++ != '.')
            return -1;                  /* Bogus.  */
    }
    while (*s != '0');

    /* We hit what should be the '0' of a new-style branch tag.  */
    char * last = s;

    if (*++s != '.' || *++s < '1' || *s > '9')
        return -1;

    for ( ; *s >= '0' && *s <= '9'; ++s);

    if (*s != 0)
        return -1;                      /* Bogus.  */

    memmove (last, last + 2, s - last - 1);

    return 1;
}


static int version_compare (const void * AA, const void * BB)
{
    const version_t * A = AA;
    const version_t * B = BB;
    return strcmp (A->version, B->version);
}


static int file_tag_compare (const void * AA, const void * BB)
{
    const file_tag_t * A = AA;
    const file_tag_t * B = BB;
    return strcmp (A->tag->tag, B->tag->tag);
}


static int branch_compare (const void * AA, const void * BB)
{
    const file_tag_t * A = * (file_tag_t * const *) AA;
    const file_tag_t * B = * (file_tag_t * const *) BB;
    return strcmp (A->vers, B->vers);
}


static void fill_in_versions_and_parents (file_t * file)
{
    qsort (file->versions, file->versions_end - file->versions,
           sizeof (version_t), version_compare);
    qsort (file->file_tags, file->file_tags_end - file->file_tags,
           sizeof (file_tag_t), file_tag_compare);

    /* Fill in the parent, sibling and children links.  */
    for (version_t * v = file->versions_end; v != file->versions;) {
        --v;
        char vers[1 + strlen (v->version)];
        strcpy (vers, v->version);
        v->parent = NULL;
        while (predecessor (vers, false)) {
            v->parent = file_find_version (file, vers);
            if (v->parent) {
                v->sibling = v->parent->children;
                v->parent->children = v;
                break;
            }
        }
    }

    /* Fill in the tag version links, and remove tags to dead versions.  */
    file_tag_t * ft = file->file_tags;
    for (file_tag_t * i = file->file_tags; i != file->file_tags_end; ++i) {
        if (i != ft)
            memcpy (ft, i, sizeof (file_tag_t));

        if (!ft->is_branch) {
            ft->version = file_find_version (file, ft->vers);
            if (ft->version == NULL)
                warning ("%s: Tag %s version %s does not exist.\n",
                         file->rcs_path, ft->tag->tag, ft->vers);
            else if (!ft->version->dead)
                ++ft;
            continue;
        }

        /* We try and find a predecessor version, to use as the branch point.
         * If none exists, that's fine, it makes sense as a branch addition.  */
        char vers[1 + strlen (ft->vers)];
        strcpy (vers, ft->vers);
        if (predecessor (vers, true))
            ft->version = file_find_version (file, vers);
        else
            ft->version = NULL;

        if (ft->version && ft->version->dead)
            /* This hits for branch additions.  We don't log, and unlike tags
             * on dead versions, we keep the file_tag.  */
            ft->version = NULL;

        file_new_branch (file, ft);
        ++ft;
    }
    file->file_tags_end = ft;

    /* Sort the branches by tag.  */
    qsort (file->branches, file->branches_end - file->branches,
           sizeof (file_tag_t *), branch_compare);

    /* Check for duplicate branches.  */
    file_tag_t ** bb = file->branches;
    for (file_tag_t ** i = file->branches; i != file->branches_end; ++i) {
        if (i == file->branches || bb[-1] != *i)
            *bb++ = *i;
        else
            fprintf (stderr, "File %s branch %s duplicates branch %s (%s)\n",
                     file->rcs_path, i[0]->tag->tag, i[-1]->tag->tag,
                     i[0]->version->version);
    }

    /* Fill in the branch pointers on the versions.  FIXME - we should
     * distinguish between trunk versions and missing branches.  */
    for (version_t * i = file->versions; i != file->versions_end; ++i)
        i->branch = file_find_branch (file, i->version);
}


static void read_file_version (file_t * result,
                               char ** __restrict__ l, size_t * buffer_len,
                               FILE * f)
{
    if (!starts_with (*l, "M revision "))
        bugger ("Log (%s) did not have expected 'revision' line: %s\n",
                result->rcs_path, *l);

    version_t * version = file_new_version (result);

    version->version = cache_string (*l + 11);
    if (!valid_version (version->version))
        bugger ("Log (%s) has malformed version %s\n",
                result->rcs_path, version->version);

    version->author = NULL;
    version->commitid = cache_string ("");
/*     version->time = 0; */
/*     version->offset = 0; */
    bool have_date = false;
    version->dead = false;
    version->children = NULL;
    version->sibling = NULL;

    bool state_next = false;
    bool author_next = false;
    bool commitid_next = false;

    size_t len = next_line (l, buffer_len, f);
    while (starts_with (*l, "MT ")) {
        if (starts_with (*l, "MT date ")) {
            if (!parse_cvs_date (&version->time, &version->offset, *l + 8))
                bugger ("Log (%s) date line has unknown format: %s\n",
                        result->rcs_path, *l);
            have_date = true;
        }
        if (author_next) {
            if (!starts_with (*l, "MT text "))
                bugger ("Log (%s) author line is not text: %s\n",
                        result->rcs_path, *l);
            version->author = cache_string (*l + 8);
            author_next = false;
        }
        if (state_next) {
            if (!starts_with (*l, "MT text "))
                bugger ("Log (%s) state line is not text: %s\n",
                        result->rcs_path, *l);
            version->dead = starts_with (*l, "MT text dead");
            state_next = false;
        }
        if (commitid_next) {
            if (!starts_with (*l, "MT text "))
                bugger ("Log (%s) commitid line is not text: %s\n",
                        result->rcs_path, *l);
            version->commitid = cache_string (*l + 8);
            commitid_next = false;
        }
        if (ends_with (*l, " author: "))
            author_next = true;
        if (ends_with (*l, " state: "))
            state_next = true;
        if (ends_with (*l, " commitid: "))
            commitid_next = true;

        len = next_line (l, buffer_len, f);
    }

    /* We don't care about the 'branches:' annotation; we reconstruct the branch
     * information ourselves.  */
    if (starts_with (*l, "M branches: "))
        len = next_line (l, buffer_len, f);

    if (!have_date)
        bugger ("Log (%s) does not have date.\n", result->rcs_path);

    if (version->author == NULL)
        bugger ("Log (%s) does not have author.\n", result->rcs_path);

    /* Snarf the log entry.  */
    char * log = NULL;
    size_t log_len = 0;
    while (strcmp (*l, REV_BOUNDARY) != 0 && strcmp (*l, FILE_BOUNDARY) != 0) {
        log = xrealloc (log, log_len + len + 1);
        memcpy (log + log_len, *l + 2, len - 2);
        log_len += len - 1;
        log[log_len - 1] = '\n';

        len = next_line (l, buffer_len, f);
    }

    version->log = cache_string_n (log, log_len);
    free (log);
}


static void read_file_versions (database_t * db,
                                string_hash_t * tags,
                                char ** restrict l, size_t * buffer_len,
                                FILE * f)
{
    if (!starts_with (*l, "M RCS file: /"))
        bugger ("Expected RCS file line, not %s\n", *l);

    size_t len = strlen (*l);
    if ((*l)[len - 1] != 'v' || (*l)[len - 2] != ',')
        bugger ("RCS file name does not end with ',v': %s\n", *l);

    file_t * file = database_new_file (db);
    file->rcs_path = cache_string_n (*l + 12, len - 14);

    do {
        len = next_line (l, buffer_len, f);
    }
    while (starts_with (*l, "M head:") ||
           starts_with (*l, "M branch:") ||
           starts_with (*l, "M locks:") ||
           starts_with (*l, "M access list:"));

    if (!starts_with (*l, "M symbolic names:"))
        bugger ("Log (%s) did not have expected tag list: %s\n",
                file->rcs_path, *l);

    len = next_line (l, buffer_len, f);
    while (starts_with (*l, "M \t")) {
        char * colon = strrchr (*l, ':');
        if (colon == NULL)
            bugger ("Tag on (%s) did not have version: %s\n",
                    file->rcs_path, *l);

        file_tag_t * file_tag = file_new_file_tag (file);

        const char * tag_name = cache_string_n (*l + 3, colon - *l - 3);
        bool n;
        tag_hash_item_t * tag = string_hash_insert (
            tags, tag_name, sizeof (tag_hash_item_t), &n);
        if (n) {
            tag->tag.tag = tag_name;
            tag->tag.tag_files = NULL;
            tag->tag.tag_files_end = NULL;
            tag->tag.tag_files_max = NULL;
            tag->tag.branch_versions = NULL;
        }

        ++colon;
        if (*colon == ' ')
            ++colon;

        int type = normalise_tag_version (colon);
        if (type < 0)
            bugger ("Tag %s on (%s) has bogus version '%s'\n",
                    tag_name, file->rcs_path, colon);

        file_tag->vers = cache_string (colon);
        file_tag->tag = &tag->tag;
        file_tag->is_branch = type;

        len = next_line (l, buffer_len, f);
    };

    while (starts_with (*l, "M keyword substitution:") ||
           starts_with (*l, "M total revisions:"))
        len = next_line (l, buffer_len, f);

    if (!starts_with (*l, "M description:"))
        bugger ("Log (%s) did not have expected 'description' item: %s\n",
                file->rcs_path, *l);

    /* Just skip until a boundary.  Too bad if a log entry contains one of
     * the boundary strings.  */
    while (strcmp (*l, REV_BOUNDARY) != 0 &&
           strcmp (*l, FILE_BOUNDARY) != 0) {
        if (!starts_with (*l, "M "))
            bugger ("Log (%s) description incorrectly terminated\n",
                    file->rcs_path);
        len = next_line (l, buffer_len, f);
    }

    while (strcmp (*l, FILE_BOUNDARY) != 0) {
        len = next_line (l, buffer_len, f);
        read_file_version (file, l, buffer_len, f);
    }

    next_line (l, buffer_len, f);

    fill_in_versions_and_parents (file);
}


static int file_compare (const void * AA, const void * BB)
{
    const file_t * A = AA;
    const file_t * B = BB;
    return strcmp (A->rcs_path, B->rcs_path);
}


static int tag_compare (const void * AA, const void * BB)
{
    const tag_t * A = AA;
    const tag_t * B = BB;
    return strcmp (A->tag, B->tag);
}


void read_files_versions (database_t * db,
                          char ** __restrict__ l, size_t * buffer_len, FILE * f)
{
    database_init (db);

    string_hash_t tags;
    string_hash_init (&tags);

    next_line (l, buffer_len, f);

    while (strcmp (*l, "ok") != 0) {
        if (strcmp (*l, "M ") == 0) {
            next_line (l, buffer_len, f);
            continue;
        }

        read_file_versions (db, &tags, l, buffer_len, f);
    }

    /* Sort the list of files.  */
    qsort (db->files, db->files_end - db->files, sizeof (file_t), file_compare);

    /* Set the pointers from versions to files and file_tags to files.  Add the
     * file_tags to the tags.  The latter will be sorted as we have already
     * sorted the files.  */
    for (file_t * f = db->files; f != db->files_end; ++f) {
        for (version_t * j = f->versions; j != f->versions_end; ++j)
            j->file = f;

        for (file_tag_t * j = f->file_tags; j != f->file_tags_end; ++j) {
            j->file = f;
            tag_new_tag_file (j->tag, j);
        }
    }

    /* Flatten the hash of tags to an array.  */
    db->tags = ARRAY_ALLOC (tag_t, tags.num_entries);
    db->tags_end = db->tags;

    for (tag_hash_item_t * i = string_hash_begin (&tags);
         i; i = string_hash_next (&tags, i))
        *db->tags_end++ = i->tag;

    assert (db->tags_end == db->tags + tags.num_entries);

    string_hash_destroy (&tags);

    /* Sort the list of tags.  */
    qsort (db->tags, db->tags_end - db->tags, sizeof (tag_t), tag_compare);

    /* Update the pointers from file_tags to tags, and compute the version
     * hashes.  */
    for (tag_t * i = db->tags; i != db->tags_end; ++i) {
        for (file_tag_t ** j = i->tag_files; j != i->tag_files_end; ++j) {
            (*j)->tag = i;
            if (i->branch_versions == NULL && (*j)->is_branch)
                i->branch_versions = ARRAY_CALLOC (version_t *,
                                                   db->files_end - db->files);
        }

        SHA_CTX sha;
        SHA_Init (&sha);
        for (file_tag_t ** j = i->tag_files; j != i->tag_files_end; ++j)
            if ((*j)->version != NULL && !(*j)->version->dead)
                SHA_Update (&sha, &(*j)->version, sizeof (version_t *));

        SHA1_Final ((unsigned char *) i->hash, &sha);
        database_tag_hash_insert (db, i);

        i->is_emitted = false;
    }

    /* Fill in all branches with their initial tags.  */
    for (tag_t * i = db->tags; i != db->tags_end; ++i)
        if (i->branch_versions)
            for (file_tag_t ** j = i->tag_files; j != i->tag_files_end; ++j)
                i->branch_versions[(*j)->file - db->files] = (*j)->version;

    /* Create the trunk branch array.  */
    db->trunk_versions = ARRAY_CALLOC (version_t *, db->files_end - db->files);
}
