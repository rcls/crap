
#include "database.h"
#include "file.h"
#include "log.h"
#include "log_parse.h"
#include "string_cache.h"
#include "xmalloc.h"

#include <assert.h>
#include <ctype.h>
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
bool valid_version (const char * s)
{
    size_t position = 0;
    size_t zero_position = 0;

    do {
        if (s[0] == '0') {
            // Forbid extraneous leading zeros.
            if (s[1] != '.')
                return false;
            if (zero_position != 0)
                return false;           /* Only one component can be zero.  */
            if ((position & 1) == 0)
                return false;           /* Must be in even position.  */
            zero_position = position;
        }

        if (!isdigit (*s))
            return false;               /* Must be at least one digit.  */

        while (isdigit (*++s));
        ++position;
    }
    while (*s++ == '.');

    if (s[-1] != 0)
        return false;                   /* Illegal character.  */

    if (position & 1)
        return false;                  /* Must be even number of components.  */

    if (zero_position != 0 && zero_position + 2 != position)
        return false;                   /* A zero must be second-to-last.  */

    return true;
}


bool predecessor (char * s)
{
    /* Check to see if we should just remove the last two components.  This is
     * the case if we are a branch tag (.0.x) or the first rev on a branch
     * (.x.1).  */
    char * l = strrchr (s, '.');
    assert (l);
    if ((l - s > 2 && l[-1] == '0' && l[-2] == '.')
        || (l[1] == '1' && l[2] == 0)) {
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
    if (--*p == '0') {
        /* Rewrite 09999 to 9999 etc.  */
        *p = '9';
        end[-1] = 0;
    }
    return true;
}


static bool tag_is_branch (const file_tag_t * t)
{
    char * l = strrchr (t->vers, '.');
    assert (l);
    return l - t->vers > 2 && l[-1] == '0' && l[-2] == '.';
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


static void fill_in_versions_and_parents (file_t * file)
{
    qsort (file->versions, file->num_versions,
           sizeof (version_t), version_compare);
    qsort (file->file_tags, file->num_file_tags,
           sizeof (file_tag_t), file_tag_compare);

    for (size_t i = file->num_versions; i != 0;) {
        version_t * v = file->versions + --i;
        char vers[1 + strlen (v->version)];
        strcpy (vers, v->version);
        v->parent = NULL;
        while (predecessor (vers)) {
            v->parent = file_find_version (file, vers);
            if (v->parent) {
                v->sibling = v->parent->children;
                v->parent->children = v->sibling;
                break;
            }
        }
    }

    for (size_t i = 0; i != file->num_file_tags; ++i) {
        file_tag_t * ft = file->file_tags + i;
        if (!tag_is_branch (ft)) {
            ft->version = file_find_version (file, ft->vers);
            if (ft->version == NULL)
                warning ("%s: Tag %s version %s does not exist.\n",
                         file->rcs_path, ft->tag->tag, ft->vers);
            continue;
        }
        /* We try and find a predecessor version, to use as the branch point.
         * If none exists, that's fine, it makes sense as a branch addition.  */
        char vers[1 + strlen (ft->vers)];
        strcpy (vers, ft->vers);
        ft->version = NULL;
        while (predecessor (vers)) {
            version_t * v = file_find_version (file, vers);
            if (v != NULL) {
                ft->version = v;
                break;
            }
        }
   }
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

    /* Now print what we've got.  */
/*     const char * nl = memchr (version->log, '\n', log_len); */
/*     if (nl == NULL) */
/*         nl = version->log + strlen (version->log); */

/*     char off_sign = '+'; */
/*     time_t offset = version->offset; */
/*     if (offset < 0) { */
/*         offset = -offset; */
/*         off_sign = '-'; */
/*     } */

/*     struct tm dtm; */
/*     char date[32]; */
/*     size_t dl = strftime (date, sizeof (date), "%F %T %Z", */
/*                           localtime_r (&version->time, &dtm)); */
/*     if (dl == 0) { */
/*         /\* Maybe someone gave us a crap timezone?  *\/ */
/*         dl = strftime (date, sizeof (date), "%F %T %Z", */
/*                        gmtime_r (&version->time, &dtm)); */
/*         assert (dl != 0); */
/*     } */
        
/*     printf ("%s %s %s %s %c%02lu%02lu %.*s\n", */
/*             result->rcs_path, version->version, version->author, date, */
/*             off_sign, offset / 3600, offset / 60 % 60, */
/*             nl - version->log, version->log); */
}


static void read_file_versions (file_database_t * db,
                                string_hash_t * tags,
                                char ** restrict l, size_t * buffer_len,
                                FILE * f)
{
    if (!starts_with (*l, "M RCS file: /"))
        bugger ("Expected RCS file line, not %s\n", *l);

    size_t len = strlen (*l);
    if ((*l)[len - 1] != 'v' || (*l)[len - 2] != ',')
        bugger ("RCS file name does not end with ',v': %s\n", *l);

    file_t * file = file_database_new_file (db);
    file->rcs_path = cache_string_n (*l + 12, len - 14);
    file->num_versions = 0;
    file->versions = NULL;
    file->num_file_tags = 0;
    file->file_tags = NULL;

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
        const char * colon = strrchr (*l, ':');
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
            tag->tag.num_tag_files = 0;
            tag->tag.tag_files = NULL;
        }

        ++colon;
        if (*colon == ' ')
            ++colon;

        /* FIXME - check that version string is a valid version.  */
        file_tag->vers = cache_string (colon);
        file_tag->tag = &tag->tag;

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


void read_files_versions (file_database_t * db,
                          char ** __restrict__ l, size_t * buffer_len, FILE * f)
{
    file_database_init (db);

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
    qsort (db->files, db->num_files, sizeof (file_t), file_compare);

    /* Set the pointers from versions and file_tags to files.  Add the file_tags
     * to the tags.  The latter will be sorted as we have already sorted the
     * files.  */
    for (size_t i = 0; i != db->num_files; ++i) {
        file_t * f = db->files + i;
        for (size_t j = 0; j != f->num_versions; ++j)
            f->versions[j].file = f;

        for (size_t j = 0; j != f->num_file_tags; ++j) {
            file_tag_t * ft = f->file_tags + j;
            ft->file = f;
            tag_new_tag_file (ft->tag, ft);
        }
    }

    /* Flatten the hash of tags to an array.  */
    db->num_tags = 0;
    db->tags = xmalloc (tags.num_entries * sizeof (tag_t));

    for (tag_hash_item_t * i = string_hash_begin (&tags);
         i; i = string_hash_next (&tags, i))
        db->tags[db->num_tags++] = i->tag;

    assert (db->num_tags == tags.num_entries);

    string_hash_destroy (&tags);

    /* Sort the list of tags.  */
    qsort (db->tags, db->num_tags, sizeof (tag_t), tag_compare);

    /* Update the pointers from file_tags to tags.  */
    for (size_t i = 0; i != db->num_tags; ++i) {
        tag_t * t = db->tags + i;
        for (size_t j = 0; j != t->num_tag_files; ++j)
            t->tag_files[i]->tag = t;
    }
}
