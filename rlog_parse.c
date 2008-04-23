#include "changeset.h"
#include "database.h"
#include "file.h"
#include "log_parse.h"
#include "string_cache.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <stdint.h>
const version_t * preceed (const version_t * v)
{
    // If cs is not ready to emit, then some version in cs is blocked.  The
    // earliest un-emitted ancestor of that version will be ready to emit.
    // Search for it.
    for (version_t * csv = v->changeset->versions; csv; csv = csv->cs_sibling) {
        if (csv->ready_index != SIZE_MAX)
            continue;                   /* Not blocked.  */
        for (version_t * v = csv->parent; v; v = v->parent)
            if (v->ready_index != SIZE_MAX)
                return v;
    }
    abort();
}

const version_t * cycle_find (const version_t * v)
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


void cycle_split (database_t * db, changeset_t * cs)
{
    fprintf (stderr, "*********** CYCLE **********\n");
    /* We split the changeset into to.  We leave all the blocked versions
     * in cs, and put the ready-to-emit into nw.  */

    changeset_t * new = database_new_changeset (db);
    new->unready_versions = 0;
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

    size_t emitted_changesets = 0;
    while (db.ready_versions.num_entries) {
        if (db.ready_changesets.num_entries == 0)
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

        for (version_t * v = change; v; v = v->cs_sibling)
            printf ("\t%s %s\n", v->file->rcs_path, v->version);

        printf ("\n");

        ++emitted_changesets;
        changeset_emitted (&db, changeset);
    }

    fflush (NULL);
    fprintf (stderr, "Emitted %u of %u changesets.\n",
             emitted_changesets, db.changesets_end - db.changesets);

    string_cache_stats (stderr);

    database_destroy (&db);
    string_cache_destroy();

    return 0;
}
