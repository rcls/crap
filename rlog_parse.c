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


static const version_t * preceed (const version_t * v)
{
    /* If cs is not ready to emit, then some version in cs is blocked.  The
     * earliest un-emitted ancestor of that version will be ready to emit.
     * Search for it.  We could be a bit smarter by seraching harder for the
     * oldest possible version.  But most cycles are trivial (length 1) so it's
     * probably not worth the effort.  */
    for (version_t * csv = v->changeset->versions; csv; csv = csv->cs_sibling) {
        if (csv->ready_index != SIZE_MAX)
            continue;                   /* Not blocked.  */
        for (version_t * v = csv->parent; v; v = v->parent)
            if (v->ready_index != SIZE_MAX)
                return v;
    }
    abort();
}


static const version_t * cycle_find (const version_t * v)
{
    const version_t * slow = v;
    const version_t * fast = v;
    do {
        slow = preceed (slow);
        fast = preceed (preceed (fast));
    }
    while (slow != fast);
    return slow;
}


static void cycle_split (database_t * db, changeset_t * cs)
{
    fflush (NULL);
    fprintf (stderr, "*********** CYCLE **********\n");
    /* We split the changeset into to.  We leave all the blocked versions
     * in cs, and put the ready-to-emit into nw.  */

    changeset_t * new = database_new_changeset (db);
    new->unready_count = 0;
    version_t ** cs_v = &cs->versions;
    version_t ** new_v = &new->versions;
    for (version_t * v = cs->versions; v; v = v->cs_sibling) {
        if (v->ready_index == SIZE_MAX) {
            /* Blocked; stays in cs.  */
            *cs_v = v;
            cs_v = &v->cs_sibling;
        }
        else {
            /* Ready-to-emit; goes into new.  */
            v->changeset = new;
            *new_v = v;
            new_v = &v->cs_sibling;
        }
    }

    *cs_v = NULL;
    *new_v = NULL;
    assert (cs->versions);
    assert (new->versions);

    heap_insert (&db->ready_changesets, new);

    fprintf (stderr, "Changeset %s %s\n%s\n",
             cs->versions->branch ? cs->versions->branch->tag->tag : "",
             cs->versions->author, cs->versions->log);
    for (const version_t * v = new->versions; v; v = v->cs_sibling)
        fprintf (stderr, "    %s:%s\n", v->file->rcs_path, v->version);
        
    fprintf (stderr, "Deferring:\n");

    for (const version_t * v = cs->versions; v; v = v->cs_sibling)
        fprintf (stderr, "    %s:%s\n", v->file->rcs_path, v->version);
}


int main()
{
    char * line = NULL;
    size_t len = 0;

    database_t db;

    read_files_versions (&db, &line, &len, stdin);
    free (line);

    create_changesets (&db);

    /* Mark the initial versions as ready to emit.  */
    for (file_t * f = db.files; f != db.files_end; ++f)
        for (version_t * j = f->versions; j != f->versions_end; ++j)
            if (j->parent == NULL)
                version_release (&db, j);

    /* Do a dummy run of the changeset emission; this breaks any cycles before
     * we commit ourselves to the real change-set order.  */
    size_t emitted_changesets = 0;
    while (db.ready_versions.entries != db.ready_versions.entries_end) {
        if (db.ready_changesets.entries == db.ready_changesets.entries_end)
            cycle_split (
                &db, cycle_find (heap_front (&db.ready_versions))->changeset);

        changeset_t * changeset = heap_pop (&db.ready_changesets);
        changeset_emitted (&db, changeset);
        ++emitted_changesets;
    }

    assert (db.ready_changesets.entries_end == db.ready_changesets.entries_end);
    assert (emitted_changesets == db.changesets_end - db.changesets);

    /* Re-do the changeset unready counts.  */
    for (changeset_t ** i = db.changesets; i != db.changesets_end; ++i)
        for (version_t * j = (*i)->versions; j; j = j->cs_sibling)
            ++(*i)->unready_count;

    /* Mark the initial versions as ready to emit once again.  */
    for (file_t * f = db.files; f != db.files_end; ++f)
        for (version_t * j = f->versions; j != f->versions_end; ++j)
            if (j->parent == NULL)
                version_release (&db, j);

    /* FIXME - we will have to mark tags as unemitted at some point also.  */

    /* Emit the changesets for real.  */
    emitted_changesets = 0;
    while (db.ready_versions.entries != db.ready_versions.entries_end) {
        if (db.ready_changesets.entries == db.ready_changesets.entries_end)
            cycle_split (
                &db, cycle_find (heap_front (&db.ready_versions))->changeset);

        changeset_t * changeset = heap_pop (&db.ready_changesets);
        version_t * change = changeset->versions;

        struct tm dtm;
        char date[32];
        size_t dl = strftime (date, sizeof (date), "%F %T %Z",
                              localtime_r (&change->time, &dtm));
        if (dl == 0)
            /* Maybe someone gave us a crap timezone?  */
            dl = strftime (date, sizeof (date), "%F %T %Z",
                           gmtime_r (&change->time, &dtm));

        assert (dl != 0);

        printf ("%s %s %s %s\n%s\n",
                date, change->branch ? change->branch->tag->tag : "",
                change->author, change->commitid, change->log);

        if (changeset_update_branch (&db, changeset) == 0)
            printf ("[There were no real changes in this changeset]\n");

        for (version_t * v = change; v; v = v->cs_sibling)
            printf ("\t%s %s\n", v->file->rcs_path, v->version);

        printf ("\n");

        ++emitted_changesets;
        changeset_emitted (&db, changeset);
    }

    fflush (NULL);
    fprintf (stderr, "Emitted %u of %u changesets.\n",
             emitted_changesets, db.changesets_end - db.changesets);

    size_t emitted_tags = 0;
    for (tag_t * i = db.tags; i != db.tags_end; ++i)
        if (i->is_emitted)
            ++emitted_tags;

    fprintf (stderr, "Emitted %u of %u tags.\n",
             emitted_tags, db.tags_end - db.tags);

    string_cache_stats (stderr);

    database_destroy (&db);
    string_cache_destroy();

    return 0;
}
