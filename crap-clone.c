#include "cvs_connection.h"
#include "branch.h"
#include "changeset.h"
#include "database.h"
#include "emission.h"
#include "file.h"
#include "log.h"
#include "log_parse.h"
#include "string_cache.h"
#include "utils.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static long mark_counter;

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


static const char * file_error (FILE * f)
{
    return ferror (f) ? strerror (errno) : (feof (f) ? "EOF" : "unknown");
}


static void read_version (const database_t * db, cvs_connection_t * s)
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
                                   slash1 - s->line - 1, s->line + 1);
    const char * vers = xasprintf ("%.*s", slash2 - slash1 - 1, slash1 + 1);

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

    bool go = version->mark == SIZE_MAX;
    if (go) {
        version->mark = ++mark_counter;
        printf ("blob\nmark :%zu\ndata %lu\n", version->mark, len);
    }
    else
        fprintf (stderr, "cvs checkout %s %s - version is duplicate\n",
                 path, vers);

    // Process the content.
    for (size_t done = 0; done != len; ) {
        char buffer[4096];
        size_t get = len - done;
        if (get > 4096)
            get = 4096;
        size_t got = fread (&buffer, 1, get, s->stream);
        if (got == 0)
            fatal ("cvs checkout %s %s - %s\n", path, vers,
                   file_error (s->stream));
        if (go) {
            size_t put = fwrite (&buffer, got, 1, stdout);
            if (put != 1)
                fatal ("git import %s %s - interrupted: %s\n",
                       version->version, version->file->path,
                       ferror (stdout) ? strerror (errno) : (
                           feof (stdout) ? "closed" : "unknown"));
        }

        done += got;
    }

    cvs_record_read (s, len);

    if (go)
        printf ("\n");

    xfree (d);
    xfree (path);
    xfree (vers);        
}


static void read_versions (const database_t * db, cvs_connection_t * s)
{
    while (1) {
        next_line (s);
        if (starts_with (s->line, "M ") || starts_with (s->line, "MT "))
            continue;

        if (strcmp (s->line, "ok") == 0)
            return;

        read_version (db, s);
    }
}


static void grab_version (const database_t * db,
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
                    s->module, slash - path, path,
                    s->prefix, slash - path, path);

    // Go to the main directory.
    cvs_printf (s,
                "Directory %s\n%.*s\n", s->module,
                strlen (s->prefix) - 1, s->prefix);

    cvs_printf (s,
                "Argument -kk\n"
                "Argument -r%s\n"
                "Argument --\n"
                "Argument %s\nupdate\n",
                version->version, version->file->path);

    read_versions (db, s);

    if (version->mark == SIZE_MAX)
        fatal ("cvs checkout - failed to get %s %s\n",
               version->file->path, version->version);
}


static void grab_by_option (const database_t * db,
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

    qsort (paths, paths_end - paths, sizeof (const char *),
           (int(*)(const void *, const void *)) strcmp);

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
                    s->module, d_len, d,
                    s->prefix, d_len, d);
    }

    // Go to the main directory.
    cvs_printf (s,
                "Directory %s\n%.*s\n", s->module,
                strlen (s->prefix) - 1, s->prefix);

    // Update args:
    if (r_arg)
        cvs_printf (s, "Argument -r%s\n", r_arg);

    if (D_arg)
        cvs_printf (s, "Argument -D%s\n", D_arg);

    cvs_printf (s, "Argument -kk\n" "Argument --\n");

    for (const char ** i = paths; i != paths_end; ++i)
        cvs_printf (s, "Argument %s\n", *i);

    xfree (paths);

    cvs_printf (s, "update\n");

    read_versions (db, s);
}


