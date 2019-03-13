#include "cvs_connection.h"
#include "branch.h"
#include "changeset.h"
#include "database.h"
#include "emission.h"
#include "file.h"
#include "filter.h"
#include "fixup.h"
#include "log.h"
#include "log_parse.h"
#include "string_cache.h"
#include "utils.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <pipeline.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>

struct author_list { /* table entry: */
    struct author_list *next; /* next entry in chain */
    const char *name; /* defined name */
    const char *author_name; /* replacement text */
    const char *author_mail; /* replacement text */
};

#define HASHSIZE 101
static struct author_list *hashtab[HASHSIZE]; /* pointer table */

/* hash: form hash value for string s */
unsigned hash(const char *s)
{
    unsigned hashval;
    for (hashval = 0; *s != '\0'; s++)
      hashval = *s + 31 * hashval;
    return hashval % HASHSIZE;
}

/* lookup: look for s in hashtab */
struct author_list *lookup(const char *s)
{
    struct author_list *np;
    for (np = hashtab[hash(s)]; np != NULL; np = np->next)
        if (strcmp(s, np->name) == 0)
          return np; /* found */
    return NULL; /* not found */
}

/* install: put (name, author_name, author_mail) in hashtab */
struct author_list *install(const char *name, const char *author_name, const char *author_mail)
{
    struct author_list *np;
    unsigned hashval;
    if ((np = lookup(name)) == NULL) { /* not found */
        np = (struct author_list *) malloc(sizeof(*np));
        if (np == NULL || (np->name = strdup(name)) == NULL)
          return NULL;
        hashval = hash(name);
        np->next = hashtab[hashval];
        hashtab[hashval] = np;
    } else { /* already there */
        free((void *) np->author_name); /*free previous author_name */
        free((void *) np->author_mail); /*free previous author_mail */
	}
    if ((np->author_name = strdup(author_name)) == NULL)
       return NULL;
    if ((np->author_mail = strdup(author_mail)) == NULL)
       return NULL;
    return np;
}

enum {
    opt_fuzz_span = 256,
    opt_fuzz_gap,
};

static const struct option opts[] = {
    { "author-conv-file", required_argument, NULL, 'A' },
    { "branch-prefix",    required_argument, NULL, 'b' },
    { "compress",         required_argument, NULL, 'z' },
    { "entries",          required_argument, NULL, 'e' },
    { "filter",           required_argument, NULL, 'F' },
    { "force",            no_argument,       NULL, 'f' },
    { "help",             no_argument,       NULL, 'h' },
    { "master",           required_argument, NULL, 'm' },
    { "output",           required_argument, NULL, 'o' },
    { "remote",           required_argument, NULL, 'r' },
    { "tag-prefix",       required_argument, NULL, 't' },
    { "version-cache",    required_argument, NULL, 'c' },
    { "directory",        required_argument, NULL, 'd' },
    { "fuzz-span",        required_argument, NULL, opt_fuzz_span },
    { "fuzz-gap",         required_argument, NULL, opt_fuzz_gap },
    { "keywords",         required_argument, NULL, 'k'},
    { NULL, 0, NULL, 0 }
};

/* Valid CVS substitution modes
   (http://cvsman.com/cvs-1.12.12/cvs_103.php#SEC103)
 */
static const char * keyword_modes[] = {
    "kv", "kv1", "k", "o", "b", "v"
};
static int keyword_mode_count = 6;

static unsigned long zlevel;
static const char * branch_prefix;
static const char * entries_name;
static const char * filter_command;
static const char * git_dir;
static const char * master = "master";
static const char * output_path;
static const char * remote = "";
static const char * tag_prefix;
static const char * version_cache_path;
static const char * keyword_mode;

static const char ** directory_list;
static const char ** directory_list_end;

static bool force;

static size_t mark_counter;
static size_t cached_marks;

// FIXME - assumes signed time_t!
#define TIME_MIN (sizeof (time_t) == sizeof (int) ? INT_MIN : LONG_MIN)
#define TIME_MAX (sizeof (time_t) == sizeof (int) ? INT_MAX : LONG_MAX)

static void print_fixups (FILE * out, const database_t * db,
                          version_t ** base_versions,
                          tag_t * tag, const changeset_t * cs,
                          cvs_connection_t * s);


static const char * format_date (const time_t * time, bool utc)
{
    struct tm dtm;
    static char date[32];
    size_t dl = 0;
    if (!utc)
        dl = strftime (date, sizeof date, "%F %T %Z", localtime_r (time, &dtm));
    if (dl == 0)
        // Maybe someone gave us a crap timezone?
        dl = strftime (date, sizeof date, "%F %T GMT", gmtime_r (time, &dtm));

    assert (dl != 0);
    return date;
}


