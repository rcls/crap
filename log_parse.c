#include "cvs_connection.h"
#include "database.h"
#include "file.h"
#include "log.h"
#include "log_parse.h"
#include "string_cache.h"
#include "utils.h"

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


typedef struct file_tag {
    tag_t * tag;
    const char * version;
} file_tag_t;


static tag_t * get_tag (string_hash_t * tags, const char * name)
{
    bool n;
    tag_hash_item_t * tag = string_hash_insert (tags, name,
                                                sizeof (tag_hash_item_t), &n);
    if (n)
        tag_init (&tag->tag, name);
    return &tag->tag;
}


/// Parse a date string into a time_t and an offset; the filled in time includes
/// the offset and hence is a real Unix time.
static bool parse_cvs_date (time_t * time, time_t * offset, const char * date)
{
    // We parse (YY|YYYY)[-/]MM[-/]DD HH:MM(:SS)?( (+|-)HH(MM?))?  This is just
    // like cvsps.  We are a little looser about the digit sequences.
    if (!isdigit (date[0]) || !isdigit (date[1]))
        return false;

    struct tm dtm;

    dtm.tm_year = 0;
    if (date[2] != ':')
        dtm.tm_year = -1900;

    char * d;
    unsigned long year = strtoul (date, &d, 10);
    if (year >= 10000 || (*d != '-' && *d != '/'))
        return false;

    ++d;
    dtm.tm_year += year;

    dtm.tm_mon = strtoul (d, &d, 10) - 1;
    if (dtm.tm_mon < 0 || dtm.tm_mon > 11 || (*d != '-' && *d != '/'))
        return false;

    ++d;
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
        d += 2;
    }
    if (*d != 0)
        return false;

    *time = timegm (&dtm) - sign * off;
    *offset = off;
    return true;
}


/// Is a version string value?  I.e., non-empty even-length '.' separated
/// numbers.
static bool valid_version (const char * s)
{
    do {
        if (*s < '1' || *s > '9')
            return false;               // Bogus.

        for (; *s >= '0' && *s <= '9'; ++s);

        if (*s != '.')
            return false;               // Bogus.

        ++s;

        if (*s < '1' || *s > '9')
            return false;               // Bogus.

        for (; *s >= '0' && *s <= '9'; ++s);

        if (*s == 0)
            return true;                // Done.
    }
    while (*s++ == '.');                // Loop if OK.

    return false;                       // Bogus.
}


static bool predecessor (char * s)
{
    char * l = strrchr (s, '.');
    assert (l);

    if (l[1] == '1' && l[2] == 0) {
        // .1 version; just remove the last two components.
        *l = 0;
        l = strrchr (s, '.');
        if (l == NULL)
            return false;
        *l = 0;
        return true;
    }

    // Decrement the last component.
    char * end = s + strlen (s);
    char * p = end;
    while (*--p == '0')
        *p = '9';

    assert (isdigit (*p));
    assert (p != s);
    if (--*p == '0' && p[-1] == '.') {
        // Rewrite 09999 to 9999 etc.
        *p = '9';
        end[-1] = 0;
    }
    return true;
}


/// Normalise a version string for a tag in place.  Rewrite the 'x.y.0.z' style
/// branch tags to 'x.y.z'.
static bool normalise_tag_version (char * s)
{
    do {
        if (*s < '1' || *s > '9')
            return false;               // Bogus.

        for (; *s >= '0' && *s <= '9'; ++s);

        if (*s == 0)
            return true;                // x.y.z style branch.

        if (*s++ != '.' || *s < '1' || *s > '9')
            return false;               // Bogus.

        for (; *s >= '0' && *s <= '9'; ++s);

        if (*s == 0)
            return true;                // Done.

        if (*s++ != '.')
            return false;               // Bogus.
    }
    while (*s != '0');

    // We hit what should be the '0' of a new-style branch tag.
    char * last = s;

    if (*++s != '.' || *++s < '1' || *s > '9')
        return false;

    for (; *s >= '0' && *s <= '9'; ++s);

    if (*s != 0)
        return false;                   // Bogus.

    memmove (last, last + 2, s - last - 1);

    return true;
}


/// Is this version a branch version?  Assumes the normalisation above, so we
/// just count the '.'s.  This does the right thing on the empty string, which
/// is used as the branch version for the trunk.
static bool is_branch (const char * v)
{
    bool res = true;
    for (; *v; ++v)
        res ^= (*v == '.');
    return res;
}


static int compare_version (const void * AA, const void * BB)
{
    const version_t * A = AA;
    const version_t * B = BB;
    if (A->version != B->version)
        return strcmp (A->version, B->version);
    else
        return A->implicit_merge - B->implicit_merge;
}


