#ifndef CVS_H
#define CVS_H

#include <stdio.h>
#include <sys/types.h>

typedef struct cvs_connection {
    FILE * stream;
    const char * remote_root;
    const char * module;
    const char * prefix;

    char * line;
    size_t line_len;

    unsigned long count_versions;
    unsigned long count_transactions;
} cvs_connection_t;


void connect_to_cvs (cvs_connection_t * conn, const char * root);

void cvs_connection_destroy (cvs_connection_t * conn);

/// Call getline and do some sanity checking.
size_t next_line (cvs_connection_t * s);

/// Send some data to the cvs connection.
void cvs_printf (cvs_connection_t * s, const char * format, ...)
    __attribute__ ((format (printf, 2, 3)));

/// Record that we've read some data behind the back of the above functions.
void cvs_record_read (cvs_connection_t * s, size_t bytes);

#endif