static void read_version (FILE * out,
                          const database_t * db, cvs_connection_t * s)
{
    if (starts_with (s->line, "Removed ")) {
        // Removed line; we got the date a bit silly, just ignore it.
        next_line (s);
        return;
    }

    if (starts_with (s->line, "Checked-in ")) {
        // Update entry but no file change.  Hopefully this just means we
        // screwed up the dates; if servers start sending this back for
        // identical versions we might have to think again.
        next_line (s);
        next_line (s);
        return;
    }

    if (!starts_with (s->line, "Created ") &&
        !starts_with (s->line, "Update-existing ") &&
        !starts_with (s->line, "Updated "))
        fatal ("Did not get Update line: '%s'\n", s->line);

    const char * d = strchr (s->line, ' ') + 1;
    // Get the directory part of the path after the module name.
    if (strcmp (d, ".") == 0 || strcmp (d, "./") == 0)
        d = xstrdup ("");
    else {
        size_t len = strlen (d);
        if (d[len - 1] == '/')
            len -= 1;
        char * dd = xmalloc (len + 2);
        memcpy (dd, d, len);
        dd[len] = '/';
        dd[len + 1] = 0;
        d = dd;
    }

    next_line (s);                      // Skip the repo directory.

    next_line (s);
    if (s->line[0] != '/')
        fatal ("cvs checkout - doesn't look like entry line: '%s'", s->line);

    const char * slash1 = strchr (s->line + 1, '/');
    if (slash1 == NULL)
        fatal ("cvs checkout - doesn't look like entry line: '%s'", s->line);

    const char * slash2 = strchr (slash1 + 1, '/');
    if (slash2 == NULL)
        fatal ("cvs checkout - doesn't look like entry line: '%s'", s->line);

    const char * path = xasprintf ("%s%.*s", d,
                                   (int) (slash1 - s->line - 1), s->line + 1);
    const char * vers = xasprintf ("%.*s",
                                   (int) (slash2 - slash1 - 1), slash1 + 1);

    file_t * file = database_find_file (db, path);
    if (file == NULL)
        fatal ("cvs checkout - got unknown file %s\n", path);

    version_t * version = file_find_version (file, vers);
    if (version == NULL)
        fatal ("cvs checkout - got unknown file version %s %s\n", path, vers);

    next_line (s);
    if (!starts_with (s->line, "u="))
        fatal ("cvs checkout %s %s - got unexpected file mode '%s'\n",
               version->version, version->file->path, s->line);

    version->exec = (strchr (s->line, 'x') != NULL);

    next_line (s);
    char * tail;
    unsigned long len = strtoul (s->line, &tail, 10);
    if (len == ULONG_MAX || *tail != 0)
        fatal ("cvs checkout %s %s - got unexpected file length '%s'\n",
               version->version, version->file->path, s->line);

    if (version->mark == SIZE_MAX) {
        version->mark = ++mark_counter;
        fprintf (out, "blob\nmark :%zu\ndata %lu\n", version->mark, len);
        cvs_read_block (s, out, len);
        fprintf (out, "\n");
    }
    else {
        warning ("cvs checkout %s %s - version is duplicate\n", path, vers);
        cvs_read_block (s, NULL, len);
    }

    ++s->count_versions;

    xfree (d);
    xfree (path);
    xfree (vers);
}


static void read_versions (FILE * out,
                           const database_t * db, cvs_connection_t * s)
{
    ++s->count_transactions;
    while (1) {
        next_line (s);
        if (starts_with (s->line, "M ") || starts_with (s->line, "MT "))
            continue;

        if (strcmp (s->line, "ok") == 0)
            return;

        read_version (out, db, s);
    }
}


static void grab_version (FILE * out, const database_t * db,
                          cvs_connection_t * s, version_t * version)
{
    if (version == NULL || version->mark != SIZE_MAX)
        return;

    const char * path = version->file->path;
    const char * slash = strrchr (path, '/');
    // Make sure we have the directory.
    if (slash != NULL
        && (version->parent == NULL
            || version->parent->mark == SIZE_MAX
            || version->parent->mark <= cached_marks))
        cvs_printf (s, "Directory %s/%.*s\n" "%s%.*s\n",
                    s->module, (int) (slash - path), path,
                    s->prefix, (int) (slash - path), path);

    // Go to the main directory.
    cvs_printf (s,
                "Directory %s\n%.*s\n", s->module,
                (int) strlen (s->prefix) - 1, s->prefix);

    cvs_printff (s,
                 "Argument -k%s\n"
                 "Argument -r%s\n"
                 "Argument --\n"
                 "Argument %s\nupdate\n",
                 keyword_mode, version->version, version->file->path);

    read_versions (out, db, s);

    if (version->mark == SIZE_MAX)
        fatal ("cvs checkout - failed to get %s %s\n",
               version->file->path, version->version);
}