static void grab_versions (const database_t * db,
                           cvs_connection_t * s,
                           version_t ** fetch, version_t ** fetch_end)
{
    if (fetch_end == fetch)
        return;

    if (fetch_end == fetch + 1) {
        grab_version (db, s, *fetch);
        return;
    }

    bool idver = true;
    for (version_t ** i = fetch + 1; i != fetch_end; ++i)
        if ((*i)->version != fetch[0]->version) {
            idver = false;
            break;
        }
    if (idver) {
        grab_by_option (db, s,
                        fetch[0]->version, NULL,
                        fetch, fetch_end);
        return;
    }        

    bool tried_date = false;
    time_t dmin = fetch[0]->time;
    time_t dmax = fetch[0]->time;
    for (version_t ** i = fetch + 1; i != fetch_end; ++i)
        if ((*i)->time < dmin)
            dmin = (*i)->time;
        else if ((*i)->time > dmax)
            dmax = (*i)->time;

    if (dmax - dmin < 300) {
        tried_date = true;

        // Format the date.
        struct tm tm;
//            ++last;                     // Add a second...
        gmtime_r (&dmax, &tm);
        char date[64];
        if (strftime (date, 64, "%d %b %Y %H:%M:%S -0000", &tm) == 0)
            fatal ("strftime failed\n");

        grab_by_option (db, s,
                        fetch[0]->branch->tag[0] ? fetch[0]->branch->tag : NULL,
                        format_date (&dmax),
                        fetch, fetch_end);
    }

    for (version_t ** i = fetch; i != fetch_end; ++i)
        if ((*i)->mark == SIZE_MAX) {
            if (tried_date > 1)
                fprintf (stderr, "Missed first time round: %s %s\n",
                         (*i)->file->path, (*i)->version);
            grab_version (db, s, *i);
        }
}


static void print_commit (const database_t * db, changeset_t * cs,
                          cvs_connection_t * s)
{
    version_t * v = cs->versions[0];
    if (!v->branch) {
        fprintf (stderr, "%s <anon> %s %s COMMIT - skip\n%s\n",
                 format_date (&cs->time), v->author, v->commitid, v->log);
        return;
    }

    version_t ** fetch = NULL;
    version_t ** fetch_end = NULL;

    // Check to see if this commit actually does anything...
    bool nil = true;
    for (version_t ** i = cs->versions; i != cs->versions_end; ++i) {
        if (!(*i)->used)
            continue;

        version_t * cv = version_live (*i);
        if (cv == version_live (
                v->branch->branch_versions[(*i)->file - db->files]))
            continue;

        nil = false;
        if (cv != NULL && cv->mark == SIZE_MAX)
            ARRAY_APPEND (fetch, cv);
    }

    if (nil) {
        cs->mark = v->branch->last->mark;
        v->branch->last = cs;
        return;
    }

    // Get the versions.
    grab_versions (db, s, fetch, fetch_end);
    xfree (fetch);

    v->branch->last = cs;
    cs->mark = ++mark_counter;

    printf ("commit refs/heads/%s\n",
            *v->branch->tag ? v->branch->tag : "cvs_master");
    printf ("mark :%lu\n", cs->mark);
    printf ("committer %s <%s> %ld +0000\n", v->author, v->author, cs->time);
    printf ("data %u\n%s\n", strlen (v->log), v->log);

    for (version_t ** i = cs->versions; i != cs->versions_end; ++i)
        if ((*i)->used) {
            version_t * vv = version_normalise (*i);
            if (vv->dead)
                printf ("D %s\n", vv->file->path);
            else
                printf ("M %s :%zu %s\n",
                        vv->exec ? "755" : "644", vv->mark, vv->file->path);
        }
}


