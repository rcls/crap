#include "changeset.h"
#include "database.h"
#include "file.h"
#include "log_parse.h"
#include "string_cache.h"

#include <assert.h>
#include <stdio.h>
#include <time.h>

int main()
{
    char * line = NULL;
    size_t len = 0;

    database_t db;

    read_files_versions (&db, &line, &len, stdin);

    create_changesets (&db);

    for (int i = 0; i != db.num_changesets; ++i) {
        version_t * change = db.changesets[i];

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
    }

    database_destroy (&db);

    string_cache_stats (stderr);

    return 0;
}