static int compare_file_tag (const void * AA, const void * BB)
{
    const file_tag_t * A = AA;
    const file_tag_t * B = BB;
    return strcmp (A->tag->tag, B->tag->tag);
}


static int compare_versionp (const void * AA, const void * BB)
{
    version_t * const * A = AA;
    version_t * const * B = BB;
    if ((*A)->file < (*B)->file)
        return -1;
    if ((*A)->file > (*B)->file)
        return 1;
    return 0;
}


static int compare_branch (const void * AA, const void * BB)
{
    const file_tag_t * A = AA;
    const file_tag_t * B = BB;
    return strcmp (A->version, B->version);
}


static tag_t * find_branch (const file_t * f,
                            const file_tag_t * branches,
                            const file_tag_t * branches_end,
                            const char * s,
                            string_hash_t * tags)
{
    char vers[strlen (s) + 1];
    strcpy (vers, s);
    char * dot = strrchr (vers, '.');
    assert (dot != NULL);
    if (memchr (vers, '.', dot - vers) == NULL)
        dot = vers;                     // On trunk.

    *dot = 0;                           // Truncate the last component.

    // Now bsearch for the branch.
    const file_tag_t * b = find_string (
        branches, branches_end - branches,
        sizeof (file_tag_t), offsetof (file_tag_t, version), vers);

    if (b != NULL)
        return b->tag;

    // Use a branch name 'unnamed-<vers>'.  It's not ideal but the best we can
    // do right here.
    char br[8 + strlen (vers) + 1];
    strcpy (br, "unnamed-");
    strcat (br, vers);

    // Get the tag and mark it as a branch.
    tag_t * branch = get_tag (tags, cache_string (br));
    static version_t * dummy_pointer;
    branch->branch_versions = &dummy_pointer;

    // Record a branch point if not already done.
    if (branch->tag_files_end != branch->tag_files
        && branch->tag_files_end[-1]->file != f)
        return branch;

    dot = strrchr (vers, '.');
    assert (dot != NULL);

    *dot = 0;
    version_t * branch_point = file_find_version (f, vers);
    if (branch_point == NULL || branch_point->dead)
        return branch;

    ARRAY_APPEND (branch->tag_files, branch_point);

    if (branch_point->time > branch->changeset.time)
        branch->changeset.time = branch_point->time;

    return branch;
}


/// Fill in the parent, sibling and children links.
static void fill_in_parents (file_t * file)
{
    for (version_t * v = file->versions_end; v != file->versions;) {
        --v;
        char vers[1 + strlen (v->version)];
        strcpy (vers, v->version);
        v->parent = NULL;
        while (predecessor (vers)) {
            v->parent = file_find_version (file, vers);
            if (v->parent) {
                // The parent of an implicit merge should be an implicit merge
                // if possible.
                if (v->implicit_merge && v->parent != file->versions_end
                    && v->parent[1].implicit_merge) {
                    assert (v->parent->version == v->parent[1].version);
                    ++v->parent;
                }
                v->sibling = v->parent->children;
                v->parent->children = v;
                break;
            }
        }
    }
}