static void print_tag (const database_t * db, tag_t * tag,
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

    printf ("reset refs/%s/%s\n",
            tag->branch_versions ? "heads" : "tags",
            *tag->tag ? tag->tag : "cvs_master");

    if (tag->parent) {
        printf ("from :%lu\n\n", tag->parent->mark);
        tag->changeset.mark = tag->parent->mark;
    }

    tag->last = &tag->changeset;

    // Go through the current versions on the branch and note any version
    // fix-ups required.
    size_t keep = 0;
    size_t added = 0;
    size_t deleted = 0;
    size_t modified = 0;

    version_t ** fetch = NULL;
    version_t ** fetch_end = NULL;

    version_t ** tf = tag->tag_files;
    for (file_t * i = db->files; i != db->files_end; ++i) {
        version_t * bv = branch
            ? version_live (branch->branch_versions[i - db->files]) : NULL;
        version_t * tv = NULL;
        if (tf != tag->tag_files_end && (*tf)->file == i)
            tv = version_live (*tf++);

        if (bv == tv) {
            if (bv != NULL)
                ++keep;
            continue;
        }

        if (tv == NULL) {
            ++deleted;
            continue;
        }

        if (tv->mark == SIZE_MAX)
            ARRAY_APPEND (fetch, tv);

        if (bv == NULL)
            ++added;
        else
            ++modified;
    }

    if (added == 0 && deleted == 0 && modified == 0) {
        assert (fetch == NULL);
        return;                         // Nothing to do.
    }

    grab_versions (db, s, fetch, fetch_end);
    xfree (fetch);

    tag->fixup = true;
    tag->changeset.mark = ++mark_counter;

    const char ** list = NULL;
    const char ** list_end = NULL;

    ARRAY_APPEND (list,
                  xasprintf ("Fix-up commit generated by crap-clone.  "
                            "(~%zu +%zu -%zu =%zu)\n",
                            modified, added, deleted, keep));

    tf = tag->tag_files;
    for (file_t * i = db->files; i != db->files_end; ++i) {
        version_t * bv = branch
            ? version_live (branch->branch_versions[i - db->files]) : NULL;
        version_t * tv = NULL;
        if (tf != tag->tag_files_end && (*tf)->file == i)
            tv = version_live (*tf++);

        if (bv == tv) {
            if (bv != NULL && keep <= deleted)
                ARRAY_APPEND (list, xasprintf ("%s KEEP %s\n",
                                               bv->file->path, bv->version));
            continue;
        }

        if (tv != NULL || deleted <= keep)
            ARRAY_APPEND (list, xasprintf ("%s %s->%s\n", i->path,
                                           bv ? bv->version : "ADD",
                                           tv ? tv->version : "DELETE"));
    }

    size_t log_len = 0;
    for (const char ** i = list; i != list_end; ++i)
        log_len += strlen (*i);

    printf ("commit refs/%s/%s\n",
            tag->branch_versions ? "heads" : "tags",
            *tag->tag ? tag->tag : "cvs_master");
    printf ("mark :%lu\n", tag->changeset.mark);
    printf ("committer crap <crap> %ld +0000\n", tag->changeset.time);
    printf ("data %u\n", log_len);
    for (const char ** i = list; i != list_end; ++i) {
        fputs (*i, stdout);
        xfree (*i);
    }
    xfree (list);

    tf = tag->tag_files;
    for (file_t * i = db->files; i != db->files_end; ++i) {
        version_t * bv = branch
            ? version_live (branch->branch_versions[i - db->files]) : NULL;
        version_t * tv = NULL;
        if (tf != tag->tag_files_end && (*tf)->file == i)
            tv = version_live (*tf++);

        if (tv != bv) {
            if (tv == NULL)
                printf ("D %s\n", i->path);
            else
                printf ("M %s :%zu %s\n",
                        tv->exec ? "755" : "644", tv->mark, tv->file->path);
        }
    }
}


int main (int argc, const char * const * argv)
{
    if (argc != 3)
        fatal ("Usage: %s <root> <repo>\n", argv[0]);

    cvs_connection_t stream;
    connect_to_cvs (&stream, argv[1]);
    stream.prefix = xasprintf ("%s/%s/", stream.remote_root, argv[2]);
    stream.module = xstrdup (argv[2]);

    cvs_printf (&stream,
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
            print_commit (&db, changeset, &stream);
            changeset_update_branch_versions (&db, changeset);
        }
        else {
            tag_t * tag = as_tag (changeset);
            tag->is_released = true;
            print_tag (&db, tag, &stream);
        }

        changeset_emitted (&db, NULL, changeset);
    }

    fflush (NULL);
    fprintf (stderr,
             "Emitted %u commits (%s total %u).\n",
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
             "Exact %5u + %5u = %5u branches + tags.\n"
             "Fixup %5u + %5u = %5u branches + tags.\n",
             exact_branches, exact_tags, exact_branches + exact_tags,
             fixup_branches, fixup_tags, fixup_branches + fixup_tags);

    string_cache_stats (stderr);

    printf ("progress done\n");

    cvs_connection_destroy (&stream);

    database_destroy (&db);
    string_cache_destroy();

    return 0;
}
