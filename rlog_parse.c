#include "changeset.h"
#include "database.h"
#include "file.h"
#include "log_parse.h"
#include "string_cache.h"

#include <assert.h>
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

    create_changesets (&db);

    /* Mark the initial versions are ready to emit.  */
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
    string_cache_stats (stderr);

    database_destroy (&db);
    string_cache_destroy();

    return 0;
}