static void fill_in_versions_and_parents (file_t * file, bool attic,
                                          file_tag_t * file_tags,
                                          file_tag_t * file_tags_end,
                                          string_hash_t * tags)
{
    ARRAY_SORT (file->versions, compare_version);
    ARRAY_TRIM (file->versions);

    fill_in_parents (file);

    // If the file is in the Attic, make sure any last version on the trunk is
    // dead.  FIXME - maybe should insert a dead version instead of munging
    // the dead flag?
    if (attic) {
        unsigned long max = 0;
        version_t * last = NULL;
        for (version_t * i = file->versions; i != file->versions_end; ++i)
            if (i->version[0] == '1' && i->version[1] == '.') {
                char * tail = NULL;
                unsigned long ver = strtoul (i->version + 2, &tail, 10);
                if (tail != NULL && *tail == 0 && ver >= max) {
                    last = i;
                    max = ver;
                }
            }
        if (last != NULL && !last->dead) {
            last->dead = true;
            fprintf (stderr, "Killing zombie version %s %s\n",
                     file->path, last->version);
        }
    }

    file_tag_t * branches = NULL;
    file_tag_t * branches_end = NULL;

    // Sort tags so we can detect duplicates.
    ARRAY_SORT (file_tags, compare_file_tag);

    // Fill in the tag version links, and remove tags to dead versions.
    for (file_tag_t * i = file_tags; i != file_tags_end; ++i) {
        if (i != file_tags && i[-1].tag == i[0].tag) {
            warning ("%s: Duplicate tag '%s', versions '%s' and '%s'.\n",
                     file->path, i->tag->tag, i[-1].version, i->version);
            continue;
        }

        if (!is_branch (i->version)) {
            version_t * version = file_find_version (file, i->version);
            if (version == NULL)
                warning ("%s: Tag %s version %s does not exist.\n",
                         file->path, i->tag->tag, i->version);
            else if (!version->dead)
                // FIXME - it might be better to keep dead version tags, because
                // that would allow better tag matching.
                ARRAY_APPEND (i->tag->tag_files, version);
            continue;
        }

        // Record the branch on the branch list.
        ARRAY_APPEND (branches, *i);

        // We try and find a predecessor version, to use as the branch point.
        // If none exists, that's fine, it makes sense as a branch addition.
        if (i->version[0] == 0)
            continue;                   // All trunk files are trunk additions.

        size_t len = strrchr (i->version, '.') - i->version;
        char vers[len + 1];
        memcpy (vers, i->version, len);
        vers[len] = 0;
        version_t * version = file_find_version (file, vers);

        if (version == NULL)
            continue;                   // Branch addition.

        if (version->time > i->tag->changeset.time)
            i->tag->changeset.time = version->time;

        if (!version->dead)
            ARRAY_APPEND (i->tag->tag_files, version);
    }

    // Sort the branches by version.
    ARRAY_SORT (branches, compare_branch);

    // Mark the branches as such.  Check for duplicate branches.
    file_tag_t * bb = branches;
    for (file_tag_t * i = branches; i != branches_end; ++i)
        if (i == branches || bb[-1].version != i->version) {
            *bb++ = *i;
            static version_t * dummy_pointer;
            i->tag->branch_versions = &dummy_pointer;
        }
        else
            fprintf (stderr, "File %s branch %s duplicates branch %s (%s)\n",
                     file->path, i->tag->tag, i[-1].tag->tag,
                     i->version);
    branches_end = bb;

    // Fill in the branch pointers on the versions.
    for (version_t * i = file->versions; i != file->versions_end; ++i)
        if (i->implicit_merge)
            i->branch = find_branch (
                file, branches, branches_end, "1.1", tags); // FIXME.
        else
            i->branch = find_branch (
                file, branches, branches_end, i->version, tags);

    free (branches);
}


static size_t read_mt_key_values (file_t * file,
                                  version_t * version,
                                  cvs_connection_t * s)
{
    bool have_date = false;

    bool state_next = false;
    bool author_next = false;
    bool commitid_next = false;

    size_t len;
    do {
        if (starts_with (s->line, "MT date ")) {
            if (!parse_cvs_date (&version->time, &version->offset, s->line + 8))
                fatal ("Log (%s) date line has unknown format: %s\n",
                       file->rcs_path, s->line);
            have_date = true;
        }
        if (author_next) {
            if (!starts_with (s->line, "MT text "))
                fatal ("Log (%s) author line is not text: %s\n",
                       file->rcs_path, s->line);
            version->author = cache_string (s->line + 8);
            author_next = false;
        }
        if (state_next) {
            if (!starts_with (s->line, "MT text "))
                fatal ("Log (%s) state line is not text: %s\n",
                       file->rcs_path, s->line);
            version->dead = starts_with (s->line, "MT text dead");
            state_next = false;
        }
        if (commitid_next) {
            if (!starts_with (s->line, "MT text "))
                fatal ("Log (%s) commitid line is not text: %s\n",
                       file->rcs_path, s->line);
            version->commitid = cache_string (s->line + 8);
            commitid_next = false;
        }
        if (ends_with (s->line, " author: "))
            author_next = true;
        if (ends_with (s->line, " state: "))
            state_next = true;
        if (ends_with (s->line, " commitid: "))
            commitid_next = true;

        len = next_line (s);
    }
    while (starts_with (s->line, "MT "));

    if (!have_date)
        fatal ("Log (%s) does not have date.\n", file->rcs_path);

    if (version->author == NULL)
        fatal ("Log (%s) does not have author.\n", file->rcs_path);

    return len;
}


