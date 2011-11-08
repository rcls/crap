#include "cvs_connection.h"
#include "branch.h"
#include "changeset.h"
#include "database.h"
#include "emission.h"
#include "file.h"
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
#include <time.h>

static struct option opts[] = {
    { "compress", required_argument, NULL, 'z' },
    { "help", required_argument, NULL, 'h' },
    { "output", required_argument, NULL, 'o' },
    { "entries", required_argument, NULL, 'e' },
    { NULL, 0, NULL, 0 }
};

static unsigned long zlevel = 0;
static const char * output_path = "|git fast-import";
static const char * entries_name = NULL;

static long mark_counter;

// FIXME - assumes signed time_t!
#define TIME_MIN (sizeof(time_t) == sizeof(int) ? INT_MIN : LONG_MIN)
#define TIME_MAX (sizeof(time_t) == sizeof(int) ? INT_MAX : LONG_MAX)

static void print_fixups(FILE * out, const database_t * db,
                         version_t ** base_versions,
                         tag_t * tag, const changeset_t * cs,
                         cvs_connection_t * s);


static const char * format_date (const time_t * time)
{
    struct tm dtm;
    static char date[32];
    size_t dl = strftime (date, sizeof date, "%F %T %Z",
                          localtime_r (time, &dtm));
    if (dl == 0)
        // Maybe someone gave us a crap timezone?
        dl = strftime (date, sizeof date, "%F %T %Z", gmtime_r (time, &dtm));

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
        && (version->parent == NULL || version->parent->mark == SIZE_MAX))
        cvs_printf (s, "Directory %s/%.*s\n" "%s%.*s\n",
                    s->module, (int) (slash - path), path,
                    s->prefix, (int) (slash - path), path);

    // Go to the main directory.
    cvs_printf (s,
                "Directory %s\n%.*s\n", s->module,
                (int) strlen (s->prefix) - 1, s->prefix);

    cvs_printff (s,
                 "Argument -kk\n"
                 "Argument -r%s\n"
                 "Argument --\n"
                 "Argument %s\nupdate\n",
                 version->version, version->file->path);

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
    size_t d_len = SIZE_MAX;

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

    cvs_printf (s, "Argument -kk\n" "Argument --\n");

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
                        format_date (&dmax),
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
    const char * sA = strrchr(A, '/');
    const char * sB = strrchr(B, '/');
    if (sA == NULL)
        return sB == NULL;
    return sB != NULL  &&  sA - A == sB - B  &&  memcmp (A, B, sA - A) == 0;
}


static int path_dirlen (const char * p)
{
    const char * s = strrchr(p, '/');
    if (s == NULL)
        return 0;
    else
        return s - p + 1;
}


static const char * path_filename (const char * p)
{
    const char * s = strrchr(p, '/');
    if (s == NULL)
        return p;
    else
        return s + 1;
}


static const char * output_entries_list (FILE * out,
                                         const database_t * db,
                                         const version_t * v,
                                         const char * last_path)
{
    if (entries_name == NULL || *entries_name == 0)
        return last_path;

    if (last_path != NULL && same_directory (last_path, v->file->path))
        return last_path;

    last_path = v->file->path;
    // FIXME - slow!!!
    bool directory_is_live = false;
    for (const file_t * f = db->files; f != db->files_end; ++f)
        if (same_directory (f->path, last_path)
            && version_live (v->branch->branch_versions[f - db->files])) {
            directory_is_live = true;
            break;
        }
    if (!directory_is_live) {
        fprintf (out, "D %.*s%s\n",
                 path_dirlen (last_path), last_path, entries_name);
        return last_path;
    }
    fprintf (out, "M 644 inline %.*s%s\n",
             path_dirlen (last_path), last_path, entries_name);
    fprintf (out, "data <<EOF\n");
    for (const file_t * f = db->files; f != db->files_end; ++f)
        if (same_directory (f->path, last_path)
            && version_live (v->branch->branch_versions[f - db->files]))
            fprintf (
                out, "%s %s\n",
                v->branch->branch_versions[f - db->files]->version,
                path_filename(f->path));
    fprintf (out, "EOF\n");
    return last_path;
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

    fprintf (stderr, "%s COMMIT", format_date (&cs->time));

    // Get the versions.
    grab_versions (out, db, s, fetch, fetch_end);
    xfree (fetch);

    v->branch->last = cs;
    cs->mark = ++mark_counter;

    fprintf (out, "commit refs/heads/%s\n",
             *v->branch->tag ? v->branch->tag : "cvs_master");
    fprintf (out, "mark :%lu\n", cs->mark);
    fprintf (out, "committer %s <%s> %ld +0000\n",
             v->author, v->author, cs->time);
    fprintf (out, "data %zu\n%s\n", strlen (v->log), v->log);

    const char * last_path = NULL;
    for (version_t ** i = cs->versions; i != cs->versions_end; ++i)
        if ((*i)->used) {
            version_t * vv = version_normalise (*i);
            if (vv->dead)
                fprintf (out, "D %s\n", vv->file->path);
            else
                fprintf (out, "M %s :%zu %s\n",
                         vv->exec ? "755" : "644", vv->mark, vv->file->path);
            last_path = output_entries_list (out, db, vv, last_path);
        }

    fprintf (stderr, "\n");
}


