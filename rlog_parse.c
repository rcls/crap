#include "branch.h"
#include "changeset.h"
#include "database.h"
#include "emission.h"
#include "file.h"
#include "log_parse.h"
#include "string_cache.h"
#include "utils.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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


static void print_commit (const changeset_t * cs)
{
    const version_t * v = cs->versions;
    printf ("%s %s %s %s\n%s\n",
            format_date (&cs->time),
            v->branch
            ? *v->branch->tag->tag ? v->branch->tag->tag : "<trunk>"
            : "<anon>",
            v->author, v->commitid, v->log);

    // FIXME - replace this.
/*         if (changeset_update_branch_hash (&db, changeset) == 0) */
/*             printf ("[There were no real changes in this changeset]\n"); */

    for (const version_t * i = v; i; i = i->cs_sibling)
        printf ("\t%s %s\n", i->file->rcs_path, i->version);

    printf ("\n");
}


static void print_implicit_merge (const changeset_t * cs)
{
    const version_t * v = cs->parent->versions;
    printf ("%s %s %s %s\n%s\n",
            format_date (&cs->time),
            v->branch->tag->tag, v->author, v->commitid, v->log);

    // FIXME - replace this.
/*         if (changeset_update_branch_hash (&db, changeset) == 0) */
/*             printf ("[There were no real changes in this changeset]\n"); */

    for (const version_t * i = v; i; i = i->cs_sibling)
        if (v->implicit_merge)
            printf ("\t%s %s\n", i->file->rcs_path, i->version);

    printf ("\n");
    

}


static void print_tag (changeset_t * cs)
{
    tag_t * tag = as_tag (cs);
    tag->is_released = true;
    printf ("%s %s %s\n",
            format_date (&cs->time),
            tag->branch_versions ? "BRANCH" : "TAG",
            tag->tag);
}


static int tag_compare (const void * AA, const void * BB)
{
    const tag_t * A = AA;
    const tag_t * B = BB;
    return A > B;
}


int main()
{
    char * line = NULL;
    size_t len = 0;

    database_t db;

    read_files_versions (&db, &line, &len, stdin);
    free (line);

    create_changesets (&db);

    branch_analyse (&db);

    // Mark commits as children of their branch.
    for (changeset_t ** j = db.changesets; j != db.changesets_end; ++j)
        if ((*j)->type == ct_commit) {
            if ((*j)->versions->branch)
                ARRAY_APPEND ((*j)->versions->branch->tag->changeset.children,
                              *j);
        }
        else if ((*j)->type == ct_implicit_merge)
            ARRAY_APPEND (db.tags[0].changeset.children, *j);
        else
            abort();

    // Do a pass through the changesets, this time assigning branch-points.
    heap_init (&db.ready_tags,
               offsetof (tag_t, changeset.ready_index), tag_compare);

    // Child unready counts.
    for (tag_t * i = db.tags; i != db.tags_end; ++i) {
        i->is_released = false;
        i->exact_match = false;
        for (changeset_t ** j = i->changeset.children;
             j != i->changeset.children_end; ++j)
            ++(*j)->unready_count;
        for (branch_tag_t * j = i->tags; j != i->tags_end; ++j)
            ++j->tag->changeset.unready_count;
    }
    prepare_for_emission (&db, NULL);

    // Put the tags that are ready right now on to the heap.
    for (tag_t * i = db.tags; i != db.tags_end; ++i)
        if (i->changeset.unready_count == 0) {
            i->is_released = true;
            heap_insert (&db.ready_tags, i);
        }

    tag_t * tag;
    while ((tag = branch_heap_next (&db.ready_tags))) {
        // Release all the children; none should be tags.  (branch_heap_next has
        // released the tags).
        for (changeset_t ** i = tag->changeset.children;
             i != tag->changeset.children_end; ++i) {
            assert ((*i)->type != ct_tag);
            changeset_release (&db, *i);
        }

        assign_tag_point (&db, tag);

        changeset_t * changeset;
        while ((changeset = next_changeset (&db))) {
            changeset_emitted (&db, NULL, changeset);
            changeset_update_branch_hash (&db, changeset);
        }
    }

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

    // Mark the initial tags as ready to emit.
    for (tag_t * i = db.tags; i != db.tags_end; ++i)
        if (i->changeset.unready_count == 0)
            heap_insert (&db.ready_changesets, &i->changeset);

    // Emit the changesets for real.
    size_t emitted_commits = 0;
    size_t emitted_implicit_merges = 0;
    changeset_t * changeset;
    while ((changeset = next_changeset (&db))) {
        switch (changeset->type) {
        case ct_tag:
            print_tag (changeset);
            break;
        case ct_implicit_merge:
            ++emitted_implicit_merges;
            print_implicit_merge (changeset);
            break;
        case ct_commit:
            ++emitted_commits;
            print_commit (changeset);
            break;
        default:
            assert ("Unknown changeset type" == 0);
        }

        changeset_emitted (&db, NULL, changeset);
    }

    fflush (NULL);
    fprintf (stderr,
             "Emitted %u commits and %u implicit merges (total %s %u).\n",
             emitted_commits, emitted_implicit_merges,
             emitted_commits + emitted_implicit_merges
             == db.changesets_end - db.changesets ? "=" : "!=",
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

    database_destroy (&db);
    string_cache_destroy();

    return 0;
}