static void grab_by_option (FILE * out,
                            const database_t * db,
                            cvs_connection_t * s,
                            const char * r_arg,
                            const char * D_arg,
                            version_t ** fetch, version_t ** fetch_end)
{
    // Build an array of the paths that we're getting.  FIXME - if changeset
    // versions were sorted we wouldn't need this.
    const char ** paths = NULL;
    const char ** paths_end = NULL;

    for (version_t ** i = fetch; i != fetch_end; ++i) {
        version_t * v = version_live (*i);
        assert (v && v->used && v->mark == SIZE_MAX);
        ARRAY_APPEND (paths, v->file->path);
    }

    assert (paths != paths_end);

    ARRAY_SORT (paths, (int(*)(const void *, const void *)) strcmp);

    const char * d = NULL;
    ssize_t d_len = SSIZE_MAX;

    for (const char ** i = paths; i != paths_end; ++i) {
        const char * slash = strrchr (*i, '/');
        if (slash == NULL)
            continue;
        if (slash - *i == d_len && memcmp (*i, d, d_len) == 0)
            continue;
        // Tell the server about this directory.
        d = *i;
        d_len = slash - d;
        cvs_printf (s,
                    "Directory %s/%.*s\n"
                    "%s%.*s\n",
                    s->module, (int) d_len, d,
                    s->prefix, (int) d_len, d);
    }

    // Go to the main directory.
    cvs_printf (s,
                "Directory %s\n%.*s\n", s->module,
                (int) (strlen (s->prefix) - 1), s->prefix);

    // Update args:
    if (r_arg)
        cvs_printf (s, "Argument -r%s\n", r_arg);

    if (D_arg)
        cvs_printf (s, "Argument -D%s\n", D_arg);

    cvs_printf (s, "Argument -k%s\n" "Argument --\n", keyword_mode);

    for (const char ** i = paths; i != paths_end; ++i)
        cvs_printf (s, "Argument %s\n", *i);

    xfree (paths);

    cvs_printff (s, "update\n");

    read_versions (out, db, s);
}


static void grab_versions (FILE * out, const database_t * db,
                           cvs_connection_t * s,
                           version_t ** fetch, version_t ** fetch_end)
{
    if (fetch_end == fetch)
        return;

    if (fetch_end == fetch + 1) {
        grab_version (out, db, s, *fetch);
        return;
    }

    bool idver = true;
    for (version_t ** i = fetch + 1; i != fetch_end; ++i)
        if ((*i)->version != fetch[0]->version) {
            idver = false;
            break;
        }
    if (idver) {
        grab_by_option (out, db, s,
                        fetch[0]->version, NULL,
                        fetch, fetch_end);
        return;
    }

    time_t dmin = fetch[0]->time;
    time_t dmax = fetch[0]->time;
    for (version_t ** i = fetch + 1; i != fetch_end; ++i)
        if ((*i)->time < dmin)
            dmin = (*i)->time;
        else if ((*i)->time > dmax)
            dmax = (*i)->time;

    if (dmax - dmin < 300 && fetch[0]->branch && !fetch[0]->branch->dummy) {
        // Format the date.
        struct tm tm;
        gmtime_r (&dmax, &tm);
        char date[64];
        if (strftime (date, 64, "%d %b %Y %H:%M:%S -0000", &tm) == 0)
            fatal ("strftime failed\n");

        grab_by_option (out, db, s,
                        fetch[0]->branch->tag[0] ? fetch[0]->branch->tag : NULL,
                        format_date (&dmax, true),
                        fetch, fetch_end);

        for (version_t ** i = fetch; i != fetch_end; ++i)
            if ((*i)->mark == SIZE_MAX)
                fprintf (stderr, "Missed first time round: %s %s\n",
                         (*i)->file->path, (*i)->version);
    }

    for (version_t ** i = fetch; i != fetch_end; ++i)
        if ((*i)->mark == SIZE_MAX)
            grab_version (out, db, s, *i);
}


static bool same_directory (const char * A, const char * B)
{
    const char * sA = strrchr (A, '/');
    const char * sB = strrchr (B, '/');
    if (sA == NULL)
        return sB == NULL;
    return sB != NULL  &&  sA - A == sB - B  &&  memcmp (A, B, sA - A) == 0;
}


static int path_dirlen (const char * p)
{
    const char * s = strrchr (p, '/');
    if (s == NULL)
        return 0;
    else
        return s - p + 1;
}


static const char * path_filename (const char * p)
{
    const char * s = strrchr (p, '/');
    if (s == NULL)
        return p;
    else
        return s + 1;
}


static const char * output_entries_list (FILE * out,
                                         const database_t * db,
                                         version_t * const * vv,
                                         const file_t * f,
                                         const char * last_path)
{
    if (entries_name == NULL || *entries_name == 0)
        return last_path;

    if (last_path != NULL && same_directory (last_path, f->path))
        return last_path;

    // Find the range of files in the same directory.
    bool directory_is_live = false;
    const file_t * start = f;
    while (start != db->files && same_directory (start[-1].path, f->path)) {
        --start;
        directory_is_live = directory_is_live
            || version_live (vv[start - db->files]);
    }
    const file_t * end = f;
    do {
        directory_is_live = directory_is_live
            || version_live (vv[end - db->files]);
        ++end;
    }
    while (end != db->files_end && same_directory (end->path, f->path));

    if (!directory_is_live) {
        fprintf (out, "D %.*s%s\n",
                 path_dirlen (f->path), f->path, entries_name);
        return f->path;
    }
    fprintf (out, "M 644 inline %.*s%s\n",
             path_dirlen (f->path), f->path, entries_name);
    fprintf (out, "data <<EOF\n");
    for (const file_t * f = start; f != end; ++f)
        if (version_live (vv[f - db->files]))
            fprintf (out, "%s %s\n",
                     vv[f - db->files]->version, path_filename (f->path));
    fprintf (out, "EOF\n");
    return f->path;
}


