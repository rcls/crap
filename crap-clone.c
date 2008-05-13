#include "branch.h"
#include "changeset.h"
#include "database.h"
#include "emission.h"
#include "file.h"
#include "log.h"
#include "log_parse.h"
#include "server.h"
#include "string_cache.h"
#include "utils.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static long mark_counter;

static const char * format_date (const time_t * time)
{
    struct tm dtm;
    static char date[32];
    size_t dl = strftime (date, sizeof (date), "%F %T %Z",
                          localtime_r (time, &dtm));
    if (dl == 0)
        // Maybe someone gave us a crap timezone?
        dl = strftime (date, sizeof (date), "%F %T %Z",
                       gmtime_r (time, &dtm));

    assert (dl != 0);
    return date;
}


static void print_commit (changeset_t * cs)
{
    const version_t * v = cs->versions;
    if (!v->branch) {
        fprintf (stderr, "Skip %s <anon> %s %s COMMIT\n%s\n",
                 format_date (&cs->time), v->author, v->commitid, v->log);
        return;
    }

    cs->mark = ++mark_counter;

    printf ("commit refs/heads/%s\n",
            *v->branch->tag->tag ? v->branch->tag->tag : "cvs_master");
    printf ("mark :%lu\n", cs->mark);
    printf ("committer %s <%s> %ld +0000\n", v->author, v->author, cs->time);
    printf ("data %u\n%s\n", strlen (v->log), v->log);

    for (const version_t * i = v; i; i = i->cs_sibling) {
        if (!i->used)
            continue;                   // Un-merged vendor import.
        if (i->dead) {
            printf ("D %s\n", i->file->path);
            continue;
        }

        const char * content = xasprintf ("File %s version %s\n",
                                          i->file->path, i->version);

        printf ("M 644 inline %s\ndata %u\n%s", i->file->path,
                strlen (content), content);

        xfree (content);
    }
}


static void print_tag (const database_t * db, tag_t * tag)
{
    fprintf (stderr, "%s %s %s\n",
             format_date (&tag->changeset.time),
             tag->branch_versions ? "BRANCH" : "TAG",
             tag->tag);

    if (tag->exact_match)
        fprintf (stderr, "Exact match\n");

    printf ("reset refs/%s/%s\n",
            tag->branch_versions ? "heads" : "tags",
            *tag->tag ? tag->tag : "cvs_master");

    if (tag->parent == NULL) {
        // Special case.
        fprintf (stderr, "No parent; create from scratch\n");
        for (file_tag_t ** i = tag->tag_files; i != tag->tag_files_end; ++i)
            if ((*i)->version && !(*i)->version->dead)
                fprintf (stderr, "\t%s %s\n",
                        (*i)->version->file->path, (*i)->version->version);
        fprintf (stderr, "WIERD: exact but create from scratch\n\n");
        return;
    }

    tag_t * branch;
    if (tag->parent->type == ct_commit)
        branch = tag->parent->versions->branch->tag;
    else
        branch = as_tag (tag->parent);

    printf ("from :%lu\n\n", tag->parent->mark);
    tag->changeset.mark = tag->parent->mark;

    file_tag_t ** tf = tag->tag_files;
    // Go through the current versions on the branch and note any version
    // fix-ups required.
    size_t fixups = 0;
    size_t keep = 0;
    for (file_t * i = db->files; i != db->files_end; ++i) {
        version_t * bv = branch->branch_versions[i - db->files];
        if (bv != NULL && bv->dead)
            bv = NULL;
        if (bv != NULL)
            bv = version_normalise (bv);
        version_t * tv = NULL;
        if (tf != tag->tag_files_end && (*tf)->file == i)
            tv = (*tf++)->version;
        if (tv != NULL && tv->dead)
            tv = NULL;
        if (tv != NULL)
            tv = version_normalise (tv);

        if (bv == tv) {
            if (bv != NULL)
                ++keep;
            continue;
        }

        if (fixups++ == 0) {
            tag->changeset.mark = ++mark_counter;
            printf ("commit refs/%s/%s\n",
                    tag->branch_versions ? "heads" : "tags",
                    *tag->tag ? tag->tag : "cvs_master");
            printf ("mark :%lu\n", tag->changeset.mark);
            printf ("committer crap <crap> %ld +0000\n",
                    tag->changeset.time);
            static const char log[] = "Fix-up commit for ";
            printf ("data %u\n%s%s\n",
                    strlen (log) + strlen (tag->tag) + 1,
                    log, tag->tag);
        }

        if (tv == NULL) {
            printf ("D %s\n", i->path);
            continue;
        }

        const char * content = xasprintf ("File %s version %s\n",
                                          i->path, tv->version);

        printf ("M 644 inline %s\ndata %u\n%s", i->path,
                strlen (content), content);

        xfree (content);
    }

    if (fixups == 0 && !tag->exact_match)
        fprintf (stderr, "WIERD: no fixups but not exact match\n");
    else if (fixups != 0 && tag->exact_match)
        fprintf (stderr, "WIERD: fixups for exact match\n");

    fprintf (stderr, "Keep %u live file versions\n\n", keep);
}


int main (int argc, const char * const * argv)
{
    if (argc != 3)
        fatal ("Usage: %s <root> <repo>\n", argv[0]);

    const char * rroot;
    FILE * stream = connect_to_server (argv[1], &rroot);
    const char * prefix = xasprintf ("%s/%s/", rroot, argv[2]);

    fprintf (stream,
             "Global_option -q\n"
             "Argument --\n"
             "Argument %s\n"
             "rlog\n", argv[2]);
    
    char * line = NULL;
    size_t len = 0;

    database_t db;

    read_files_versions (&db, &line, &len, stream, prefix);
    free (line);
    fclose (stream);
    xfree (prefix);

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
            for (file_tag_t ** j = i->tag_files; j != i->tag_files_end; ++j)
                i->branch_versions[(*j)->file - db.files] = (*j)->version;
        }
    }

    // Emit the changesets for real.
    size_t emitted_commits = 0;
    changeset_t * changeset;
    while ((changeset = next_changeset (&db))) {
        if (changeset->type == ct_commit) {
            ++emitted_commits;
            changeset_update_branch_versions (&db, changeset);
            print_commit (changeset);
        }
        else {
            tag_t * tag = as_tag (changeset);
            tag->is_released = true;
            print_tag (&db, tag);
        }

        changeset_emitted (&db, NULL, changeset);
    }

    fflush (NULL);
    fprintf (stderr,
             "Emitted %u commits (%s total %u).\n",
             emitted_commits,
             emitted_commits == db.changesets_end - db.changesets ? "=" : "!=",
             db.changesets_end - db.changesets);

    size_t matched_branches = 0;
    size_t late_branches = 0;
    size_t matched_tags = 0;
    size_t late_tags = 0;
    for (tag_t * i = db.tags; i != db.tags_end; ++i) {
        assert (i->is_released);
        if (i->branch_versions)
            if (i->exact_match)
                ++matched_branches;
            else
                ++late_branches;
        else
            if (i->exact_match)
                ++matched_tags;
            else
                ++late_tags;
    }

    fprintf (stderr,
             "Matched %5u + %5u = %5u branches + tags.\n"
             "Late    %5u + %5u = %5u branches + tags.\n",
             matched_branches, matched_tags, matched_branches + matched_tags,
             late_branches, late_tags, late_branches + late_tags);

    string_cache_stats (stderr);

    printf ("progress done\n");

    database_destroy (&db);
    string_cache_destroy();

    return 0;
}
