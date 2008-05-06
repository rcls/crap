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


int main()
{
    char * line = NULL;
    size_t len = 0;

    database_t db;

    read_files_versions (&db, &line, &len, stdin);
    free (line);

    create_changesets (&db);

    branch_analyse (&db);

    // Do a pass through the changesets, this time assigning branch-points.
    prepare_for_emission (&db, NULL);
    prepare_for_tag_emission (&db);

    // FIXME - we do not seem to interleave tag & changeset emissions correctly.
    tag_t * tag;
    while ((tag = branch_heap_next (&db.ready_tags))) {
        assign_tag_point (&db, tag);

        changeset_t * changeset;
        while ((changeset = next_changeset (&db))) {
            changeset_emitted (&db, NULL, changeset);
            // Add the changeset to its branch.  FIXME handle vendor merges.
            if (changeset->type == ct_commit &&
                changeset->versions->branch) {
                tag_t * branch = changeset->versions->branch->tag;
                ARRAY_APPEND (branch->changeset.children, changeset);
            }
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
    size_t emitted_changesets = 0;
    changeset_t * changeset;
    while ((changeset = next_changeset (&db))) {
        switch (changeset->type) {
        case ct_tag:
            print_tag (changeset);
            break;
        case ct_implicit_merge:
            ++emitted_changesets;
            print_implicit_merge (changeset);
            break;
        case ct_commit:
            ++emitted_changesets;
            print_commit (changeset);
            break;
        default:
            assert ("Unknown changeset type" == 0);
        }

        changeset_emitted (&db, NULL, changeset);
    }

    fflush (NULL);
    fprintf (stderr, "Emitted %u of %u changesets.\n",
             emitted_changesets, db.changesets_end - db.changesets);

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
