
#include "log.h"
#include "log_parse.h"
#include "string_cache.h"
#include "xmalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


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


// Parse a date string into a time_t and an offset; the filled in time
// includes the offset and hence is a real Unix time.
static bool parse_cvs_date (time_t * time, time_t * offset, const char * date)
{
    // We parse (YY|YYYY)-MM-DD HH:MM(:SS)?( (+|-)HH(MM?))?
    // This is just like cvsps.  We are a little looser about the digit
    // sequences.  Due to the vagaries of the format we specify
    if (!isdigit (date[0]) || !isdigit (date[1]))
        return false;

    struct tm dtm;

    dtm.tm_year = 0;
    if (date[2] != ':')
        dtm.tm_year = -1900;

    char * d;
    unsigned long year = strtoul (date, &d, 10);
    if (year >= 10000 || *d++ != '-')
        return false;

    dtm.tm_year += year;

    dtm.tm_mon = strtoul (d, &d, 10) - 1;
    if (dtm.tm_mon < 0 || dtm.tm_mon > 11 || *d++ != '-')
        return false;

    dtm.tm_mday = strtoul (d, &d, 10);
    if (dtm.tm_mday < 1 || dtm.tm_mday > 31 || *d++ != ' ')
        return false;

    dtm.tm_hour = strtoul (d, &d, 10);
    if (dtm.tm_hour < 0 || dtm.tm_hour > 24 || *d++ != ':')
        return false;

    dtm.tm_min = strtoul (d, &d, 10);
    if (dtm.tm_min < 0 || dtm.tm_min > 59)
        return false;

    if (*d == ':') {
        ++d;
        dtm.tm_sec = strtoul (d, &d, 10);
        if (dtm.tm_sec < 0 || dtm.tm_sec > 61)
            return false;
    }
    else
        dtm.tm_sec = 0;

    if (*d == 0) {
        *time = timegm (&dtm);
        *offset = 0;
        return true;
    }
    if (*d++ != ' ')
        return false;

    int sign;
    if (*d == '+')
        sign = 1;
    else if (*d == '-')
        sign = -1;
    else
        return false;

    if (!isdigit (d[1]) || !isdigit (d[2]))
        return false;

    time_t off = (d[1] - '0') * 36000 + (d[2] - '0') * 3600;
    d += 3;

    if (*d != 0) {
        if (!isdigit (d[0]) || !isdigit (d[1]))
            return false;
        off += (d[0] - '0') * 600 + (d[1] - '0') * 60;
    }
    if (d[3] != 0)
        return false;

    *time = timegm (&dtm) - sign * off;
    *offset = off;
    return true;
}



void read_file_version (const char * rcs_file,
                        char ** __restrict__ l, size_t * buffer_len, FILE * f)
{
    if (!starts_with (*l, "M revision "))
        bugger ("Log (%s) did not have expected 'revision' line: %s\n",
                rcs_file, *l);

    const char * revision = cache_string (*l + 11);
    const char * author = NULL;
    time_t time = 0;
    time_t offset = 0;
    bool have_date = false;
    bool dead = false;

    bool state_next = false;
    bool author_next = false;

    size_t len = next_line (l, buffer_len, f);
    while (starts_with (*l, "MT ")) {
        if (starts_with (*l, "MT date ")) {
            if (!parse_cvs_date (&time, &offset, *l + 8))
                bugger ("Log (%s) date line has unknown format: %s\n",
                        rcs_file, *l);
            have_date = true;
        }
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

    if (!have_date)
        bugger ("Log (%s) does not have date.\n", rcs_file);

    if (author == NULL)
        bugger ("Log (%s) does not have author.\n", rcs_file);

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

    char off_sign = '+';
    if (offset < 0) {
        offset = -offset;
        off_sign = '-';
    }

    struct tm dtm;
    char date[32];
    size_t dl = strftime (date, sizeof (date), "%F %T %Z",
                          localtime_r (&time, &dtm));
    if (dl == 0) {
        // Maybe someone gave us a crap timezone?
        dl = strftime (date, sizeof (date), "%F %T %Z",
                       gmtime_r (&time, &dtm));
        assert (dl != 0);
    }
        
    printf ("%s %s %s %s %c%02lu%02lu %.*s\n",
            rcs_file, revision, author, date,
            off_sign, offset / 3600, offset / 60 % 60,
            nl - clog, clog);
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

    while (strcmp (*l, "ok") != 0) {
        if (strcmp (*l, "M ") == 0) {
            next_line (l, buffer_len, f);
            continue;
        }

        read_file_versions (l, buffer_len, f);
    }
}