static void print_tag (FILE * out, const database_t * db, tag_t * tag,
                       cvs_connection_t * s)
{
    fprintf (stderr, "%s %s %s\n",
             format_date (&tag->changeset.time),
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

    fprintf (out, "reset refs/%s/%s\n",
             tag->branch_versions ? "heads" : "tags",
             *tag->tag ? tag->tag : "cvs_master");

    if (tag->parent)
        tag->changeset.mark = tag->parent->mark;
    else
        tag->changeset.mark = 0;

    if (tag->changeset.mark != 0)
        fprintf (out, "from :%lu\n\n", tag->changeset.mark);

    tag->last = &tag->changeset;

    create_fixups (db, branch ? branch->branch_versions : NULL, tag);

    // If the tag is a branch, then rewind the current versions to the parent
    // versions.  The fix-up commits will restore things.  FIXME - we should
    // just initialise the branch correctly!
    if (tag->branch_versions) {
        size_t bytes = sizeof (branch->branch_versions[0])
            * (db->files_end - db->files);
        if (branch)
            memcpy (tag->branch_versions, branch->branch_versions, bytes);
        else
            memset (tag->branch_versions, 0, bytes);
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
    tag->changeset.mark = ++mark_counter;

    fprintf (out, "commit refs/%s/%s\n",
             tag->branch_versions ? "heads" : "tags",
             *tag->tag ? tag->tag : "cvs_master");
    fprintf (out, "mark :%lu\n", tag->changeset.mark);

    fprintf (out, "committer crap <crap> %ld +0000\n",
             tag->branch_versions && tag->last
             ? tag->last->time : tag->changeset.time);
    const char * comment = fixup_commit_comment (
        db, base_versions, tag, fixups, fixups_end);
    fprintf (out, "data %zu\n%s", strlen (comment), comment);
    xfree (comment);

    for (fixup_ver_t * ffv = fixups; ffv != fixups_end; ++ffv) {
        int i = ffv->file - db->files;
        version_t * bv = base_versions ? version_live (base_versions[i]) : NULL;
        version_t * tv = ffv->version;

        assert (tv != bv);
        if (tv != bv) {
            if (tv == NULL)
                fprintf (out, "D %s\n", bv->file->path);
            else
                fprintf (out, "M %s :%zu %s\n",
                         tv->exec ? "755" : "644", tv->mark, tv->file->path);
        }

        if (tag->branch_versions)
            tag->branch_versions[i] = tv;
    }

    xfree (fixups);
}


static void usage (const char * prog, FILE * stream, int code)
    __attribute__ ((noreturn));
static void usage (const char * prog, FILE * stream, int code)
{
    fprintf (stream,
             "Usage: %s [-z <0--9>] [-o <PATH>] <ROOT> <REPO>\n", prog);
    exit (code);
}


void process_opts (int argc, char * const argv[])
{
    int longindex;
    while (1)
        switch (getopt_long (argc, argv, "e:hz:o:", opts, &longindex)) {
        case 'e':
            entries_name = optarg;
            break;
        case 'o':
            output_path = optarg;
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
        case -1:
            return;
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
            fatal("open /dev/null failed: %s\n", strerror(errno));
    }
    while (f < 2);

    if (f > 2)
        close (f);

    process_opts (argc, argv);
    if (argc != optind + 2)
        usage (argv[0], stderr, EXIT_FAILURE);

    // Start the output to git-fast-import.
    pipeline * pipeline = NULL;
    FILE * out;
    if (output_path[0] == '|') {
        pipeline = pipeline_new();
        pipeline_command_argstr(pipeline, output_path + 1);
        pipeline_want_in(pipeline, -1);
        pipeline_start(pipeline);
        out = pipeline_get_infile(pipeline);
    }
    else {
        out = fopen(output_path, "w");
        if (out == NULL)
            fatal("open %s failed: %s\n", output_path, strerror(errno));
    }

    fprintf(out, "feature done\n");

    cvs_connection_t stream;
    connect_to_cvs (&stream, argv[optind]);

    if (zlevel != 0)
        cvs_connection_compress (&stream, zlevel);

    stream.module = xstrdup (argv[optind + 1]);
    stream.prefix = xasprintf ("%s/%s/", stream.remote_root, stream.module);

    cvs_printff (&stream,
                 "Global_option -q\n"
                 "Argument --\n"
                 "Argument %s\n"
                 "rlog\n", stream.module);

    database_t db;

    read_files_versions (&db, &stream);

    create_changesets (&db);

    branch_analyse (&db);

    // Prepare for the real changeset emission.  This time the tags go through
    // the the usual emission process, and branches block revisions on the
    // branch.

    for (tag_t * i = db.tags; i != db.tags_end; ++i) {
        i->is_released = false;
        for (changeset_t ** j = i->changeset.children;
             j != i->changeset.children_end; ++j)
            ++(*j)->unready_count;
    }

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

    // Emit the changesets for real.
    size_t emitted_commits = 0;
    changeset_t * changeset;
    while ((changeset = next_changeset (&db))) {
        if (changeset->type == ct_commit) {
            ++emitted_commits;

            // Before doing the commit proper, output any branch-fixups that
            // need doing.
            version_t * v = changeset->versions[0];
            print_fixups (out, &db, v->branch->branch_versions, v->branch,
                          changeset, &stream);
            if (changeset_update_branch_versions (&db, changeset) != 0)
                print_commit (out, &db, changeset, &stream);
            else {
                changeset->mark = v->branch->last->mark;
                v->branch->last = changeset;
            }
        }
        else {
            tag_t * tag = as_tag (changeset);
            tag->is_released = true;
            print_tag (out, &db, tag, &stream);
        }

        changeset_emitted (&db, NULL, changeset);
    }

    // Final fixups.
    for (tag_t * i = db.tags; i != db.tags_end; ++i)
        if (i->branch_versions)
            print_fixups (out, &db, i->branch_versions, i, NULL, &stream);

    fflush (NULL);
    fprintf (stderr,
             "Emitted %zu commits (%s total %zu).\n",
             emitted_commits,
             emitted_commits == db.changesets_end - db.changesets ? "=" : "!=",
             db.changesets_end - db.changesets);

    size_t exact_branches = 0;
    size_t fixup_branches = 0;
    size_t exact_tags = 0;
    size_t fixup_tags = 0;
    for (tag_t * i = db.tags; i != db.tags_end; ++i) {
        assert (i->is_released);
        if (i->branch_versions)
            if (i->fixup)
                ++fixup_branches;
            else
                ++exact_branches;
        else
            if (i->fixup)
                ++fixup_tags;
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
    fflush(out);
    if (ferror(out))
        fatal("Writing output failed.\n");
    if (pipeline != NULL) {
        int status = pipeline_wait(pipeline);
        if (status != 0)
            fatal("Import command exited with %i.\n", status);
        pipeline_free(pipeline);
    }
    else {
        fclose (out);
    }

    cvs_connection_destroy (&stream);

    database_destroy (&db);
    string_cache_destroy();

    return 0;
}
