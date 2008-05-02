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
            ARRAY_EXTEND (branch->changesets, branch->changesets_end);
            branch->changesets_end[-1] = changeset;

            changeset_update_branch_hash (&db, changeset);
        }
    }

    // Prepare for the real changeset emission.  This time the tags go through
    // the the usual emission process, and branches block revisions on the
    // branch.
    prepare_for_emission (&db);
    for (tag_t * i = db.tags; i != db.tags_end; ++i)
        i->is_released = false;

    // Emit the changesets for real.
    emitted_changesets = 0;
    // FIXME - handle emitting tags.
    while ((changeset = next_changeset (&db))) {

        struct tm dtm;
        char date[32];
        size_t dl = strftime (date, sizeof (date), "%F %T %Z",
                              localtime_r (&changeset->time, &dtm));
        if (dl == 0)
            // Maybe someone gave us a crap timezone?
            dl = strftime (date, sizeof (date), "%F %T %Z",
                           gmtime_r (&changeset->time, &dtm));

        assert (dl != 0);

        version_t * change;
        const char * branch;
        const char * log;
        bool implicit_merge = false;
        if (changeset->type == ct_implicit_merge) {
            change = changeset->parent->versions;
            branch = "";
            log = "Implicit merge of vendor branch to trunk.\n";
            implicit_merge = true;
        }
        else {
            change = changeset->versions;
            if (change->branch)
                branch = change->branch->tag->tag;
            else
                branch = "";
            log = change->log;
        }

        printf ("%s %s %s %s\n%s\n",
                date, branch, change->author, change->commitid, log);

        // FIXME - replace this.
/*         if (changeset_update_branch_hash (&db, changeset) == 0) */
/*             printf ("[There were no real changes in this changeset]\n"); */

        for (version_t * v = change; v; v = v->cs_sibling)
            if (!implicit_merge || v->implicit_merge)
                printf ("\t%s %s\n", v->file->rcs_path, v->version);

        printf ("\n");

        ++emitted_changesets;
        changeset_emitted (&db, changeset);

        for (changeset_t * i = changeset->children; i; i = i->sibling)
            heap_insert (&db.ready_changesets, i);
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