static void read_m_key_values (file_t * file, version_t * version,
                               const char * l)
{
    bool have_date = false;

    while (l) {
        const char * end = strchr (l, ';');
        while (end != NULL && end[1] && (end[1] != ' ' || end[2] != ' '))
            end = strchr (end + 1, ';');
        if (end == NULL)
            break;

        if (starts_with (l, "date: ")) {
            l += 6;
            char date[end - l + 1];
            memcpy (date, l, end - l);
            date[end - l] = 0;
            if (!parse_cvs_date (&version->time, &version->offset, date))
                fatal ("Log (%s) date has unknown format: %s\n",
                       file->rcs_path, date);
            have_date = true;
        }
        else if (starts_with (l, "author: "))
            version->author = cache_string_n (l + 8, end - l - 8);
        else if (starts_with (l, "state: dead"))
            version->dead = true;
        else if (starts_with (l, "commitid: "))
            version->commitid = cache_string_n (l + 10, end - l - 10);

        l = end + 1;
        if (l[0] == ' ' && l[1] == ' ')
            l += 2;
    }

    if (!have_date)
        fatal ("Log (%s) does not have date.\n", file->rcs_path);

    if (version->author == NULL)
        fatal ("Log (%s) does not have author.\n", file->rcs_path);
}


static void read_file_version (file_t * file, cvs_connection_t * s)
{
    if (!starts_with (s->line, "M revision "))
        fatal ("Log (%s) did not have expected 'revision' line: %s\n",
               file->rcs_path, s->line);

    version_t * version = file_new_version (file);

    version->version = cache_string (s->line + 11);
    if (!valid_version (version->version))
        fatal ("Log (%s) has malformed version %s\n",
               file->rcs_path, version->version);

    version->author = NULL;
    version->commitid = cache_string ("");
    version->dead = false;
    version->children = NULL;
    version->sibling = NULL;

    size_t len = next_line (s);
    if (starts_with (s->line, "MT "))
        len = read_mt_key_values (file, version, s);
    else if (starts_with (s->line, "M date: ")) {
        read_m_key_values (file, version, s->line + 2);
        len = next_line (s);
    }
    else
        fatal ("Log (%s) has malformed date/author/state: %s",
               file->rcs_path, s->line);

    // We don't care about the 'branches:' annotation; we reconstruct the branch
    // information ourselves.
    if (starts_with (s->line, "M branches: "))
        len = next_line (s);

    // Snarf the log entry.
    char * log = NULL;
    size_t log_len = 0;
    while (strcmp (s->line, REV_BOUNDARY) != 0
           && strcmp (s->line, FILE_BOUNDARY) != 0) {
        log = xrealloc (log, log_len + len + 1);
        memcpy (log + log_len, s->line + 2, len - 2);
        log_len += len - 1;
        log[log_len - 1] = '\n';

        len = next_line (s);
    }

    version->log = cache_string_n (log, log_len);
    free (log);

    // FIXME - improve this test.
    if (strncmp (version->version, "1.1.1.", 6) == 0
        && strchr (version->version + 6, '.') == NULL) {
        // Looks like like a vendor import; create an implicit merge item.
        ARRAY_EXTEND (file->versions);
        file->versions_end[-1] = file->versions_end[-2];
        file->versions_end[-1].implicit_merge = true;
    }
}