static void print_commit (FILE * out, const database_t * db, changeset_t * cs,
                          cvs_connection_t * s)
{
    version_t * v = cs->versions[0];

    version_t ** fetch = NULL;
    version_t ** fetch_end = NULL;

    // Get the list of versions to fetch.
    for (version_t ** i = cs->versions; i != cs->versions_end; ++i) {
        if (!(*i)->used)
            continue;

        version_t * cv = version_live (*i);
        if (cv != NULL && cv->mark == SIZE_MAX)
            ARRAY_APPEND (fetch, cv);
    }

    fprintf (stderr, "%s COMMIT", format_date (&cs->time, false));

    // Get the versions.
    grab_versions (out, db, s, fetch, fetch_end);
    xfree (fetch);

    v->branch->last = cs;
    cs->mark = ++mark_counter;
    v->branch->changeset.mark = cs->mark;

	const char * author_name;
	const char * author_mail;

	struct author_list * a = lookup (v->author);
	if(a == NULL ) {
		author_name=v->author;
		author_mail=v->author;
	} else {
		author_name=a->author_name;
		author_mail=a->author_mail;
	}

    fprintf (out, "commit %s/%s\n",
             branch_prefix, *v->branch->tag ? v->branch->tag : master);
    fprintf (out, "mark :%zu\n", cs->mark);
    fprintf (out, "committer %s <%s> %ld +0000\n",
             author_name, author_mail, cs->time);
    fprintf (out, "data %zu\n%s\n", strlen (v->log), v->log);
    for (changeset_t ** i = cs->merge; i != cs->merge_end; ++i)
        if ((*i)->mark == 0)
            fprintf (stderr, "Whoops, out of order!\n");
        else if ((*i)->mark == mark_counter)
            fprintf (stderr, "Whoops, self-ref\n");
        else
            fprintf (out, "merge :%zu\n", (*i)->mark);

    const char * last_path = NULL;
    for (version_t ** i = cs->versions; i != cs->versions_end; ++i)
        if ((*i)->used) {
            version_t * vv = version_normalise (*i);
            if (vv->dead)
                fprintf (out, "D %s\n", vv->file->path);
            else
                fprintf (out, "M %s :%zu %s\n",
                         vv->exec ? "755" : "644", vv->mark, vv->file->path);
            last_path = output_entries_list (
                out, db, v->branch->branch_versions, vv->file, last_path);
        }

    fprintf (stderr, "\n");
}


static void print_tag (FILE * out, const database_t * db, tag_t * tag,
                       cvs_connection_t * s)
{
    fprintf (stderr, "%s %s %s\n",
             format_date (&tag->changeset.time, false),
             tag->branch_versions ? "BRANCH" : "TAG",
             tag->tag);

    tag_t * branch;
    if (tag->parent == NULL)
        branch = NULL;
    else if (tag->parent->type == ct_commit)
        branch = tag->parent->versions[0]->branch;
    else
        branch = as_tag (tag->parent);

    assert (tag->parent == NULL || (branch && branch->last == tag->parent));

    tag->last = &tag->changeset;

    create_fixups (db, branch ? branch->branch_versions : NULL, tag);

    // If the tag is a branch, then rewind the current versions to the parent
    // versions.  The fix-up commits will restore things.  FIXME - we should
    // just initialise the branch correctly!
    if (tag->branch_versions) {
        size_t bytes = sizeof branch->branch_versions[0]
            * (db->files_end - db->files);
        if (branch)
            memcpy (tag->branch_versions, branch->branch_versions, bytes);
        else
            memset (tag->branch_versions, 0, bytes);
    }

    if (tag->parent)
        tag->changeset.mark = tag->parent->mark;
    else
        tag->changeset.mark = 0;

    if (tag->deleted
        && (!tag->merge_source || tag->fixups == tag->fixups_end)) {
        assert (tag->branch_versions == NULL);
        return;
    }

    if (!tag->deleted) {
        fprintf (out, "reset %s/%s\n",
                 tag->branch_versions ? branch_prefix : tag_prefix,
                 *tag->tag ? tag->tag : master);
        if (tag->changeset.mark != 0)
            fprintf (out, "from :%zu\n", tag->changeset.mark);
    }

    if (tag->branch_versions == NULL)
        // For a tag, just force out all the fixups immediately.
        print_fixups (out, db, branch ? branch->branch_versions : NULL,
                      tag, NULL, s);
}


