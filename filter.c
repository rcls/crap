
#include "changeset.h"
#include "database.h"
#include "emission.h"
#include "file.h"
#include "filter.h"
#include "log.h"
#include "utils.h"

#include <pipeline.h>
#include <string.h>

struct filter_context {
    database_t * db;
    changeset_t ** serial;
    changeset_t ** serial_end;
};


static void filter_output (void * data)
{
    struct filter_context * context = data;
    size_t seq = 0;
    for (changeset_t ** p = context->serial; p != context->serial_end; ++p) {
        // FIXME - some changesets are going to get dropped...  We should
        // pre-compute them and not send to the filter...
        if ((*p)->type == ct_commit) {
            printf ("COMMIT %zu %s\n", ++seq, (*p)->versions[0]->branch->tag);
            continue;
        }

        tag_t * tag = as_tag (*p);
        printf ("%s %zu %s\n",
                tag->branch_versions ? "BRANCH" : "TAG",
                ++seq,                  // FIXME - should refer to commit.
                tag->tag);
    }
    fflush (stdout);
}


static changeset_t * ref_lookup (const database_t * db, const char * ref)
{
    switch (*ref) {
    case ':':
        ;
        extern void abort();
        abort();                        // NYI.
    case '-':
    case '=': ;
        tag_t * tag = database_find_tag (db, ref + 1);
        if (tag == NULL)
            fatal ("Unknown tag reference from filter: %s\n", ref);
        // FIXME - if no fixups, then '-' should take changeset also?
        if (*ref == '=')
            return &tag->changeset;
        if (tag->parent == NULL)
            fatal ("Unknown tag reference from filter: %s\n", ref);
        return tag->parent;
    default:
        fatal ("Illegal reference from filter: %s\n", ref);
    }
}


static void filter_input (database_t * db, FILE * in)
{
    size_t n = 0;
    char * line = NULL;
    ssize_t len;
    while ((len = getline (&line, &n, in)) >= 0) {
        if (len > 0 && line[len - 1] == '\n')
            line[--len] = 0;
        if (len == 0)
            continue;
        if (strlen (line) != (size_t) len)
            fatal ("Line with NUL from filter\n");
        if (starts_with (line, "DELETE TAG ")) {
            tag_t * tag = database_find_tag (db, line + 11);
            if (tag == NULL)
                fatal ("Unknown tag from filter: %s\n", line);
            if (tag->branch_versions != NULL)
                fatal ("Filter attempts to delete branch: %s\n", line);
            tag->deleted = true;
        }
        else if (starts_with (line, "MERGE ")) {
            char * ref1 = line + 6;
            char * ref2 = strchr (ref1, ' ');
            if (ref2 == NULL)
                fatal ("Illegal merge from filter: '%s'\n", line);
            *ref2++ = 0;
            changeset_t * cs1 = ref_lookup (db, ref1);
            changeset_t * cs2 = ref_lookup (db, ref2);
            if (cs2->type == ct_tag)
                as_tag (cs2)->merge_source = true;
            ARRAY_APPEND (cs1->merge, cs2);
        }
        else
            fatal ("Unknown line from filter: '%s'\n", line);
    }
    xfree (line);
}


void filter_changesets (database_t * db,
                        changeset_t ** serial, changeset_t ** serial_end,
                        const char * filter_command)
{
    // Set up a pipeline for running the subprocess and sending the data to it.
    struct filter_context context = { db, serial, serial_end };
    pipeline * pl = pipeline_new();
    pipeline_command (
        pl,
        pipecmd_new_function ("filter source", filter_output, NULL, &context));
    pipeline_command_argstr (pl, filter_command);
    pipeline_want_out (pl, -1);

    fflush (NULL);                      // We're forking...
    pipeline_start (pl);

    filter_input (db, pipeline_get_outfile (pl));

    int res = pipeline_wait (pl);
    if (res != 0)
        fatal ("filter subprocess gave error: %i\n", res);

    pipeline_free (pl);
}