static void read_file_versions (database_t * db,
                                string_hash_t * tags,
                                cvs_connection_t * s)
{
    if (!starts_with (s->line, "M RCS file: /"))
        fatal ("Expected RCS file line, not %s\n", s->line);

    size_t len = strlen (s->line);
    if ((s->line)[len - 1] != 'v' || (s->line)[len - 2] != ',')
        fatal ("RCS file name does not end with ',v': %s\n", s->line);

    file_t * file = database_new_file (db);
    file->rcs_path = cache_string_n (s->line + 12, len - 12);

    if (!starts_with (s->line + 12, s->prefix))
        fatal ("RCS file name '%s' does not start with prefix '%s'\n",
               s->line + 12, s->prefix);

    (s->line)[len - 2] = 0;                 // Remove the ',v'
    char * last_slash = strrchr (s->line, '/');
    bool attic = false;
    if (last_slash != NULL && last_slash - s->line >= 18 &&
        memcmp (last_slash - 6, "/Attic", 6) == 0) {
        // Remove that Attic portion.  We can't use strcpy because the strings
        // may overlap.
        attic = true;
        memmove (last_slash - 6, last_slash, strlen (last_slash) + 1);
    }

    file->path = cache_string (s->line + 12 + strlen (s->prefix));

    file_tag_t * file_tags = NULL;
    file_tag_t * file_tags_end = NULL;

    // Add a fake branch for the trunk.
    const char * empty_string = cache_string ("");
    ARRAY_EXTEND (file_tags);
    file_tags_end[-1].tag = get_tag (tags, empty_string);
    file_tags_end[-1].version = empty_string;

    do
        len = next_line (s);
    while (starts_with (s->line, "M head:") ||
           starts_with (s->line, "M branch:") ||
           starts_with (s->line, "M locks:") ||
           starts_with (s->line, "M access list:"));

    if (!starts_with (s->line, "M symbolic names:"))
        fatal ("Log (%s) did not have expected tag list: %s\n",
               file->rcs_path, s->line);

    len = next_line (s);

    while (starts_with (s->line, "M \t")) {
        char * colon = strrchr (s->line, ':');
        if (colon == NULL)
            fatal ("Tag on (%s) did not have version: %s\n",
                   file->rcs_path, s->line);

        const char * tag_name = cache_string_n (s->line + 3,
                                                colon - s->line - 3);
        ++colon;
        if (*colon == ' ')
            ++colon;

        if (!normalise_tag_version (colon))
            fatal ("Tag %s on (%s) has bogus version '%s'\n",
                   tag_name, file->rcs_path, colon);

        ARRAY_EXTEND (file_tags);
        file_tags_end[-1].tag = get_tag (tags, tag_name);
        file_tags_end[-1].version = cache_string (colon);

        len = next_line (s);
    };

    while (starts_with (s->line, "M keyword substitution:") ||
           starts_with (s->line, "M total revisions:"))
        len = next_line (s);

    if (!starts_with (s->line, "M description:"))
        fatal ("Log (%s) did not have expected 'description' item: %s\n",
               file->rcs_path, s->line);

    // Just skip until a boundary.  Too bad if a log entry contains one of
    // the boundary strings.
    while (strcmp (s->line, REV_BOUNDARY) != 0
           && strcmp (s->line, FILE_BOUNDARY) != 0) {
        if (!starts_with (s->line, "M "))
            fatal ("Log (%s) description incorrectly terminated\n",
                   file->rcs_path);
        len = next_line (s);
    }

    while (strcmp (s->line, FILE_BOUNDARY) != 0) {
        len = next_line (s);
        read_file_version (file, s);
    }

    next_line (s);

    fill_in_versions_and_parents (file, attic, file_tags, file_tags_end, tags);

    xfree (file_tags);
}


static int compare_file (const void * AA, const void * BB)
{
    const file_t * A = AA;
    const file_t * B = BB;
    return strcmp (A->path, B->path);
}


static int compare_tag (const void * AA, const void * BB)
{
    const tag_t * A = AA;
    const tag_t * B = BB;
    return strcmp (A->tag, B->tag);
}


void read_files_versions (database_t * db, cvs_connection_t * s)
{
    database_init (db);

    string_hash_t tags;
    string_hash_init (&tags);

    next_line (s);

    while (strcmp (s->line, "ok") != 0)
        if (strcmp (s->line, "M ") == 0)
            next_line (s);
        else
            read_file_versions (db, &tags, s);

    // Sort the list of files.
    ARRAY_SORT (db->files, compare_file);

    // Set the pointers from versions to files.
    for (file_t * f = db->files; f != db->files_end; ++f)
        for (version_t * j = f->versions; j != f->versions_end; ++j)
            j->file = f;

    // Flatten the hash of tags to an array.
    db->tags = ARRAY_ALLOC (tag_t, tags.num_entries);
    db->tags_end = db->tags;

    for (tag_hash_item_t * i = string_hash_begin (&tags);
         i; i = string_hash_next (&tags, i))
        *db->tags_end++ = i->tag;

    assert (db->tags_end == db->tags + tags.num_entries);

    // Sort the list of tags.
    ARRAY_SORT (db->tags, compare_tag);
    for (tag_t * i = db->tags; i != db->tags_end; ++i) {
        tag_hash_item_t * h = string_hash_find (&tags, i->tag);
        assert (h);
        assert (h->tag.tag == i->tag);
        h->tag.parent = &i->changeset;
    }

    // Restore the branch pointers.
    for (file_t * i = db->files; i != db->files_end; ++i)
        for (version_t * j = i->versions; j != i->versions_end; ++j)
            if (j->branch)
                j->branch = as_tag (j->branch->parent);

    // Sort the tag version lists.  Set the initial branch version lists.
    for (tag_t * i = db->tags; i != db->tags_end; ++i) {
        ARRAY_TRIM (i->tag_files);
        ARRAY_SORT (i->tag_files, compare_versionp);
        if (i->branch_versions) {
            i->branch_versions = ARRAY_CALLOC (version_t *,
                                               db->files_end - db->files);
            for (version_t ** j = i->tag_files; j != i->tag_files_end; ++j)
                i->branch_versions[(*j)->file - db->files] = *j;
        }

        i->is_released = false;
    }

    string_hash_destroy (&tags);
}
