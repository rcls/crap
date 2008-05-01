#include "branch.h"
#include "changeset.h"
#include "database.h"
#include "emission.h"
#include "file.h"
#include "log_parse.h"
#include "string_cache.h"

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

    // Do a pass through the branch/tag structure; this breaks any cycles in it.
    heap_t branch_heap;
    branch_heap_init (&db, &branch_heap);
    tag_t * tag;
    while ((tag = branch_heap_next (&branch_heap))) {
        fprintf (stderr, "Tag '%s' with %u parents\n",
                 tag->tag, tag->parents_end - tag->parents);

        for (parent_branch_t * i = tag->parents; i != tag->parents_end; ++i)
            fprintf (stderr, "\t%s\n", i->branch->tag);
    }
    heap_destroy (&branch_heap);

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

    for (tag_t * i = db.tags; i != db.tags_end; ++i)
        i->is_released = false;

    // Do a second pass through the changesets, this time assigning
    // branch-points.
    prepare_for_emission (&db);
    prepare_for_tag_emission (&db);

    while (!heap_empty (&db.ready_tags)) {
        tag_t * tag = heap_pop (&db.ready_tags);
        assign_tag_point (&db, tag);
// FIXME        tag_emitted (&db, tag);

        while ((changeset = next_changeset (&db))) {
            changeset_update_branch_hash (&db, changeset);
            changeset_emitted (&db, changeset);
        }
    }

    for (tag_t * i = db.tags; i != db.tags_end; ++i)
        i->is_released = false;

    // Emit the changesets for real.
    prepare_for_emission (&db);
    emitted_changesets = 0;
    // FIXME - should not be splitting at this point.
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

        if (changeset_update_branch_hash (&db, changeset) == 0)
            printf ("[There were no real changes in this changeset]\n");

        for (version_t * v = change; v; v = v->cs_sibling)
            if (!implicit_merge || v->implicit_merge)
                printf ("\t%s %s\n", v->file->rcs_path, v->version);

        printf ("\n");

        ++emitted_changesets;
        changeset_emitted (&db, changeset);
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
