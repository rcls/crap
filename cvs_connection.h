#ifndef CVS_H
#define CVS_H

#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>
#include <zlib.h>

typedef struct cvs_connection {
    int socket;
    const char * remote_root;
    const char * module;
    const char * prefix;

    /// Last input line; nul-terminated.
    char * line;

    /// The start of the unused data in line_buf.  This is only used when
    /// inflating a compressed stream.
    char * line_next;

    /// The end of the unused data in line_buf.  This is only used when
    /// inflating a compressed stream.
    char * line_end;

    unsigned long count_versions;
    unsigned long count_transactions;

    FILE * log_in;                      ///< Log of data to cvs server.
    FILE * log_out;                     ///< Log of data from cvs server.

    struct pipeline * pipeline;

    bool compress;                      ///< Are we compressing?

    z_stream deflater;                ///< State for compressing data to server.
    z_stream inflater;            ///< State for decompressing data from server.

    unsigned char * in_next;            ///< Next available input byte.
    unsigned char * in_end;             ///< End of available input data.
    unsigned char * out_next;           ///< Next byte to place output in.

    unsigned char in[4096];             ///< Input buffer.
    unsigned char out[4096];            ///< Output buffer.
    unsigned char zin[4096];            ///< (Compressed) input buffer.
} cvs_connection_t;


/// Create a connection to the CVS server for @c root.
void connect_to_cvs (cvs_connection_t * conn, const char * root);

/// Negotiate compression at the given level.
void cvs_connection_compress (cvs_connection_t * conn, int level);

/// Destroy a connection object.
void cvs_connection_destroy (cvs_connection_t * conn);

/// Call getline and do some sanity checking.
size_t next_line (cvs_connection_t * s);

/// Receive @c n bytes of data and send to file @c f.  If @c f is NULL, then the
/// data is read and discarded.
void cvs_read_block (cvs_connection_t * s, FILE * f, size_t n);

/// Send some data to the cvs connection.
void cvs_printf (cvs_connection_t * s, const char * format, ...)
    __attribute__ ((format (printf, 2, 3)));

/// Send some data to the cvs connection, and flush.
void cvs_printff (cvs_connection_t * s, const char * format, ...)
    __attribute__ ((format (printf, 2, 3)));

#endif
