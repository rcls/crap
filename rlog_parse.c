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


void changeset_report (const changeset_t * cs)
{
    printf ("Changeset %s\n%s\n", cs->versions->author, cs->versions->log);
    for (const version_t * v = cs->versions; v; v = v->cs_sibling)
        printf ("    %s:%s\n", v->file->rcs_path, v->version);
}


void cycle_report (const database_t * db, const version_t * v)
{
    printf ("*********** CYCLE **********\n");
    printf ("File %s:%s", v->file->rcs_path, v->version);
    changeset_report (v->changeset);
    const version_t * u = v;
    do {
        u = preceed (u);
        printf ("File %s:%s", u->file->rcs_path, u->version);
        changeset_report (u->changeset);
    }
    while (u != v);
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
    for (size_t i = 0; i != db.num_files; ++i) {
        file_t * f = &db.files[i];
        for (size_t j = 0; j != f->num_versions; ++j)
            if (f->versions[j].parent == NULL)
                version_release (&db, &f->versions[j]);
    }

    size_t emitted_changesets = 0;
    while (db.ready_changesets.num_entries != 0) {
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

        printf ("%s %s %s\n%s\n",
                date, change->author, change->commitid, change->log);

        for (version_t * v = change; v; v = v->cs_sibling)
            printf ("\t%s %s\n", v->file->rcs_path, v->version);

        printf ("\n");

        ++emitted_changesets;
        changeset_emitted (&db, changeset);
    }

    fflush (NULL);
    fprintf (stderr, "Emitted %u of %u changesets.\n",
             emitted_changesets, db.num_changesets);
    if (db.ready_versions.num_entries)
        fprintf (stderr, "Versions ready but unemitted: %u\n",
                 db.ready_versions.num_entries);

    if (db.ready_versions.num_entries)
        // Presume we have a cycle; log a cycle.
        cycle_report (&db, cycle_find (heap_front (&db.ready_versions)));

    string_cache_stats (stderr);

    database_destroy (&db);
    string_cache_destroy();

    return 0;
}