/// Output the fixups that must be done before the given time.  If none, then no
/// commit is created.
void print_fixups (FILE * out, const database_t * db,
                   version_t ** base_versions,
                   tag_t * tag, const changeset_t * cs,
                   cvs_connection_t * s)
{
    fixup_ver_t * fixups;
    fixup_ver_t * fixups_end;

    fixup_list (&fixups, &fixups_end, tag, cs);

    if (fixups == fixups_end)
        return;

    // base_versions should only be NULL for starting the trunk.  But that
    // should never need fixups.
    assert (base_versions != NULL);

    // If we're doing fixups for a branch, then the base_versions should be the
    // branch version.
    assert (tag->branch_versions == NULL
            || base_versions == tag->branch_versions);

    version_t ** fetch = NULL;
    version_t ** fetch_end = NULL;
    for (fixup_ver_t * ffv = fixups; ffv != fixups_end; ++ffv)
        if (ffv->version != NULL && !ffv->version->dead
            && ffv->version->mark == SIZE_MAX)
            ARRAY_APPEND (fetch, ffv->version);

    // FIXME - grab_versions assumes that all versions are on the same branch!
    // We should pass in the tag rather than guessing it!
    grab_versions (out, db, s, fetch, fetch_end);
    xfree (fetch);

    tag->fixup = true;
    size_t from = tag->changeset.mark;
    tag->changeset.mark = ++mark_counter;

    if (tag->deleted)
        fprintf (out, "commit _crap_zombie\n");
    else
        fprintf (out, "commit %s/%s\n",
                 tag->branch_versions ? branch_prefix : tag_prefix,
                 *tag->tag ? tag->tag : master);

    fprintf (out, "mark :%zu\n", tag->changeset.mark);

    fprintf (out, "committer crap <crap> %ld +0000\n",
             tag->branch_versions && tag->last
             ? tag->last->time : tag->changeset.time);
    const char * comment = fixup_commit_comment (
        db, base_versions, fixups, fixups_end);
    fprintf (out, "data %zu\n%s", strlen (comment), comment);
    xfree (comment);
    if (tag->deleted)
        fprintf (out, "from :%zu\n", from);

    // We need a list of versions for updating the entries files.  If we are
    // working on a branch, then we need to update that anyway.  Else take a
    // temporary list.
    version_t ** updated_versions = tag->branch_versions;
    if (updated_versions == NULL) {
        size_t bytes = (db->files_end - db->files) * sizeof (version_t *);
        updated_versions = xmalloc (bytes);
        memcpy (updated_versions, base_versions, bytes);
    }

    for (fixup_ver_t * ffv = fixups; ffv != fixups_end; ++ffv) {
        int i = ffv->file - db->files;
        version_t * tv = ffv->version;
        assert (tv != version_live (updated_versions[i]));
        updated_versions[i] = tv;
    }

    const char * last_path = NULL;
    for (fixup_ver_t * ffv = fixups; ffv != fixups_end; ++ffv) {
        version_t * tv = ffv->version;

        if (tv == NULL)
            fprintf (out, "D %s\n", ffv->file->path);
        else
            fprintf (out, "M %s :%zu %s\n",
                     tv->exec ? "755" : "644", tv->mark, tv->file->path);

        last_path = output_entries_list (
            out, db, updated_versions, ffv->file, last_path);
    }

    if (tag->branch_versions == NULL)
        xfree (updated_versions);

    xfree (fixups);
}


/// Read in our version-sha file and generate marks.
static void initial_process_marks (const database_t * db)
{
    const char * crap_dir = xasprintf ("%s/crap", git_dir);
    // Ignore errors; we only care if we can end up using the directory.
    (void) mkdir (crap_dir, 0777);
    xfree (crap_dir);

    const char * marks_path = xasprintf (
        "%s/crap/marks%s%s.txt", git_dir, *remote ? "." : "", remote);
    FILE * output_marks = fopen (marks_path, "w");
    xfree (marks_path);
    if (output_marks == NULL)
        fatal ("opening marks file failed: %s\n", strerror (errno));

    FILE * cache = fopen (version_cache_path, "r");
    if (cache == NULL) {
        warning ("opening %s failed: %s\n", version_cache_path,
                 strerror (errno));
        fclose (output_marks);
        return;
    }

    // FIXME - error handling.
    char * line = NULL;
    size_t line_max = 0;

    while (true) {
        uint32_t sha[5];
        char mode;
        if (fscanf (cache, "%8x%8x%8x%8x%8x %c",
                    &sha[0], &sha[1], &sha[2], &sha[3], &sha[4], &mode) < 6)
            break;

        if (mode != '-' && mode != 'x')
            break;

        ssize_t ll = getline (&line, &line_max, cache);
        if (ll <= 0)
            break;

        if (line[ll - 1] == '\n')
            line[ll - 1] = 0;

        char * ver = line;
        if (*ver == ' ')
            ++ver;

        char * path = strchr (ver, ' ');
        if (path == NULL)
            continue;

        *path++ = 0;

        // Attempt to find the path and version.
        const file_t * f = database_find_file (db, path);
        if (!f)
            continue;

        version_t * v = file_find_version (f, ver);
        if (v) {
            v->mark = ++mark_counter;
            v->exec = mode == 'x';
            fprintf (output_marks, ":%zu %08x%08x%08x%08x%08x\n",
                     mark_counter, sha[0], sha[1], sha[2], sha[3], sha[4]);
        }
    }

    cached_marks = mark_counter;

    xfree (line);

    fclose (cache);
    fclose (output_marks);
}


