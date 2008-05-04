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
            v->branch->tag->tag, v->author, v->commitid, v->log);

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
            v->branch->tag->tag, v->author, v->commitid, cs->versions->log);

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

    branch_analyse (&db);

    create_changesets (&db);

    // Do a pass through the changesets; this breaks any cycles.
    prepare_for_emission (&db);
    size_t emitted_changesets = 0;
    changeset_t * changeset;
    while ((changeset = next_changeset_split (&db))) {
        changeset_emitted (&db, changeset);
        ++emitted_changesets;
    }

    assert (heap_empty (&db.ready_changesets));
    assert (emitted_changesets == db.changesets_end - db.changesets);

    // Do a second pass through the changesets, this time assigning
    // branch-points.
    prepare_for_emission (&db);
    prepare_for_tag_emission (&db);

    tag_t * tag;
    while ((tag = branch_heap_next (&db.ready_tags))) {
        fprintf (stderr, "Process tag '%s'\n", tag->tag);
        assign_tag_point (&db, tag);

        while ((changeset = next_changeset (&db))) {
            changeset_emitted (&db, changeset);
            // Add the changeset to its branch.  FIXME handle vendor merges.
            tag_t * branch = changeset->versions->branch->tag;
            ARRAY_EXTEND (branch->changeset.children,
                          branch->changeset.children_end);
            branch->changeset.children_end[-1] = changeset;

            changeset_update_branch_hash (&db, changeset);
        }
    }

    // Prepare for the real changeset emission.  This time the tags go through
    // the the usual emission process, and branches block revisions on the
    // branch.

    // Re-do the version->changeset unready counts.
    for (tag_t * i = db.tags; i != db.tags_end; ++i) {
        assert (i->changeset.unready_count == 0);
    }
    for (changeset_t ** i = db.changesets; i != db.changesets_end; ++i) {
        assert ((*i)->unready_count == 0);
    }
    for (changeset_t ** i = db.changesets; i != db.changesets_end; ++i) {
        if ((*i)->type == ct_commit)
            for (version_t * j = (*i)->versions; j; j = j->cs_sibling)
                ++(*i)->unready_count;
        for (changeset_t ** j = (*i)->children; j != (*i)->children_end; ++j)
            ++(*j)->unready_count;
    }
    for (tag_t * i = db.tags; i != db.tags_end; ++i) {
        i->is_released = false;
        for (changeset_t ** j = i->changeset.children;
             j != i->changeset.children_end; ++j)
            ++(*j)->unready_count;
    }

    for (tag_t * i = db.tags; i != db.tags_end; ++i)
        if (i->changeset.unready_count == 0)
            heap_insert (&db.ready_changesets, &i->changeset);
    // Mark the initial versions as ready to emit.
    for (file_t * f = db.files; f != db.files_end; ++f)
        for (version_t * j = f->versions; j != f->versions_end; ++j)
            if (j->parent == NULL)
                version_release (&db, j);

    // Emit the changesets for real.
    emitted_changesets = 0;
    // FIXME - handle emitting tags.
    while ((changeset = next_changeset (&db))) {
        switch (changeset->type) {
        case ct_tag:
            print_tag (changeset);
            break;
        case ct_implicit_merge:
            print_implicit_merge (changeset);
            break;
        case ct_commit:
            print_commit (changeset);
            break;
        default:
            assert ("Unknown changeset type" == 0);
        }

        ++emitted_changesets;
        changeset_emitted (&db, changeset);
        // Pick off the versions.  FIXME - no need to even use this heap; just
        // use a differnet changeset_emitted.
/*         while (!heap_empty (&db.ready_versions)) */
/*             version_release (&db, heap_pop (&db.ready_versions)); */
    }

    fflush (NULL);
    fprintf (stderr, "Emitted %u of %u changesets.\n",
             emitted_changesets, db.changesets_end - db.changesets);

    size_t emitted_tags = 0;
    size_t emitted_branches = 0;
    size_t tags = 0;
    size_t branches = 0;
    for (tag_t * i = db.tags; i != db.tags_end; ++i)
        if (i->branch_versions) {
            ++branches;
            emitted_branches += i->is_released;
            if (!i->is_released)
                fprintf (stderr, "Missed branch %s\n", i->tag);
        }
        else {
            ++tags;
            emitted_tags += i->is_released;
            if (!i->is_released)
                fprintf (stderr, "Missed tag %s\n", i->tag);
        }

    fprintf (stderr,
             "Emitted %u + %u = %u of %u + %u = %u branches + tags = total.\n",
             emitted_branches, emitted_tags, emitted_branches + emitted_tags,
             branches, tags, branches + tags);

    string_cache_stats (stderr);

    database_destroy (&db);
    string_cache_destroy();

    return 0;
}
