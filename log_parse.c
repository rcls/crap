
#include "log.h"
#include "log_parse.h"
#include "string_cache.h"
#include "xmalloc.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define REV_BOUNDARY "M ----------------------------"
#define FILE_BOUNDARY "M ============================================================================="


typedef struct file_version_t {
    struct file_version_t * parent;     /* Parent version.  */
    const char * version;               /* Version, cached.  */
    const char * log;                   /* Log info, cached.  */
} file_version_t;


static size_t next_line (char ** line, size_t * len, FILE * stream)
{
    ssize_t s = getline (line, len, stream);
    if (s < 0)
        bugger ("Unexpected EOF from server.\n");

    if (strlen (*line) < s)
        bugger ("Got line containing ASCII NUL from server.\n");

    if (s > 0 && (*line)[s - 1] == '\n') {
        --s;
        (*line)[s] = 0;
    }

    return s;
}


static inline bool starts_with (const char * haystack, const char * needle)
{
    return strncmp (haystack, needle, strlen (needle)) == 0;
}


static inline bool ends_with (const char * haystack, const char * needle)
{
    size_t h_len = strlen (haystack);
    size_t n_len = strlen (needle);
    return h_len >= n_len
        && memcmp (haystack + h_len - n_len, needle, n_len) == 0;
}


void read_file_version (const char * rcs_file,
                        char ** __restrict__ l, size_t * buffer_len, FILE * f)
{
    if (!starts_with (*l, "M revision "))
        bugger ("Log (%s) did not have expected 'revision' line: %s\n",
                rcs_file, *l);

    const char * revision = cache_string (*l + 11);
    const char * author = NULL;
    const char * date = NULL;
    bool dead = false;

    bool state_next = false;
    bool author_next = false;

    size_t len = next_line (l, buffer_len, f);
    while (starts_with (*l, "MT ")) {
        if (starts_with (*l, "MT date "))
            date = cache_string (*l + 8);
        if (author_next) {
            if (!starts_with (*l, "MT text "))
                bugger ("Log (%s) author line is not text: %s\n",
                        rcs_file, *l);
            author = cache_string (*l + 8);
            author_next = false;
        }
        if (state_next) {
            if (!starts_with (*l, "MT text "))
                bugger ("Log (%s) state line is not text: %s\n",
                        rcs_file, *l);
            dead = starts_with (*l, "MT text dead");
            state_next = false;
        }
        if (ends_with (*l, " author: "))
            author_next = true;
        if (ends_with (*l, " state: "))
            state_next = true;

        len = next_line (l, buffer_len, f);
    }

    // Snarf the log entry.
    char * log = NULL;
    size_t log_len = 0;
    while (strcmp (*l, REV_BOUNDARY) != 0 &&
           strcmp (*l, FILE_BOUNDARY) != 0) {
        log = xrealloc (log, log_len + len + 1);
        memcpy (log + log_len, *l + 2, len - 2);
        log_len += len - 1;
        log[log_len - 1] = '\n';

        len = next_line (l, buffer_len, f);
    }        

    const char * clog = cache_string_n (log, log_len);
    free (log);

    // Now print what we've got.
    const char * nl = memchr (clog, '\n', log_len);
    if (nl == NULL)
        nl = clog;

    printf ("%s %s %s %s %.*s\n",
            rcs_file, revision, date, author, nl - clog, clog);
}


void read_file_versions (char ** __restrict__ l, size_t * buffer_len, FILE * f)
{
    if (!starts_with (*l, "M RCS file: /"))
        bugger ("Expected RCS file line, not %s\n", *l);

    size_t len = strlen (*l);
    if ((*l)[len - 1] != 'v' || (*l)[len - 2] != ',')
        bugger ("RCS file name does not end with ',v': %s\n", *l);

    const char * rcs_file = cache_string_n (*l + 12, len - 14);

    do {
        len = next_line (l, buffer_len, f);
    }
    while (starts_with (*l, "M head:") ||
           starts_with (*l, "M branch:") ||
           starts_with (*l, "M locks:") ||
           starts_with (*l, "M access list:"));

    if (!starts_with (*l, "M symbolic names:"))
        bugger ("Log (%s) did not have expected tag list: %s\n",
                rcs_file, *l);

    len = next_line (l, buffer_len, f);
    while (starts_with (*l, "M \t")) {
        const char * colon = strrchr (*l, ':');
        if (colon == NULL)
            bugger ("Tag on (%s) did not have version: %s\n", rcs_file, *l);

        const char * tag_name = cache_string_n (*l + 3, colon - *l - 3);
        ++colon;
        if (*colon == ' ')
            ++colon;

        // FIXME - check that version string is a version.
        const char * version = cache_string (colon);

        // FIXME - record it.
        printf ("File %s tag %s version %s\n", rcs_file, tag_name, version);

        len = next_line (l, buffer_len, f);
    };

    while (starts_with (*l, "M keyword substitution:") ||
           starts_with (*l, "M total revisions:"))
        len = next_line (l, buffer_len, f);

    if (!starts_with (*l, "M description:"))
        bugger ("Log (%s) did not have expected 'description' item: %s\n",
                rcs_file, *l);

    // Just skip until a boundary.  Too bad if a log entry contains one of
    // the boundary strings.
    while (strcmp (*l, REV_BOUNDARY) != 0 &&
           strcmp (*l, FILE_BOUNDARY) != 0) {
        if (!starts_with (*l, "M "))
            bugger ("Log (%s) description incorrectly terminated\n",
                    rcs_file);
        len = next_line (l, buffer_len, f);
    }

    while (strcmp (*l, FILE_BOUNDARY) != 0) {
        len = next_line (l, buffer_len, f);
        read_file_version (rcs_file, l, buffer_len, f);
    }

    next_line (l, buffer_len, f);
}


void read_files_versions (char ** __restrict__ l, size_t * buffer_len, FILE * f)
{
    next_line (l, buffer_len, f);

    while (strcmp (*l, "ok ") != 0) {
        if (strcmp (*l, "M ") == 0) {
            next_line (l, buffer_len, f);
            continue;
        }

        read_file_versions (l, buffer_len, f);
    }
}