/// Read in the marks file written by git-fast-import, and write out a file
/// containing the id's in a form that is useful for us to re-read.
static void final_process_marks (const database_t * db)
{
    const char * marks_path = xasprintf ("%s/crap/marks%s%s.txt", git_dir,
                                         *remote ? "." : "", remote);
    FILE * marks = fopen (marks_path, "r");
    xfree (marks_path);
    if (marks == NULL) {
        warning ("open crap/marks.txt failed: %s\n", strerror (errno));
        return;
    }
    assert (sizeof (uint32_t) == 4);
    assert (mark_counter < LONG_MAX / 20);
    uint32_t * shas = xcalloc (mark_counter * 20 + 20);

    while (true) {
        size_t mark;
        uint32_t sha[5];
        if (fscanf (marks, ":%zu %8x%8x%8x%8x%8x\n",
                    &mark, &sha[0], &sha[1], &sha[2], &sha[3], &sha[4]) < 6)
            break;

        if (mark <= mark_counter)
            memcpy (shas + mark * 5, sha, 20);
    }

    fclose (marks);

    // FIXME - bounce via temporary.
    marks = fopen (version_cache_path, "w");
    if (marks == NULL) {
        free (shas);
        warning ("opening %s failed: %s\n",
                 version_cache_path, strerror (errno));
        return;
    }

    for (const file_t * f = db->files; f != db->files_end; ++f)
        for (const version_t * v = f->versions; v != f->versions_end; ++v) {
            if (v->mark > mark_counter)
                continue;
            const uint32_t * p = shas + 5 * v->mark;
            if (p[0] | p[1] | p[2] | p[3] | p[4])
                fprintf (marks, "%08x%08x%08x%08x%08x %c %s %s\n",
                         p[0], p[1], p[2], p[3], p[4],
                         v->exec ? 'x' : '-', v->version, f->path);
        }

    // FIXME - check errors on write...
    fclose (marks);

    free (shas);
}


static void usage (const char * prog, FILE * stream, int code)
    __attribute__ ((noreturn));
static void usage (const char * prog, FILE * stream, int code)
{
    fprintf (stream, "Usage: %s [options] <ROOT> <MODULE>\n\
  -z, --compress=[0-9]   Compress the CVS network traffic.\n\
  -h, --help             This message.\n\
  -o, --output=FILE      Send output to a file instead of git-fast-import.\n\
                         If FILE starts with '|' then pipe to a command.\n\
  -F, --filter=COMMAND   Use COMMAND as a filter on the version/branch/tag\n\
                         information, to detect merges etc.\n\
  -f, --force            Pass --force to git-fast-import.\n\
  -e, --entries=NAME     Add a file listing the CVS versions to each directory\n\
                         in the git repository.\n\
  -m, --master=NAME      Use branch NAME for the cvs trunk instead of 'master'.\n\
  -r, --remote=NAME      Import to remote NAME; implies appropriate -b, -t\n\
                         and -c.\n\
  -c, --version-cache=PATH     File path for version cache.\n\
  -b, --branch-prefix=PREFIX   Place branches in PREFIX instead of 'refs/heads'.\n\
  -t, --tag-prefix=PREFIX      Place tags in PREFIX instead of 'refs/tags'.\n\
  -d, --directory=PATH   Limit the clone to certain paths within the CVS MODULE.\n\
                         This option may be given multiple times, and despite\n\
                         the name, works for file.\n\
      --fuzz-span=SECONDS The maximum time between the first and last commits of\n\
                         a changeset (default 300 seconds).\n\
      --fuzz-gap=SECONDS The maximum time between two consecutive commits of a\n\
                         changeset (default 300 seconds).\n\
      --keywords=MODE    The CVS substitution mode to use (default: 'kk')\n\
  -A, --author-conv-file=FILE  CVS by default uses the Unix username when\n\
                               writing its commit logs. Using this option and\n\
                               an author-conv-file maps the name recorded in\n\
                               CVS to author name and e-mail:\n\
https://git-scm.com/docs/git-cvsimport#Documentation/git-cvsimport.txt--Altauthor-conv-filegt\n\
  <ROOT>                 The CVS repository to access.\n\
  <MODULE>               The relative path within the CVS repository.\n",
             prog);

    exit (code);
}

char* rtrim(char* string, char junk)
{
    char* original = string + strlen(string);
    while(*--original == junk);
    *(original + 1) = '\0';
    return string;
}

static void process_opts (int argc, char * const argv[])
{
    while (1)
        switch (getopt_long (argc, argv,
                             "A:b:c:d:e:F:fhz:m:o:r:t:k:", opts, NULL)) {
        case 'A': {
			FILE * fid=fopen(optarg, "r");
			
			char c[1000];
  
			char author_cvs[1000];
			char author_name[1000];
			char author_mail[1000];
  
			if(fid==NULL) {
				printf("Could not open author file: %s\n", optarg);
				abort();
			}

			// read as many line as you can
			while (fscanf(fid,"%[^=]=%[^<]<%[^>]>%[^\n]\n", author_cvs, author_name, author_mail, c) != EOF)
			{
				printf("Mapping author:\n%s\n%s\n%s\n", author_cvs, author_name, author_mail);
				install (rtrim(author_cvs, ' '), rtrim(author_name, ' '), rtrim(author_mail, ' '));
			}
			fclose(fid);
			
           	break;
		}
        case 'b':
            branch_prefix = optarg;
            break;
        case 'c':
            version_cache_path = optarg;
            break;
        case 'd':
            ARRAY_APPEND(directory_list, optarg);
            break;
        case 'e':
            entries_name = optarg;
            break;
        case 'F':
            filter_command = optarg;
            break;
        case 'f':
            force = true;
            break;
        case 'o':
            output_path = optarg;
            break;
        case 'm':
            master = optarg;
            break;
        case 'r':
            remote = optarg;
            break;
        case 't':
            tag_prefix = optarg;
            break;
        case 'z':
            zlevel = strtoul (optarg, NULL, 10);
            if (zlevel > 9)
                usage (argv[0], stderr, EXIT_FAILURE);
            break;
        case 'h':
            usage (argv[0], stdout, EXIT_SUCCESS);
        case '?':
            usage (argv[0], stderr, EXIT_FAILURE);
        case opt_fuzz_span:
            fuzz_span = strtoul (optarg, NULL, 10);
            break;
        case opt_fuzz_gap:
            fuzz_gap = strtoul (optarg, NULL, 10);
            break;
        case -1:
            return;
        case 'k':
            keyword_mode = optarg;
            break;
        default:
            abort();
        }
}


