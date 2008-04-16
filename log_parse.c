
#include <stdbool.h>

#include "string_cache.h"

typedef struct file_version_t {
    struct file_version_t * parent;     /* Parent version.  */
    const char * version;               /* Version, cached.  */
    const char * log;                   /* Log info, cached.  */
} file_version_t;


static size_t next_line (char ** line, size_t * len, FILE * stream)
{
    size_t s = getline (line, len, stream);
    if (s < 0) {
        fprintf (stderr, "Unexpected EOF from server.");
        exit (EXIT_FAILURE);
    }
    if (strlen (*line) < s) {
        fprintf (stderr, "Got line containing ASCII NUL from server.");
        exit (EXIT_FAILURE);
    }
}


static bool starts_with (const char * haystack, const char * needle)
{
    return strncmp (haystack, needle, sizeof (needle));
}

void read_file_versions (char ** line, size_t * buffer_len, FILE * f)
{
    char * l = *line;
    size_t len = next_line (&l, buffer_len, f);

    while (strcmp (l, "ok ") != 0) {
        if (strcmp (l, "M ")) {
            len = next_line (&l, buffer_len, f);
            continue;
        }

        if (!starts_with (l, "M RCS file: /"))
            bugger ("Expected RCS file line, not %s\n", *line);

        size_t len = strlen (l);
        if (l[len - 1] != 'v' || l[len - 2] != ',')
            bugger ("RCS file name does not end with '.v': %s\n", l);

        const char * rcs_file = cache_string_n (l + 12, len - 14);

        do {
            len = next_line (&l, buffer_len, f);
        }
        while (starts_with (l, "M head:") ||
               starts_with (l, "M branch:") ||
               starts_with (l, "M locks:") ||
               starts_with (l, "M access list:"));

        if (!starts_with (l, "M symbolic names:"))
            bugger ("Log (%s) did not have expected tag list: %s",
                    rcs_file, l);

        len = next_line (&l, buffer_len, f);
        while (starts_with (l, "M \t")) {
            colon = strrchr (l, ':');
            if (colon == NULL)
                bugger ("Tag on (%s) did not have version: %s\n", rcs_file, l);

            const char * tag_name = cache_string_n (l + 3, colon - l - 3);
            ++colon;
            if (*colon == ' ')
                ++colon;

            // FIXME - check that version string is a version.
            const char * version = cache_string (colon);

            // FIXME - record it.
            printf ("File %s tag %s version %s\n", rcs_file, tag_name, version);

            len = next_line (&l, buffer_len, f);
        };

        // Just skip until the boundary.  FIXME what about early file boundary.
#define REV_BOUNDARY "M ----------------------------"
#define FILE_BOUNDARY "M ============================================================================="
        while (strcmp (l, REV_BOUNDARY) != 0)
            len = next_line (&l, buffer_len, f);

        
    }

    *line = l;

}