int main (int argc, char * const argv[])
{
    // Make sure stdin/stdout/stderr are valid FDs.
    int f;
    do {
        f = open ("/dev/null", O_RDWR);
        if (f < 0)
            fatal ("open /dev/null failed: %s\n", strerror (errno));
    }
    while (f < 2);

    if (f > 2)
        close (f);

    process_opts (argc, argv);
    if (argc != optind + 2)
        usage (argv[0], stderr, EXIT_FAILURE);

    if (branch_prefix == NULL) {
        if (*remote)
            branch_prefix = cache_stringf ("refs/remotes/%s", remote);
        else
            branch_prefix = "refs/heads";
    }

    if (tag_prefix == NULL) {
        if (*remote)
            tag_prefix = cache_stringf ("refs/remotes/tags/%s", remote);
        else
            tag_prefix = "refs/tags";
    }

    int i;
    if (keyword_mode == NULL) {
        keyword_mode = "k";
    }
    for (i=0; i<keyword_mode_count; i++) {
        if(strcmp(keyword_mode, keyword_modes[i]) == 0) break;
    }
    if (i == keyword_mode_count) {
        fatal("%s is not a valid CVS substitution mode\n", keyword_mode);
    }

    // Set up git_dir.
    {
        pipeline * git_dir_pl = pipeline_new_command_args (
            "git", "rev-parse", "--git-dir", NULL);
        pipeline_want_infile (git_dir_pl, "/dev/null");
        pipeline_want_out (git_dir_pl, -1);
        pipeline_start (git_dir_pl);
        size_t len = 4096;
        const char * data = pipeline_read (git_dir_pl, &len);
        if (len > 0 && data[len - 1] == '\n')
            --len;
        git_dir = cache_string_n (data, len);
        if (pipeline_wait (git_dir_pl) != 0)
            exit (EXIT_FAILURE);
        pipeline_free (git_dir_pl);
    }

    if (version_cache_path == NULL)
        version_cache_path = cache_stringf (
            "%s/crap/version-cache%s%s.txt",
            git_dir, *remote ? "." : "", remote);

    cvs_connection_t stream;
    connect_to_cvs (&stream, argv[optind]);

    if (zlevel != 0)
        cvs_connection_compress (&stream, zlevel);

    stream.module = xstrdup (argv[optind + 1]);
    int root_len = strlen(stream.remote_root);
    while (root_len > 0 && stream.remote_root[root_len - 1] == '/')
        --root_len;
    stream.prefix = xasprintf ("%.*s/%s/", root_len, stream.remote_root,
                               stream.module);

    cvs_printff (&stream,
                 "Global_option -q\n"
                 "Argument --\n");
    if (directory_list == directory_list_end)
        cvs_printff (&stream, "Argument %s\n", stream.module);
    else
        for (const char ** i = directory_list; i != directory_list_end; ++i)
            cvs_printff (&stream, "Argument %s/%s\n", stream.module, *i);
    cvs_printff (&stream, "rlog\n");

    database_t db;

    read_files_versions (&db, &stream);

    create_changesets (&db);

    branch_analyse (&db);

    // Prepare for the ultimate changeset emission.  This time the tags go
    // through the the usual emission process, and branches block revisions on
    // the branch.

    for (tag_t * i = db.tags; i != db.tags_end; ++i)
        for (changeset_t ** j = i->changeset.children;
             j != i->changeset.children_end; ++j)
            ++(*j)->unready_count;

    // Re-do the version->changeset unready counts.
    prepare_for_emission (&db, NULL);

    // Mark the initial tags as ready to emit, and fill in branches with their
    // initial versions.
    for (tag_t * i = db.tags; i != db.tags_end; ++i) {
        if (i->changeset.unready_count == 0)
            heap_insert (&db.ready_changesets, &i->changeset);
        if (i->branch_versions) {
            memset (i->branch_versions, 0,
                    sizeof (version_t *) * (db.files_end - db.files));
            for (version_t ** j = i->tag_files; j != i->tag_files_end; ++j)
                i->branch_versions[(*j)->file - db.files] = *j;
        }
    }

    // Now do the changeset emission that creates the ultimate changeset order.
    // FIXME - it would be better to store the order for each branch separately,
    // so that filter_output can re-order between branches.
    changeset_t ** serial = NULL;
    changeset_t ** serial_end = NULL;
    for (changeset_t * changeset; (changeset = next_changeset (&db)); ) {
        ARRAY_APPEND (serial, changeset);
        if (changeset->type == ct_commit)
            // FIXME - account for fixups?
            changeset_update_branch_versions (&db, changeset);
        changeset_emitted (&db, NULL, changeset);
    }

    if (filter_command != NULL)
        filter_changesets (&db, serial, serial_end, filter_command);

    // Reset all branches to their initial versions.
    for (tag_t * i = db.tags; i != db.tags_end; ++i) {
        i->is_released = false;
        if (i->branch_versions) {
            memset (i->branch_versions, 0,
                    sizeof (version_t *) * (db.files_end - db.files));
            for (version_t ** j = i->tag_files; j != i->tag_files_end; ++j)
                i->branch_versions[(*j)->file - db.files] = *j;
        }
    }

    // Read in any cached version sha's.
    initial_process_marks (&db);

    // Start the output to git-fast-import.
    pipeline * pipeline = NULL;
    FILE * out;
    if (output_path == NULL) {
        pipecmd * cmd = pipecmd_new_args ("git", "fast-import", NULL);
        pipecmd_argf (cmd, "--import-marks=%s/crap/marks%s%s.txt",
                      git_dir, *remote ? "." : "", remote);
        pipecmd_argf (cmd, "--export-marks=%s/crap/marks%s%s.txt",
                      git_dir, *remote ? "." : "", remote);
        if (force)
            pipecmd_arg (cmd, "--force");
        pipeline = pipeline_new_commands (cmd, NULL);
        pipeline_want_in (pipeline, -1);
        pipeline_start (pipeline);
        out = pipeline_get_infile (pipeline);
    }
    else if (output_path[0] == '|') {
        pipeline = pipeline_new();
        pipeline_command_argstr (pipeline, output_path + 1);
        pipeline_want_in (pipeline, -1);
        pipeline_start (pipeline);
        out = pipeline_get_infile (pipeline);
    }
    else {
        out = fopen (output_path, "we");
        if (out == NULL)
            fatal ("open %s failed: %s\n", output_path, strerror (errno));
    }

    fprintf (out, "feature done\n");

    // Output the changesets to git-filter-branch.
    ssize_t emitted_commits = 0;
    for (changeset_t ** p = serial; p != serial_end; ++p) {
        changeset_t * changeset = *p;
        if (changeset->type == ct_tag) {
            tag_t * tag = as_tag (changeset);
            tag->is_released = true;
            print_tag (out, &db, tag, &stream);
            continue;
        }

        ++emitted_commits;

        // Before doing the commit proper, output any branch-fixups that need
        // doing.
        tag_t * branch = changeset->versions[0]->branch;
        print_fixups (out, &db, branch->branch_versions, branch,
                      changeset, &stream);

        bool live = false;
        for (version_t ** i = changeset->versions;
             i != changeset->versions_end; ++i)
            if ((*i)->used) {
                version_t ** bv
                    = &branch->branch_versions[(*i)->file - db.files];
                if (version_live (*bv) != version_live (*i))
                    live = true;
                // Keep dead versions, like we do elsewhere...
                *bv = *i;
            }

        if (live) {
            print_commit (out, &db, changeset, &stream);
        }
        else {
            changeset->mark = branch->last->mark;
            branch->last = changeset;
        }
    }
    free (serial);

    // Final fixups.
    for (tag_t * i = db.tags; i != db.tags_end; ++i)
        if (i->branch_versions)
            print_fixups (out, &db, i->branch_versions, i, NULL, &stream);

    fprintf (stderr,
             "Emitted %zu commits (%s total %zu).\n",
             emitted_commits,
             emitted_commits == db.changesets_end - db.changesets ? "=" : "!=",
             db.changesets_end - db.changesets);

    size_t exact_branches = 0;
    size_t fixup_branches = 0;
    size_t exact_tags = 0;
    size_t fixup_tags = 0;
    bool deleted_fixup = false;
    for (tag_t * i = db.tags; i != db.tags_end; ++i) {
        assert (i->is_released);
        if (i->fixup) {
            if (i->deleted)
                deleted_fixup = true;
            if (i->branch_versions)
                ++fixup_branches;
            else
                ++fixup_tags;
        }
        else
            if (i->branch_versions)
                ++exact_branches;
            else
                ++exact_tags;
    }

    fprintf (stderr,
             "Exact %5zu + %5zu = %5zu branches + tags.\n"
             "Fixup %5zu + %5zu = %5zu branches + tags.\n",
             exact_branches, exact_tags, exact_branches + exact_tags,
             fixup_branches, fixup_tags, fixup_branches + fixup_tags);

    fprintf (stderr,
             "Download %lu cvs versions in %lu transactions.\n",
             stream.count_versions, stream.count_transactions);

    string_cache_stats (stderr);

    fprintf (out, "done\n");
    fflush (out);
    if (ferror (out))
        fatal ("Writing output failed.\n");
    if (pipeline != NULL) {
        int status = pipeline_wait (pipeline);
        if (status != 0)
            fatal ("Import command exited with %i.\n", status);
        pipeline_free (pipeline);
        final_process_marks (&db);
    }
    else {
        fclose (out);
    }

    if (deleted_fixup) {
        int ret = pipeline_run (
                pipeline_new_command_args (
                    "git", "update-ref", "-d", "_crap_zombie", NULL));
        if (ret != 0)
            fatal ("Deleting dummy ref failed: %i\n", ret);
    }

    cvs_connection_destroy (&stream);

    database_destroy (&db);
    string_cache_destroy();

    return 0;
}
